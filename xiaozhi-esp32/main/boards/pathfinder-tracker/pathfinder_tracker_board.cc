/*
 * pathfinder_tracker_board.cc
 *
 * PathFinder Tracker board for xiaozhi-esp32.
 * ESP32-S3-N16R8 (16MB Flash + 8MB Octal PSRAM)
 *
 * Hardware:
 *   - AcousticEye V1.0: ES7210 (4-ch ADC) + ES8311 (DAC) + NS4150B (Class-D amp)
 *   - OV2640 DVP camera
 *   - MG90S pan/tilt servos (Phase 3)
 *   - PA_EN power enable (GPIO45, must be HIGH before any peripheral init)
 *
 * Audio architecture:
 *   - BoxAudioCodec: ES7210 TDM 4-ch ADC (CH0 → xiaozhi voice) + ES8311 DAC → NS4150B → speaker
 *   - Shared I2S bus: MCLK=GPIO42, BCLK=GPIO41, WS=GPIO40, DIN=GPIO21, DOUT=GPIO3
 *   - Both codecs at 24kHz
 *
 * Phase 3 (future): MCP tools for servo/sound-source/face-detection
 */

#include "wifi_board.h"
#include <esp_netif.h>
#include "codecs/box_audio_codec.h"
#include "display/display.h"   // NoDisplay
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "led/single_led.h"
#include "led/led.h"
#include "esp32_camera.h"
#include "wifi_manager.h"
#include "ssid_manager.h"
#include "assets/lang_config.h"

/* Mesh + ESP-NOW C headers (wrapped for C++ linkage) */
extern "C" {
#include "mesh_node.h"
#include "mesh_protocol.h"
#include "mesh_espnow.h"
#include "sound_localizer.h"
#include "servo_controller.h"
#include "tracking_coordinator.h"
#include "face_tracker.h"
#include "led_ring.h"
}

#include <esp_log.h>
#include "camera_http_server.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/uart.h>

#define TAG "PathfinderTrackerBoard"

/* ── A板传感器数据缓存 (由 ESP-NOW 回调更新，由 MCP 工具读取) ── */
static sensor_packet_t s_sensor_data;
static SemaphoreHandle_t s_sensor_mutex = NULL;
static int64_t s_sensor_last_us = 0;  /* 上次收到传感器数据的时间 */

static void SensorCacheInit() {
    if (!s_sensor_mutex) {
        s_sensor_mutex = xSemaphoreCreateMutex();
        memset(&s_sensor_data, 0, sizeof(s_sensor_data));
    }
}

static void SensorCacheUpdate(const uint8_t *payload, int len) {
    if (len < (int)sizeof(sensor_packet_t)) return;
    if (xSemaphoreTake(s_sensor_mutex, pdMS_TO_TICKS(10))) {
        memcpy(&s_sensor_data, payload, sizeof(sensor_packet_t));
        s_sensor_last_us = esp_timer_get_time();
        xSemaphoreGive(s_sensor_mutex);
    }
}

static bool SensorCacheGet(sensor_packet_t *out) {
    if (!s_sensor_mutex || s_sensor_last_us == 0) return false;
    if (xSemaphoreTake(s_sensor_mutex, pdMS_TO_TICKS(10))) {
        memcpy(out, &s_sensor_data, sizeof(sensor_packet_t));
        xSemaphoreGive(s_sensor_mutex);
        return true;
    }
    return false;
}

/* ══ extern "C" 转发接口 (供 application.cc 调用，实现字幕/情感同步) ══ */
extern "C" {

/* 转发字幕文本到 A板 (UTF-8, 中文/英文) */
void pathfinder_forward_subtitle(const char *text) {
    if (!text || !*text) return;
    size_t len = strlen(text);
    if (len > 240) len = 240;
    mesh_espnow_send(ESPNOW_BROADCAST_MAC, MSG_CHAT_TEXT,
                     (const uint8_t*)text, len);
}

/* 转发 LLM 情感标签到 A板 (ASCII, e.g. "happy") */
void pathfinder_forward_emotion(const char *emotion) {
    if (!emotion || !*emotion) return;
    size_t len = strlen(emotion);
    if (len > 31) len = 31;
    mesh_espnow_send(ESPNOW_BROADCAST_MAC, MSG_EMOTION,
                     (const uint8_t*)emotion, len);
}

}  /* extern "C" */

class PathfinderTrackerBoard : public WifiBoard {
private:
    Button boot_button_;
    Esp32Camera* camera_ = nullptr;
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    bool mesh_root_started_ = false;
    bool camera_http_started_ = false;

    /* ── Start Mesh ROOT directly (bypasses WifiStation to avoid scan conflicts) ──
     * Mesh ROOT manages its own WiFi connection to the router.
     * WifiStation is NOT started, so its periodic scan timer won't
     * interfere with Mesh's internal WiFi state machine. */
    void StartMeshRootDirect() {
        if (mesh_root_started_) return;

        /* Initialize WiFi driver via WifiManager (NVS + netif + event loop + esp_wifi_init) */
        auto& wifi_mgr = WifiManager::GetInstance();
        WifiManagerConfig config;
        config.ssid_prefix = "Xiaozhi";
        config.language = Lang::CODE;
        wifi_mgr.Initialize(config);

        /* Set event callback to forward mesh-related events to application */
        wifi_mgr.SetEventCallback([this](WifiEvent event, const std::string& data) {
            switch (event) {
                case WifiEvent::Connected:
                    OnNetworkEvent(NetworkEvent::Connected, data);
                    break;
                case WifiEvent::Connecting:
                    OnNetworkEvent(NetworkEvent::Connecting, data);
                    break;
                case WifiEvent::Disconnected:
                    OnNetworkEvent(NetworkEvent::Disconnected);
                    break;
                default:
                    break;
            }
        });

        /* Check for saved WiFi credentials */
        auto& ssid_mgr = SsidManager::GetInstance();
        if (ssid_mgr.GetSsidList().empty()) {
            ESP_LOGW(TAG, "No WiFi credentials saved, entering config mode");
            vTaskDelay(pdMS_TO_TICKS(1500));
            StartWifiConfigMode();
            return;
        }

        /* Use the first saved SSID/password for Mesh ROOT */
        const auto& item = ssid_mgr.GetSsidList()[0];
        ESP_LOGI(TAG, "Starting Mesh ROOT (router='%s', pass_len=%d)...",
                 item.ssid.c_str(), (int)item.password.length());

        esp_err_t ret = mesh_node_start_root_after_wifi(
            item.ssid.c_str(),
            item.password.c_str()
        );

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Mesh ROOT failed: %s, fallback to normal WiFi", esp_err_to_name(ret));
            WifiBoard::StartNetwork();
            return;
        }

        mesh_root_started_ = true;
        mesh_espnow_init();
        mesh_espnow_register_rx_cb(OnEspnowRx);
        mesh_node_register_rx_cb(OnMeshRx);

        /* Background task: wait for ROOT to obtain IP, then notify application */
        xTaskCreate([](void* arg) {
            auto* board = static_cast<PathfinderTrackerBoard*>(arg);
            ESP_LOGI(TAG, "Waiting for Mesh ROOT to connect to router...");
            int timeout = 60;
            while (timeout-- > 0 && !mesh_node_is_connected()) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            if (mesh_node_is_connected()) {
                ESP_LOGI(TAG, "Mesh ROOT connected, notifying application layer");
                board->OnNetworkEvent(NetworkEvent::Connected, "mesh-root");
                /* Start camera HTTP preview server after WiFi is up */
                if (!board->camera_http_started_) {
                    board->camera_http_started_ = true;
                    camera_http_server_start();
                }
            } else {
                ESP_LOGW(TAG, "Mesh ROOT connection timeout (60s)");
            }
            vTaskDelete(NULL);
        }, "mesh_ip_wait", 4096, this, 2, NULL);
    }

    /* ── ESP-NOW receive callback (from PathFinder Tracker CHILD) ── */
    static void OnEspnowRx(const uint8_t *src_mac, const uint8_t *data, int data_len) {
        mesh_msg_t msg;
        if (!mesh_msg_parse(data, data_len, &msg)) return;

        switch (msg.msg_type) {
        case MSG_HEARTBEAT:
            ESP_LOGD(TAG, "ESP-NOW heartbeat from %02x:%02x",
                     src_mac[4], src_mac[5]);
            break;
        case MSG_ANGLE_DATA:
            if (msg.payload_len >= 5) {
                uint16_t angle_fixed = (uint16_t)(msg.payload[0] | (msg.payload[1] << 8));
                bool valid = msg.payload[4] != 0;
                ESP_LOGI(TAG, "Sound angle: %.1f° (valid=%d)",
                         angle_fixed / 10.0f, valid);
            }
            break;
        case MSG_FACE_INFO:
            ESP_LOGD(TAG, "Face info from CHILD: len=%d", msg.payload_len);
            break;
        case MSG_SENSOR_DATA:
            /* A板传感器数据（2Hz上报：温湿度/气压/UV/IMU/罗盘） */
            SensorCacheUpdate(msg.payload, msg.payload_len);
            break;
        default:
            ESP_LOGD(TAG, "ESP-NOW RX type=0x%02X len=%d", msg.msg_type, msg.payload_len);
            break;
        }
    }

    /* ── Mesh P2P receive callback ── */
    static void OnMeshRx(const uint8_t *from_mac, const uint8_t *data, int data_len) {
        mesh_msg_t msg;
        if (!mesh_msg_parse(data, data_len, &msg)) return;
        ESP_LOGI(TAG, "Mesh P2P from %02x:%02x: type=0x%02X len=%d",
                 from_mac[4], from_mac[5], msg.msg_type, msg.payload_len);
    }

    /* I2C master bus for ES7210 + ES8311 codec control.
     * Camera SCCB uses I2C port 1 (hardcoded in esp32-camera),
     * so audio codecs use I2C port 0. */
    void InitializeI2c() {
        i2c_master_bus_config_t bus_cfg = {
            .i2c_port           = (i2c_port_t)0,
            .sda_io_num         = ES7210_I2C_SDA,
            .scl_io_num         = ES7210_I2C_SCL,
            .clk_source         = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt  = 7,
            .intr_priority      = 0,
            .trans_queue_depth  = 0,
            .flags = { .enable_internal_pullup = true },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus_));
        ESP_LOGI(TAG, "I2C bus initialized: SDA=%d SCL=%d port=0", ES7210_I2C_SDA, ES7210_I2C_SCL);
    }

    /* AcousticEye 板级电源使能：必须最先调用，等电源轨稳定再初始化外设。
     * 历史经验：PA_EN 不拉高时 ES7210 与 ES8311 都无响应。 */
    void InitializePowerEnable() {
        gpio_config_t cfg = {
            .pin_bit_mask = (1ULL << PA_EN_GPIO),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&cfg));
        ESP_ERROR_CHECK(gpio_set_level(PA_EN_GPIO, 1));
        ESP_LOGI(TAG, "PA_EN HIGH on GPIO%d (AcousticEye powered)", PA_EN_GPIO);
        vTaskDelay(pdMS_TO_TICKS(100));  // 等电源轨稳定
    }

    void InitializeCamera() {
        camera_config_t config = {};
        config.pin_d0 = CAMERA_PIN_D0;
        config.pin_d1 = CAMERA_PIN_D1;
        config.pin_d2 = CAMERA_PIN_D2;
        config.pin_d3 = CAMERA_PIN_D3;
        config.pin_d4 = CAMERA_PIN_D4;
        config.pin_d5 = CAMERA_PIN_D5;
        config.pin_d6 = CAMERA_PIN_D6;
        config.pin_d7 = CAMERA_PIN_D7;
        config.pin_xclk = CAMERA_PIN_XCLK;
        config.pin_pclk = CAMERA_PIN_PCLK;
        config.pin_vsync = CAMERA_PIN_VSYNC;
        config.pin_href = CAMERA_PIN_HREF;
        config.pin_sccb_sda = CAMERA_PIN_SIOD;
        config.pin_sccb_scl = CAMERA_PIN_SIOC;
        config.sccb_i2c_port = 0;
        config.pin_pwdn = CAMERA_PIN_PWDN;
        config.pin_reset = CAMERA_PIN_RESET;
        config.xclk_freq_hz = XCLK_FREQ_HZ;
        config.pixel_format = PIXFORMAT_RGB565;
        config.frame_size = FRAMESIZE_QVGA;  /* 320x240 — 降低 PSRAM 带宽竞争，避免音频中断 */
        config.jpeg_quality = 12;
        config.fb_count = 2;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
        camera_ = new Esp32Camera(config);
        camera_->SetHMirror(false);
        ESP_LOGI(TAG, "OV2640 camera initialised: QVGA RGB565, XCLK %d Hz", XCLK_FREQ_HZ);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                // 启动阶段短按 BOOT → 进入 WiFi 配网模式
                EnterWifiConfigMode();
                return;
            }
            // 运行期短按 BOOT → 切换对话状态（开始/停止听话）
            app.ToggleChatState();
        });
    }

    /* ── 注册 MCP 工具：让小智 AI 能查询声源定位角度 ──
     * 小智 AI 在对话中可主动调用这些工具，例如用户问 "声音从哪个方向来？"
     * 时 AI 会自动调用 self.sound.get_angle 获取角度。 */
    void RegisterMcpTools() {
        auto& mcp_server = McpServer::GetInstance();

        /* 获取声源角度（0~360°，或 -1 表示无效） */
        mcp_server.AddTool(
            "self.sound.get_angle",
            "获取四麦克风声源定位的最新角度。当用户询问声源方向、声音来自哪里、"
            "或者需要根据声音方向转动设备时使用此工具。\n"
            "返回值：\n"
            "   JSON 字符串，包含 angle(角度0-360)、direction(方向描述)、active(是否有效)\n",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                float angle = sound_localizer_get_angle();
                bool active = sound_localizer_is_active();
                const char* dir = sound_localizer_get_direction(angle);

                char json[160];
                snprintf(json, sizeof(json),
                    "{\"angle\":%.1f,\"direction\":\"%s\",\"active\":%s}",
                    angle, dir, active ? "true" : "false");
                return std::string(json);
            }
        );

        /* 获取 GCC-PHAT 调试参数（开发/调试用） */
        mcp_server.AddTool(
            "self.sound.get_debug",
            "获取声源定位的内部调试信息（GCC-PHAT 各通道互相关峰值、活动量等）。"
            "用于调试和验证声源定位是否正常工作。\n"
            "返回值：\n"
            "   JSON 字符串，包含 activity、peak_x、peak_y、delay_x、delay_y\n",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                float activity, peak_x, peak_y, delay_x, delay_y;
                sound_localizer_get_debug(&activity, &peak_x, &peak_y,
                                          &delay_x, &delay_y);

                char json[256];
                snprintf(json, sizeof(json),
                    "{\"activity\":%.4f,\"peak_x\":%.4f,\"peak_y\":%.4f,"
                    "\"delay_x\":%.1f,\"delay_y\":%.1f}",
                    activity, peak_x, peak_y, delay_x, delay_y);
                return std::string(json);
            }
        );

        /* ── 舵机云台 MCP 工具（Phase 3） ──
         * 小智 AI 可以通过这些工具控制云台：
         * - set_pan/tilt：手动转向指定角度
         * - set_mode：切换自动追踪/手动模式
         * - get_status：查询当前云台状态
         * - look_at_me：根据声源方向转向
         * 典型场景：用户说"看看左边"→AI调用set_pan→舵机左转 */

        /* 设置 Pan 舵机角度（水平 0=左, 90=中, 180=右） */
        mcp_server.AddTool(
            "self.servo.set_pan",
            "设置云台水平(Pan)舵机角度。角度范围0-180：0=最左,90=居中,180=最右。\n"
            "当用户要求转头、转向某个方向、看左边/右边时使用。\n"
            "参数：\n"
            "   `angle`: Pan角度(0-180)\n",
            PropertyList({ Property("angle", kPropertyTypeInteger, 0, 180) }),
            [](const PropertyList& props) -> ReturnValue {
                int angle = props["angle"].value<int>();
                tracking_manual_set_pan(angle);
                return std::string("Pan set to " + std::to_string(angle) + " degrees");
            }
        );

        /* 设置 Tilt 舵机角度（俯仰 0=下, 90=中, 180=上） */
        mcp_server.AddTool(
            "self.servo.set_tilt",
            "设置云台俯仰(Tilt)舵机角度。角度范围0-180：0=最下,90=居中,180=最上。\n"
            "当用户要求抬头、低头、看上方/下方时使用。\n"
            "参数：\n"
            "   `angle`: Tilt角度(0-180)\n",
            PropertyList({ Property("angle", kPropertyTypeInteger, 0, 180) }),
            [](const PropertyList& props) -> ReturnValue {
                int angle = props["angle"].value<int>();
                tracking_manual_set_tilt(angle);
                return std::string("Tilt set to " + std::to_string(angle) + " degrees");
            }
        );

        /* 设置追踪模式 */
        mcp_server.AddTool(
            "self.servo.set_mode",
            "设置云台追踪模式。\n"
            "  `auto`：自动追踪声源（有人说话时Pan舵机自动转向声源方向）\n"
            "  `face`：人脸追踪（摄像头检测肤色区域，Pan+Tilt双轴追踪）\n"
            "  `manual`：手动控制（仅接受set_pan/set_tilt指令）\n"
            "  `idle`：空闲待机（保持当前位置不动）\n"
            "当用户要求开始/停止追踪声音、追踪人脸、或切换模式时使用。\n"
            "参数：\n"
            "   `mode`: 模式名称，可选值：auto, face, manual, idle\n",
            PropertyList({ Property("mode", kPropertyTypeString) }),
            [](const PropertyList& props) -> ReturnValue {
                std::string mode = props["mode"].value<std::string>();
                if (mode == "auto") {
                    tracking_set_mode(TRACK_MODE_AUTO);
                } else if (mode == "manual") {
                    tracking_set_mode(TRACK_MODE_MANUAL);
                } else if (mode == "idle") {
                    tracking_set_mode(TRACK_MODE_IDLE);
                } else if (mode == "face") {
                    face_tracker_start();
                } else {
                    return std::string("Unknown mode: " + mode + ". Use: auto, face, manual, idle");
                }
                return std::string("Tracking mode set to " + mode);
            }
        );

        /* 查询云台状态 */
        mcp_server.AddTool(
            "self.servo.get_status",
            "获取云台当前状态，包括Pan角度、Tilt角度、追踪模式和声源角度。\n"
            "当用户询问云台朝向、当前角度或追踪状态时使用。\n"
            "返回值：JSON包含 pan, tilt, mode, sound_angle, sound_active\n",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                const char* modes[] = {"idle", "auto", "face", "manual"};
                int mode_idx = (int)tracking_get_mode();
                float snd_angle = sound_localizer_get_angle();
                bool snd_active = sound_localizer_is_active();
                char json[200];
                snprintf(json, sizeof(json),
                    "{\"pan\":%d,\"tilt\":%d,\"mode\":\"%s\","
                    "\"sound_angle\":%.1f,\"sound_active\":%s}",
                    tracking_get_pan(), tracking_get_tilt(),
                    modes[mode_idx], snd_angle, snd_active ? "true" : "false");
                return std::string(json);
            }
        );

        /* 转向声源方向 */
        mcp_server.AddTool(
            "self.servo.look_at_sound",
            "根据声源定位结果转向声音来源方向。如果当前有声源定位结果，\n"
            "Pan舵机会自动转向声源角度。适合用户问'谁在说话'或'看那个声音'。\n"
            "如果没有有效声源，返回提示信息。\n",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                float angle = sound_localizer_get_angle();
                if (!sound_localizer_is_active() || angle < 0) {
                    return std::string("No active sound source detected");
                }
                tracking_set_mode(TRACK_MODE_AUTO);
                tracking_on_sound_angle(angle);
                char msg[128];
                snprintf(msg, sizeof(msg),
                    "Looking at sound source at %.1f degrees", angle);
                return std::string(msg);
            }
        );

        /* 云台居中 */
        mcp_server.AddTool(
            "self.servo.center",
            "将云台回到居中位置（Pan=90, Tilt=90）。\n"
            "当用户要求复位、回正、回到中间位置时使用。\n",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                tracking_manual_set_pan_tilt(90, 90);
                return std::string("Servo centered (Pan=90, Tilt=90)");
            }
        );

        /* ── 人脸追踪 MCP 工具 ──
         * 启动/停止基于摄像头的肤色追踪：
         * - face_start：启动追踪，Pan+Tilt 双轴跟随人脸移动
         * - face_stop：停止追踪，切回声源自动模式
         * 典型场景：用户说"看着我"→AI调用face_start→舵机追踪人脸 */

        /* 启动人脸追踪 */
        mcp_server.AddTool(
            "self.face.start",
            "启动人脸追踪模式。摄像头通过 ESP-DL 神经网络(MSRMNP)检测人脸，\n"
            "自动驱动 Pan 和 Tilt 舵机使人脸保持在画面中央。\n"
            "当用户说 \"看着我\"、\"跟着我的脸\"、\n"
            "\"盯着我\"、\"人脸追踪\" 时使用此工具。\n"
            "追踪频率约 6.7Hz，丢失目标约 1.2 秒后自动回中。\n",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                face_tracker_start();
                return std::string("Face tracking started — servo will follow your face");
            }
        );

        /* 停止人脸追踪 */
        mcp_server.AddTool(
            "self.face.stop",
            "停止人脸追踪，切换回声源自动追踪模式。\n"
            "当用户说 \"停止追踪\"、\"别看我了\"、\n"
            "\"取消人脸追踪\" 时使用此工具。\n",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                face_tracker_stop();
                return std::string("Face tracking stopped — switched to sound tracking");
            }
        );

        /* 查询人脸检测状态 */
        mcp_server.AddTool(
            "self.face.status",
            "查询人脸检测与追踪状态。返回是否正在追踪、是否检测到人脸、\n"
            "以及人脸在画面中的位置、大小和置信度（像素坐标 320x240）。\n"
            "当需要确认是否能看到人脸、人脸位置时使用。\n",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                bool running  = face_tracker_is_running();
                bool detected = face_tracker_detected();
                face_info_t info;
                face_tracker_get_info(&info);
                std::string msg = "running=" + std::string(running ? "true" : "false")
                                + ", detected=" + std::string(detected ? "true" : "false");
                if (detected) {
                    msg += ", pos=(" + std::to_string(info.cx) + "," + std::to_string(info.cy)
                         + "), bbox=" + std::to_string(info.w) + "x" + std::to_string(info.h)
                         + ", score=" + std::to_string(info.score).substr(0, 4)
                         + " (320x240)";
                }
                return msg;
            }
        );

        /* ══ A板传感器数据查询工具（3个）══ */

        /* 获取全部传感器数据（JSON） */
        mcp_server.AddTool(
            "self.sensor.get_all",
            "获取A板(PathFinder EMOTE)上所有传感器的最新读数。包括：\n"
            "  - 温湿度(AHT20)\n"
            "  - 气压/海拔(BMP280)\n"
            "  - UV紫外线指数\n"
            "  - 加速度/陀螺仪(MPU9250)\n"
            "  - 罗盘方位角(HMC5883L/QMC5883L)\n"
            "当用户询问温度、湿度、气压、海拔、紫外线、方位角、加速度、\n"
            "姿态、环境状况、周围环境等信息时使用此工具。\n"
            "返回JSON字符串，包含所有传感器数据，age_ms为数据时长(毫秒)。\n",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                sensor_packet_t s;
                if (!SensorCacheGet(&s)) {
                    return std::string("{\"error\":\"No sensor data yet (A-board offline?)\"}");
                }
                int age_ms = (int)((esp_timer_get_time() - s_sensor_last_us) / 1000);

                char json[512];
                snprintf(json, sizeof(json),
                    "{"
                    "\"temperature\":%.1f,\"humidity\":%.1f,"
                    "\"pressure\":%.0f,\"altitude\":%.1f,"
                    "\"uv_index\":%.1f,"
                    "\"accel\":[%.2f,%.2f,%.2f],"
                    "\"gyro\":[%.1f,%.1f,%.1f],"
                    "\"imu_temp\":%.1f,"
                    "\"heading\":%.0f,"
                    "\"mag\":[%.1f,%.1f,%.1f],"
                    "\"flags\":%d,\"age_ms\":%d"
                    "}",
                    s.temperature, s.humidity,
                    s.pressure, s.altitude,
                    s.uv_index,
                    s.accel[0], s.accel[1], s.accel[2],
                    s.gyro[0], s.gyro[1], s.gyro[2],
                    s.imu_temp,
                    s.heading,
                    s.mag[0], s.mag[1], s.mag[2],
                    s.flags, age_ms);
                return std::string(json);
            }
        );

        /* 获取环境数据（温湿度/气压/UV） */
        mcp_server.AddTool(
            "self.sensor.get_env",
            "获取A板环境传感器数据：温度、湿度、气压、海拔、UV指数。\n"
            "当用户问 \"现在多少度\"、\"湿度多少\"、\"气压多少\"、\n"
            "\"海拔多高\"、\"紫外线强不强\" 时使用。\n",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                sensor_packet_t s;
                if (!SensorCacheGet(&s)) {
                    return std::string("{\"error\":\"No sensor data\"}");
                }
                char json[256];
                snprintf(json, sizeof(json),
                    "{\"temperature\":%.1f,\"humidity\":%.1f,"
                    "\"pressure\":%.0f,\"altitude\":%.1f,"
                    "\"uv_index\":%.1f}",
                    s.temperature, s.humidity,
                    s.pressure, s.altitude, s.uv_index);
                return std::string(json);
            }
        );

        /* 获取姿态/方位数据（IMU+罗盘） */
        mcp_server.AddTool(
            "self.sensor.get_motion",
            "获取A板运动传感器数据：加速度(xyz)、陀螺仪(xyz)、罗盘方位角。\n"
            "当用户问 \"设备朝哪个方向\"、\"方位角\"、\"姿态\"、\n"
            "\"倾斜角度\"、\"移动检测\" 时使用。\n",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                sensor_packet_t s;
                if (!SensorCacheGet(&s)) {
                    return std::string("{\"error\":\"No sensor data\"}");
                }
                char json[320];
                snprintf(json, sizeof(json),
                    "{\"heading\":%.0f,"
                    "\"accel\":[%.2f,%.2f,%.2f],"
                    "\"gyro\":[%.1f,%.1f,%.1f],"
                    "\"mag\":[%.1f,%.1f,%.1f]}",
                    s.heading,
                    s.accel[0], s.accel[1], s.accel[2],
                    s.gyro[0], s.gyro[1], s.gyro[2],
                    s.mag[0], s.mag[1], s.mag[2]);
                return std::string(json);
            }
        );

        ESP_LOGI(TAG, "MCP tools registered: sound(2) + servo(6) + sensor(3)");
    }

    /* ── 声源角度 → 追踪协调器转发任务 ──
     * 以 10Hz 频率查询声源定位结果，转发给 tracking_coordinator。
     * 仅在 AUTO 模式下驱动 Pan 舵机。 */
    void StartSoundAngleForwarder() {
        xTaskCreate([](void* arg) {
            while (true) {
                if (sound_localizer_is_active()) {
                    float angle = sound_localizer_get_angle();
                    if (angle >= 0.0f) {
                        tracking_on_sound_angle(angle);
                        led_ring_show_angle(angle);
                    } else {
                        led_ring_show_angle(-1.0f);
                    }
                } else {
                    led_ring_show_angle(-1.0f);
                }
                vTaskDelay(pdMS_TO_TICKS(100));  // 10Hz
            }
            vTaskDelete(NULL);
        }, "snd_fwd", 2048, NULL, 3, NULL);
    }

    /* ── 小智对话状态 → A板转发任务 ──
     * 5Hz 轮询小智对话状态，仅在状态变化时通过 ESP-NOW 发送给 A板
     * A板接收后驱动表情切换（listening→investigate, speaking→smile） */
    void StartDialogStateForwarder() {
        xTaskCreate([](void* arg) {
            uint8_t last_state = 0xFF;
            while (true) {
                auto& app = Application::GetInstance();
                DeviceState ds = app.GetDeviceState();
                uint8_t state;
                switch (ds) {
                    case kDeviceStateListening:  state = 1; break;
                    case kDeviceStateSpeaking:   state = 2; break;
                    case kDeviceStateConnecting: state = 3; break;
                    default:                      state = 0; break;
                }
                /* 仅状态变化时发送（最小化 ESP-NOW 流量） */
                if (state != last_state) {
                    last_state = state;
                    mesh_espnow_send(ESPNOW_BROADCAST_MAC, MSG_DIALOG_STATE, &state, 1);
                }
                vTaskDelay(pdMS_TO_TICKS(200));  // 5Hz 轮询
            }
            vTaskDelete(NULL);
        }, "dlg_fwd", 3072, NULL, 3, NULL);
    }

    /* ── A板传感器 UART1 接收任务 ──
     * A板通过 UART0 TX(GPIO43) 发送带帧头的传感器数据帧
     * B板通过 UART1 RX(GPIO20) 接收解析
     * 帧格式：[0xAA][0x55][0x20][LEN][sensor_packet_t][CRC8] */
    void StartSensorUartRx() {
        /* 初始化 UART1：RX=GPIO20, TX=GPIO19(不用) */
        uart_config_t uart_cfg = {
            .baud_rate  = 115200,
            .data_bits  = UART_DATA_8_BITS,
            .parity     = UART_PARITY_DISABLE,
            .stop_bits  = UART_STOP_BITS_1,
            .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
        };
        uart_driver_install(UART_NUM_1, 512, 0, 0, NULL, 0);
        uart_param_config(UART_NUM_1, &uart_cfg);
        uart_set_pin(UART_NUM_1, 19, 20, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

        /* 后台任务：扫描帧头解析传感器数据 */
        xTaskCreate([](void* arg) {
            ESP_LOGI(TAG, "Sensor UART1 RX task started (RX=GPIO20)");
            uint8_t rxbuf[256];
            const int frame_size = 5 + sizeof(sensor_packet_t);  /* head(2)+type(1)+len(1)+payload+CRC(1) */

            while (true) {
                int len = uart_read_bytes(UART_NUM_1, rxbuf, sizeof(rxbuf), pdMS_TO_TICKS(200));
                if (len <= 0) continue;

                /* 在接收缓冲区中扫描帧头 0xAA 0x55 */
                for (int i = 0; i <= len - frame_size; i++) {
                    if (rxbuf[i] != 0xAA || rxbuf[i+1] != 0x55) continue;
                    if (rxbuf[i+2] != MSG_SENSOR_DATA) continue;

                    uint8_t plen = rxbuf[i+3];
                    if (plen != sizeof(sensor_packet_t)) continue;

                    /* CRC 校验 */
                    uint8_t crc_calc = mesh_crc8(&rxbuf[i+2], 2 + sizeof(sensor_packet_t));
                    uint8_t crc_rx   = rxbuf[i + 4 + sizeof(sensor_packet_t)];
                    if (crc_calc != crc_rx) continue;

                    /* 有效帧！更新传感器缓存 */
                    SensorCacheUpdate(&rxbuf[i+4], sizeof(sensor_packet_t));
                    break;  /* 本批次只处理第一个有效帧 */
                }
            }
            vTaskDelete(NULL);
        }, "sensor_rx", 4096, NULL, 3, NULL);
    }

public:
    PathfinderTrackerBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        // 1. PA_EN 必须最先（电源轨稳定后再启动音频/摄像头外设）
        InitializePowerEnable();
        // 2. I2C 总线（ES7210 + ES8311 共用，摄像头 SCCB 用 port 1 不冲突）
        InitializeI2c();
        // 3. 按键（与外设初始化并行无依赖）
        InitializeButtons();
        // 4. 摄像头（OV2640 DVP，esp32-camera 组件自动用 I2C port 1 做 SCCB）
        InitializeCamera();
        // 5. 声源定位（GCC-PHAT 四麦克风，后台任务）
        sound_localizer_init();
        sound_localizer_start_task();
        // 5b. WS2812 LED 环形方向指示灯初始化
        led_ring_init();
        // 6. 舵机云台初始化（LEDC PWM 双通道 MG90S/ES9052）
        servo_init(SERVO_PAN_GPIO, SERVO_TILT_GPIO);
        tracking_init();
        tracking_start_task();
        // 7. 声源角度→舵机联动转发（10Hz 后台任务）
        StartSoundAngleForwarder();
        // 8. 传感器缓存初始化（A板数据 UART1+ESP-NOW 双通道）
        SensorCacheInit();
        // 9. UART1 接收任务（RX=GPIO20，接收A板传感器数据帧）
        StartSensorUartRx();
        // 10. 小智对话状态转发 → A板表情联动
        StartDialogStateForwarder();
        // 11. 注册 MCP 工具（声源查询 + 舵机控制 + 传感器查询）
        RegisterMcpTools();
        ESP_LOGI(TAG, "PathfinderTrackerBoard ready (board type: pathfinder-tracker)");
    }

    virtual Led* GetLed() override {
        // GPIO48 上已由 led_ring 驱动 36 颗 WS2812，
        // 不能再创建第二个 RMT 设备（SingleLed）在同一引脚。
        // 返回 NoLed 空实现，避免 RMT 通道冲突。
        static NoLed no_led;
        return &no_led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        // BoxAudioCodec: ES7210 TDM 4-ch ADC + ES8311 DAC → NS4150B → speaker
        // Both share I2S0 (TDM mode), I2C port 0, 24kHz
        static BoxAudioCodec codec(
            i2c_bus_,
            AUDIO_INPUT_SAMPLE_RATE,    // 24000
            AUDIO_OUTPUT_SAMPLE_RATE,   // 24000
            AUDIO_I2S_GPIO_MCLK,        // GPIO42
            AUDIO_I2S_GPIO_BCLK,        // GPIO41
            AUDIO_I2S_GPIO_WS,          // GPIO40
            AUDIO_I2S_GPIO_DOUT,        // GPIO3 → ES8311 DIN
            AUDIO_I2S_GPIO_DIN,         // GPIO21 ← ES7210 DOUT
            AUDIO_CODEC_PA_PIN,         // GPIO45 (NS4150B enable)
            AUDIO_CODEC_ES8311_ADDR,    // 0x30 (8-bit, → 0x18 7-bit)
            ES7210_I2C_ADDR,            // 0x80 (8-bit, → 0x40 7-bit)
            false,                      // no input reference (no echo cancellation)
            4                           // tdm_channels=4: TDM 4通道，4麦克风全方向声源定位
        );
        // 注册双通道数据回调 → 声源定位（标准 I2S 立体声数据转发给 GCC-PHAT）
        static bool callback_registered = false;
        if (!callback_registered) {
            codec.SetTdmDataCallback([](const int16_t* data, int frames) {
                sound_localizer_feed(data, frames);
            });
            callback_registered = true;
        }
        return &codec;
    }

    /* B 板无屏幕：返回 NoDisplay，xiaozhi 上层 UI 适配无显示场景 */
    virtual Display* GetDisplay() override {
        static NoDisplay display;
        return &display;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }

    virtual std::string GetBoardType() override {
        return "pathfinder-tracker";
    }

    /* ── Override StartNetwork: use standard WiFi (not Mesh ROOT) ──
     *
     * 历史背景：B板之前使用 ESP-MESH ROOT 模式连接路由器。
     * 但 Mesh 网络栈会干扰 UDP 下行流量，导致小智 TTS 音频包无法到达
     * 设备（recv() 永远不会返回），造成"有字幕无声音"的现象。
     *
     * 修复：改用标准 WiFi STA 连接，ESP-NOW 独立初始化（不需要 Mesh）。
     * A板通过 UART + ESP-NOW 与 B板通信，不依赖 Mesh。 */
    virtual void StartNetwork() override {
        /* Step 1: 标准WiFi连接（继承 WifiBoard::StartNetwork 的全部逻辑） */
        WifiBoard::StartNetwork();

        /* Step 2: 后台等待 WiFi 连接成功，然后初始化 ESP-NOW + Camera */
        xTaskCreate([](void* arg) {
            auto* board = static_cast<PathfinderTrackerBoard*>(arg);
            ESP_LOGI(TAG, "Waiting for WiFi to connect...");
            int timeout = 60;
            while (timeout-- > 0) {
                esp_netif_ip_info_t ip_info;
                esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK &&
                    ip_info.ip.addr != 0) {
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(1000));
            }

            if (timeout > 0) {
                ESP_LOGI(TAG, "WiFi connected, initializing ESP-NOW...");
                mesh_espnow_init();
                mesh_espnow_register_rx_cb(OnEspnowRx);

                if (!board->camera_http_started_) {
                    board->camera_http_started_ = true;
                    camera_http_server_start();
                }
            } else {
                ESP_LOGW(TAG, "WiFi connection timeout (60s), ESP-NOW not started");
            }
            vTaskDelete(NULL);
        }, "wifi_espnow_init", 4096, this, 2, NULL);
    }
};

DECLARE_BOARD(PathfinderTrackerBoard);
