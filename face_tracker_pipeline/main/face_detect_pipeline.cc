/**
 * @file face_detect_pipeline.cc
 * @brief ESP-DL 人脸检测推理任务 (Pipeline Stage 2, CPU 1)
 *
 * =========================================================================
 *  职责：
 *    从 g_frame_queue 接收摄像头帧 → ESP-DL 神经网络推理 → NMS 过滤
 *    → 计算人脸中心偏移量 → 发送到 g_control_queue
 *
 *  模型：face_detection_front (MSRMNP_S8_V1)
 *    - 第一阶段 MSR:  120×160 输入，粗筛人脸候选框 (~33ms)
 *    - 第二阶段 MNP:  48×48  输入，精修关键点     (~6ms)
 *    - 总延迟 ~44ms，模型体积 ~187KB (INT8 量化)
 *    - NMS 阈值 0.5，置信度阈值 0.5
 *
 *  推理流程：
 *    1. 从队列阻塞接收 camera_fb_t* 指针 (零拷贝)
 *    2. 封装为 dl::image::img_t (RGB565 大端)
 *    3. HumanFaceDetect::run(img) — 内部自动 resize + 归一化 + INT8 推理
 *    4. 结果已含 NMS 过滤，取置信度最高的人脸
 *    5. 计算偏移量 err_x = cx - 160, err_y = cy - 120
 *    6. 发送 face_offset_t 到控制队列
 *    7. esp_camera_fb_return(fb) 归还帧缓冲
 *
 *  帧生命周期：
 *    detect_task 负责在推理完成后归还帧缓冲 (esp_camera_fb_return)，
 *    这是零拷贝流水线的关键约束——cam_task 不归还，detect_task 归还。
 * =========================================================================
 */

/* ── C 头文件 (通过 face_tracker.h 的 extern "C" 块引入) ── */
#include "face_tracker.h"

/* ── ESP-DL C++ 头文件 ── */
#include "human_face_detect.hpp"       /* HumanFaceDetect MSRMNP_S8_V1 */
#include "dl_image_define.hpp"          /* dl::image::img_t, pix_type_t */
#include "dl_detect_define.hpp"         /* dl::detect::result_t */

/* ── ESP-IDF 系统头文件 ── */
#include "esp_timer.h"

static const char *TAG = "DetectPipeline";

/* ── 检测器单例 ──
 * HumanFaceDetect 构造时 lazy_load=true (默认)，
 * 首次 run() 时才加载模型到 PSRAM，避免预加载延迟。
 *
 * 模型名 "face_detection_front" 对应 ESP-DL MSRMNP_S8_V1，
 * 已编译为 C 数组 (face_detection_front_model) 链接到固件中。
 * 在实际项目中由 managed_components/human_face_detect 提供。 */
static HumanFaceDetect *s_detector = nullptr;

/* ── 检测置信度阈值 ── */
static constexpr float SCORE_THRESHOLD = 0.5f;
static constexpr float NMS_THRESHOLD   = 0.5f;

/* ── EMA 平滑 (时间域指数移动平均，降噪单帧抖动) ── */
static constexpr float EMA_ALPHA = 0.5f;
static float s_ema_cx = (float)FRAME_CENTER_X;
static float s_ema_cy = (float)FRAME_CENTER_Y;
static bool  s_ema_init = false;

/* ── FPS 与推理耗时统计 ── */
static int64_t s_last_fps_time = 0;
static int s_infer_count = 0;
static float s_avg_infer_ms = 0.0f;

/* ======================================================================== */
/*                      detect_task — 主推理循环                              */
/* ======================================================================== */

void detect_task(void *arg)
{
    ESP_LOGI(TAG, "Face detection task started on Core %d", xPortGetCoreID());

    /* ── 1. 初始化 ESP-DL 人脸检测器 ── */
    ESP_LOGI(TAG, "Loading face_detection_front model (MSRMNP_S8_V1)...");

    s_detector = new HumanFaceDetect(HumanFaceDetect::MSRMNP_S8_V1, false);
    if (!s_detector) {
        ESP_LOGE(TAG, "Failed to allocate HumanFaceDetect — aborting task");
        vTaskDelete(NULL);
        return;
    }

    /* 设置置信度阈值 (阶段 0=MSR, 阶段 1=MNP) */
    s_detector->set_score_thr(SCORE_THRESHOLD, 0);
    s_detector->set_score_thr(SCORE_THRESHOLD, 1);
    s_detector->set_nms_thr(NMS_THRESHOLD, 0);
    s_detector->set_nms_thr(NMS_THRESHOLD, 1);

    ESP_LOGI(TAG, "Model loaded: MSRMNP_S8_V1, score_thr=%.2f, nms_thr=%.2f",
             SCORE_THRESHOLD, NMS_THRESHOLD);

    s_last_fps_time = esp_timer_get_time();
    s_infer_count = 0;

    camera_fb_t *fb = nullptr;

    while (true) {
        /* ── 2. 阻塞等待帧 (从 cam_task 接收 camera_fb_t*) ──
         * portMAX_DELAY: 无限等待，直到队列有数据。
         * 零拷贝：只传指针 (4 字节)，不拷贝像素数据。 */
        if (xQueueReceive(g_frame_queue, &fb, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!fb) {
            ESP_LOGW(TAG, "Received NULL frame pointer");
            continue;
        }

        /* ── 3. 封装 img_t 结构 (零拷贝引用) ──
         * dl::image::img_t 直接引用 fb->buf，不分配新内存。
         * RGB565 大端格式 (OV2640 DVP 原生输出)。 */
        dl::image::img_t img = {};
        img.data     = (void *)fb->buf;
        img.width    = fb->width;
        img.height   = fb->height;
        img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565BE;

        /* ── 4. 执行 ESP-DL 推理 ──
         * HumanFaceDetect::run() 内部完成：
         *   a. ImagePreprocessor: RGB565 → INT8 归一化 + resize 到模型输入 (120×160)
         *   b. MSR 第一阶段推理: 生成候选框
         *   c. NMS 过滤 (IoU > 0.5 的框合并)
         *   d. MNP 第二阶段推理: 精修 5 点关键点
         *   e. 返回 std::list<result_t> (已含 NMS) */
        int64_t t0 = esp_timer_get_time();

        std::list<dl::detect::result_t> &results = s_detector->run(img);

        int64_t infer_ms = (esp_timer_get_time() - t0) / 1000;

        /* ── 5. 处理检测结果 ── */
        face_offset_t offset = FACE_LOST_SENTINEL;

        if (!results.empty()) {
            /* 取置信度最高的人脸 (NMS 已过滤重叠候选框) */
            auto best = results.begin();
            for (auto it = results.begin(); it != results.end(); ++it) {
                if (it->score > best->score) {
                    best = it;
                }
            }

            /* 提取边界框: box = [x1, y1, x2, y2] */
            int x1 = best->box[0];
            int y1 = best->box[1];
            int x2 = best->box[2];
            int y2 = best->box[3];

            int cx = (x1 + x2) / 2;
            int cy = (y1 + y2) / 2;
            int fw = x2 - x1;
            int fh = y2 - y1;

            /* ── EMA 时间域平滑 ──
             * 降低单帧检测噪声，使舵机运动更平滑。 */
            if (!s_ema_init) {
                s_ema_cx = (float)cx;
                s_ema_cy = (float)cy;
                s_ema_init = true;
            } else {
                s_ema_cx = EMA_ALPHA * cx + (1.0f - EMA_ALPHA) * s_ema_cx;
                s_ema_cy = EMA_ALPHA * cy + (1.0f - EMA_ALPHA) * s_ema_cy;
            }

            /* 计算偏移量 (相对于画面中心) */
            offset.detected = true;
            offset.err_x    = (float)(s_ema_cx - FRAME_CENTER_X);
            offset.err_y    = (float)(s_ema_cy - FRAME_CENTER_Y);
            offset.score    = best->score;
            offset.face_w   = fw;
            offset.face_h   = fh;

            ESP_LOGD(TAG, "Face: score=%.2f center=(%d,%d) EMA=(%.1f,%.1f) "
                     "offset=(%.1f,%.1f) %dx%d %lldms",
                     best->score, cx, cy, s_ema_cx, s_ema_cy,
                     offset.err_x, offset.err_y, fw, fh, (long long)infer_ms);
        } else {
            /* ── 无人脸：发送丢失标志 ──
             * control_task 收到 detected=false 后执行回中逻辑。 */
            s_ema_init = false;
            ESP_LOGD(TAG, "No face detected (%lld ms)", (long long)infer_ms);
        }

        /* ── 6. 发送偏移量到控制队列 ──
         * 非阻塞发送：控制队列满则丢弃旧消息 (保留最新偏移量)。 */
        xQueueOverwrite(g_control_queue, &offset);

        /* ── 7. 归还帧缓冲 (关键！) ──
         * detect_task 消费完毕后归还 fb 给 esp_camera 驱动，
         * 使 cam_task 的双缓冲池可继续写入新帧。 */
        esp_camera_fb_return(fb);
        fb = nullptr;

        /* ── 8. FPS 统计 ── */
        s_infer_count++;
        s_avg_infer_ms = s_avg_infer_ms * 0.9f + infer_ms * 0.1f;  /* 滑动平均 */
        int64_t now = esp_timer_get_time();
        if (now - s_last_fps_time >= 1000000) {
            float fps = (float)s_infer_count * 1000000.0f / (float)(now - s_last_fps_time);
            ESP_LOGI(TAG, "Inference FPS: %.1f | avg infer: %.1f ms | face: %s",
                     fps, s_avg_infer_ms,
                     offset.detected ? "YES" : "NO");
            s_infer_count = 0;
            s_last_fps_time = now;
        }
    }

    /* 不应到达 */
    vTaskDelete(NULL);
}
