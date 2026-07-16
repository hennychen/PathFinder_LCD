# PathFinder Tracker Phase 3：人脸检测 → 精细追踪 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在现有声源追踪系统基础上，加入 OV2640 摄像头 + ESP-DL 人脸检测 + PID 舵机精调，实现 Core1 视觉追踪与 Core0 音频追踪的双核协作。

**Architecture:** Core1 运行 DVP 摄像头采集 + ESP-DL 人脸检测推理 + PID 计算，通过 FreeRTOS Queue 向 Core0 的状态机发送人脸偏移指令。状态机新增 FACE_TRACK 状态，当 ACOUSTIC_TRACK 检测到稳定人脸时切换。PID 控制死区 ±10 像素，限速 2°/帧，Pan+Tilt 双轴联动。

**Tech Stack:** ESP-IDF v6.0, esp_camera (DVP), esp-dl (MobileFaceDet), FreeRTOS Queue, PID 控制器

**Spec:** `docs/superpowers/specs/2026-07-16-pathfinder-tracker-multimodal-design.md` §4.3, §7 Phase 3

**Prerequisite:** Phase 1-2 已完成并编译通过（声源→舵机闭环在 Core0 运行）

---

## File Structure

```
PathFinder_Tracker/
├── main/
│   ├── tracker_config.h          # 修改：添加 OV2640 引脚定义
│   ├── main.c                    # 修改：创建 Core1 视觉任务
│   ├── tracker_state_machine.h   # 修改：扩展 face 相关字段
│   ├── tracker_state_machine.c   # 修改：实现 FACE_TRACK 状态
│   ├── vision/                   # 新建目录
│   │   ├── drv_ov2640.h          # 摄像头驱动封装
│   │   ├── drv_ov2640.c
│   │   ├── face_detector.h       # ESP-DL 人脸检测封装
│   │   ├── face_detector.c
│   │   ├── face_tracker.h        # PID 控制器 + 死区
│   │   ├── face_tracker.c
│   │   └── vision_task.h         # Core1 视觉任务入口
│   │   └── vision_task.c
│   ├── CMakeLists.txt            # 修改：添加 vision 源文件
│   └── idf_component.yml         # 修改：添加 esp-dl 依赖
└── components/
    └── model/                    # 人脸检测模型文件 (SPIFFS分区)
```

---

## Task 1: 添加 OV2640 引脚定义与摄像头驱动

**Files:**
- Modify: `main/tracker_config.h`
- Create: `main/vision/drv_ov2640.h`
- Create: `main/vision/drv_ov2640.c`
- Modify: `main/CMakeLists.txt`
- Modify: `main/idf_component.yml`
- Modify: `main/main.c`

- [ ] **Step 1: 在 tracker_config.h 添加 OV2640 引脚定义**

在 `xiaozhi AI TTS` 区块之前插入：

```c
/* ======================== OV2640 摄像头 (DVP) ======================== */
#define CAM_PIN_PWDN    GPIO_NUM_NC  /* 无电源控制 */
#define CAM_PIN_RESET   GPIO_NUM_NC  /* 硬件复位，NC */

/* DVP 8-bit 并行数据总线 */
#define CAM_PIN_D0      GPIO_NUM_11
#define CAM_PIN_D1      GPIO_NUM_9
#define CAM_PIN_D2      GPIO_NUM_8
#define CAM_PIN_D3      GPIO_NUM_10
#define CAM_PIN_D4      GPIO_NUM_12
#define CAM_PIN_D5      GPIO_NUM_18
#define CAM_PIN_D6      GPIO_NUM_17
#define CAM_PIN_D7      GPIO_NUM_16

/* 同步与时钟信号 */
#define CAM_PIN_VSYNC   GPIO_NUM_6
#define CAM_PIN_HREF    GPIO_NUM_7
#define CAM_PIN_PCLK    GPIO_NUM_13
#define CAM_PIN_XCLK    GPIO_NUM_15

/* SCCB 配置接口 (I2C-like) */
#define CAM_PIN_SIOD    GPIO_NUM_4   /* SDA */
#define CAM_PIN_SIOC    GPIO_NUM_5   /* SCL */

/* 摄像头参数 */
#define CAM_XCLK_FREQ   16000000     /* 16MHz XCLK */
#define CAM_FRAME_SIZE  FRAME_240X240 /* 近似 QVGA,适合人脸检测 */
#define CAM_PIXFORMAT   PIXFORMAT_RGB565
```

- [ ] **Step 2: 创建 main/vision/drv_ov2640.h**

```c
#ifndef DRV_OV2640_H
#define DRV_OV2640_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/* 人脸检测使用的图像参数 */
#define FACE_IMG_WIDTH   240
#define FACE_IMG_HEIGHT  240

typedef struct {
    uint8_t  *data;       /* RGB565 图像数据指针 */
    uint32_t  width;
    uint32_t  height;
    uint32_t  format;     /* PIXFORMAT enum 值 */
} camera_frame_t;

/**
 * 初始化 OV2640 DVP 摄像头
 * 配置为 240×240 RGB565, PSRAM 帧缓冲
 */
esp_err_t drv_ov2640_init(void);

/**
 * 获取一帧图像
 * 返回 camera_frame_t 结构,数据在 PSRAM 中
 * 调用者使用完毕后需调用 drv_ov2640_return_frame()
 */
esp_err_t drv_ov2640_capture(camera_frame_t *frame);

/**
 * 归还帧缓冲（让 esp_camera 回收）
 */
void drv_ov2640_return_frame(camera_frame_t *frame);

/**
 * 反初始化摄像头
 */
esp_err_t drv_ov2640_deinit(void);

#endif /* DRV_OV2640_H */
```

- [ ] **Step 3: 创建 main/vision/drv_ov2640.c**

使用 Espressif 官方 `esp_camera` 组件。关键点：
- `camera_config_t` 配置 DVP 引脚映射、240×240 分辨率、RGB565
- `fb_count = 2`（双缓冲）, `xclk = 16MHz`
- `frame_size = FRAME_240X240`
- `pixel_format = PIXFORMAT_RGB565`
- `fb_location = CAMERA_FB_IN_PSRAM`（利用 8MB PSRAM）

```c
#include "drv_ov2640.h"
#include "tracker_config.h"
#include "esp_camera.h"
#include "esp_log.h"

static const char *TAG = "drv_ov2640";
static bool s_initialized = false;

esp_err_t drv_ov2640_init(void)
{
    camera_config_t config = {
        .pin_pwdn     = CAM_PIN_PWDN,
        .pin_reset    = CAM_PIN_RESET,
        .pin_xclk     = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,

        .pin_d7   = CAM_PIN_D7,
        .pin_d6   = CAM_PIN_D6,
        .pin_d5   = CAM_PIN_D5,
        .pin_d4   = CAM_PIN_D4,
        .pin_d3   = CAM_PIN_D3,
        .pin_d2   = CAM_PIN_D2,
        .pin_d1   = CAM_PIN_D1,
        .pin_d0   = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href  = CAM_PIN_HREF,
        .pin_pclk  = CAM_PIN_PCLK,

        .xclk_freq_hz = CAM_XCLK_FREQ,
        .ledc_timer   = LEDC_TIMER_1,   /* 与舵机 Timer 0 不冲突 */
        .ledc_channel = LEDC_CHANNEL_2,

        .pixel_format  = PIXFORMAT_RGB565,
        .frame_size    = FRAMESIZE_240X240,
        .jpeg_quality  = 12,
        .fb_count      = 2,
        .fb_location   = CAMERA_FB_IN_PSRAM,
        .grab_mode     = CAMERA_GRAB_WHEN_EMPTY,
    };

    esp_err_t ret = esp_camera_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "OV2640 ready: 240x240 RGB565, XCLK=16MHz, PSRAM fb");
    return ESP_OK;
}

esp_err_t drv_ov2640_capture(camera_frame_t *frame)
{
    if (!s_initialized || !frame) return ESP_ERR_INVALID_STATE;

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGW(TAG, "Frame buffer get failed");
        return ESP_FAIL;
    }

    frame->data   = fb->buf;
    frame->width  = fb->width;
    frame->height = fb->height;
    frame->format = fb->format;
    /* 将 fb 指针存在 data 的对齐前 8 字节供 return_frame 回收 */
    /* 实际方案：用全局 last_fb 指针 */
    return ESP_OK;
}
```

> 注意：esp_camera 的帧缓冲管理需要跟踪 `camera_fb_t*` 指针以便回收。实际实现中用 `static camera_fb_t *s_last_fb` 保存上次获取的帧，在 `drv_ov2640_return_frame()` 中调用 `esp_camera_fb_return(s_last_fb)`。

- [ ] **Step 4: 更新 idf_component.yml 添加 esp_camera 依赖**

```yaml
dependencies:
  idf:
    version: '>=5.0'
  esp-dsp:
    version: '>=1.5'
  led_strip:
    version: '>=2.5'
  esp_camera:
    version: '>=2.0'
```

- [ ] **Step 5: 更新 main/CMakeLists.txt**

在 SRCS 添加 `"vision/drv_ov2640.c"`，在 INCLUDE_DIRS 添加 `"vision"`，在 REQUIRES 添加 `esp_camera`

- [ ] **Step 6: 在 main.c 中添加摄像头初始化测试**

在 ES7210 初始化之前添加：
```c
#include "drv_ov2640.h"

/* 在 app_main 中 */
ret = drv_ov2640_init();
if (ret != ESP_OK) {
    printf("[%s] Camera init FAILED: %s\n", TAG, esp_err_to_name(ret));
    /* Non-fatal for Phase 3 testing */
}
```

- [ ] **Step 7: 编译验证**

Run: `. /Users/pm/esp/esp-idf/export.sh && cd /Users/pm/PathFinder_LCD/PathFinder_Tracker && idf.py build`
Expected: 编译成功，esp_camera 组件被正确下载和链接

- [ ] **Step 8: 提交**

```bash
cd /Users/pm/PathFinder_LCD
git add PathFinder_Tracker
git commit -m "feat: add OV2640 DVP camera driver on Core1 vision path"
```

---

## Task 2: ESP-DL 人脸检测集成

**Files:**
- Modify: `main/idf_component.yml`
- Create: `main/vision/face_detector.h`
- Create: `main/vision/face_detector.c`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: 在 idf_component.yml 添加 esp-dl 依赖**

```yaml
dependencies:
  idf:
    version: '>=5.0'
  esp-dsp:
    version: '>=1.5'
  led_strip:
    version: '>=2.5'
  esp_camera:
    version: '>=2.0'
  esp-dl:
    version: '>=2.0'
```

- [ ] **Step 2: 创建 main/vision/face_detector.h**

```c
#ifndef FACE_DETECTOR_H
#define FACE_DETECTOR_H

#include "drv_ov2640.h"
#include <stdbool.h>

#define MAX_FACES 4

typedef struct {
    int16_t x;        /* 人脸框左上角 X */
    int16_t y;        /* 人脸框左上角 Y */
    uint16_t width;   /* 框宽 */
    uint16_t height;  /* 框高 */
    float   confidence; /* 置信度 0~1 */
} face_box_t;

typedef struct {
    int       count;    /* 检测到的人脸数量 (0 = 无人脸) */
    face_box_t faces[MAX_FACES];
    uint32_t  inference_ms; /* 推理耗时（毫秒） */
} face_result_t;

/**
 * 初始化 ESP-DL 人脸检测模型
 * 将模型加载到 PSRAM
 */
esp_err_t face_detector_init(void);

/**
 * 对一帧 RGB565 图像执行人脸检测
 * 返回检测到的人脸列表
 */
esp_err_t face_detector_detect(const camera_frame_t *frame, face_result_t *result);

/**
 * 从多个人脸中选择面积最大的（最可能是追踪目标）
 */
const face_box_t *face_detector_pick_largest(const face_result_t *result);

#endif /* FACE_DETECTOR_H */
```

- [ ] **Step 3: 创建 main/vision/face_detector.c**

使用 ESP-DL 的 HumanFaceDetect 类（`esp-dl/vision/human_face_detect`）。

```c
#include "face_detector.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "face_det";

#if CONFIG_IDF_TARGET_ESP32S3
#include "esp_human_face_detect.hpp"

/* ESP-DL MSRMNP 人脸检测器实例 */
static HumanFaceDetectMSRMNP *s_detector = nullptr;
static bool s_initialized = false;
#endif

esp_err_t face_detector_init(void)
{
#if CONFIG_IDF_TARGET_ESP32S3
    s_detector = new HumanFaceDetectMSRMNP();
    if (!s_detector) {
        ESP_LOGE(TAG, "Failed to allocate HumanFaceDetectMSRMNP");
        return ESP_ERR_NO_MEM;
    }
    s_initialized = true;
    ESP_LOGI(TAG, "ESP-DL face detector ready (MSRMNP model)");
    return ESP_OK;
#else
    ESP_LOGE(TAG, "Face detection only supported on ESP32-S3");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t face_detector_detect(const camera_frame_t *frame, face_result_t *result)
{
    if (!s_initialized || !frame || !result) return ESP_ERR_INVALID_ARG;

    result->count = 0;

#if CONFIG_IDF_TARGET_ESP32S3
    int64_t t_start = esp_timer_get_time();

    /* esp_camera 的 RGB565 帧直接喂给检测器 */
    std::list<dl::detect::result_t> det_results;

    /* HumanFaceDetectMSRMNP::detect() 接受 uint16_t* RGB565 数据 */
    det_results = s_detector->detect((uint16_t *)frame->data,
                                      frame->height, frame->width);

    result->inference_ms = (uint32_t)((esp_timer_get_time() - t_start) / 1000);

    int idx = 0;
    for (const auto &res : det_results) {
        if (idx >= MAX_FACES) break;
        result->faces[idx].x          = res.box[0];
        result->faces[idx].y          = res.box[1];
        result->faces[idx].width      = res.box[2] - res.box[0];
        result->faces[idx].height     = res.box[3] - res.box[1];
        result->faces[idx].confidence = res.score;
        idx++;
    }
    result->count = idx;
#endif

    return ESP_OK;
}

const face_box_t *face_detector_pick_largest(const face_result_t *result)
{
    if (!result || result->count == 0) return NULL;

    const face_box_t *largest = &result->faces[0];
    int max_area = largest->width * largest->height;

    for (int i = 1; i < result->count; i++) {
        int area = result->faces[i].width * result->faces[i].height;
        if (area > max_area) {
            max_area = area;
            largest = &result->faces[i];
        }
    }
    return largest;
}
```

> **注意：** ESP-DL 的 C++ API 需要在 `.c` 文件中使用会导致编译错误。实际实现需将此文件命名为 `face_detector.cpp` 或使用 `extern "C"` 包装。CMakeLists.txt 中 SRCS 需列出 `.cpp` 文件。

- [ ] **Step 4: 将 face_detector.c 重命名为 face_detector.cpp**

由于 ESP-DL 使用 C++ 类，此文件必须用 C++ 编译：

```bash
cd /Users/pm/PathFinder_LCD/PathFinder_Tracker/main/vision
mv face_detector.c face_detector.cpp
```

在 `face_detector.cpp` 的头文件保护后添加：
```cpp
#ifdef __cplusplus
extern "C" {
#endif
```

在文件末尾添加：
```cpp
#ifdef __cplusplus
}
#endif
```

- [ ] **Step 5: 更新 main/CMakeLists.txt**

SRCS 中将 `"vision/face_detector.c"` 改为 `"vision/face_detector.cpp"`。添加 `esp-dl` 到 REQUIRES。

- [ ] **Step 6: 编译验证**

Run: `idf.py build`
Expected: 编译成功（注意 esp-dl 组件可能较大，首次编译时间较长）

- [ ] **Step 7: 提交**

```bash
cd /Users/pm/PathFinder_LCD
git add PathFinder_Tracker
git commit -m "feat: integrate ESP-DL MSRMNP face detection model"
```

---

## Task 3: PID 面部追踪控制器

**Files:**
- Create: `main/vision/face_tracker.h`
- Create: `main/vision/face_tracker.c`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: 创建 main/vision/face_tracker.h**

```c
#ifndef FACE_TRACKER_H
#define FACE_TRACKER_H

#include "face_detector.h"
#include "drv_servo.h"
#include <stdbool.h>

/* PID 控制参数 */
#define FACE_PID_KP_PAN     0.08f   /* Pan 比例增益 */
#define FACE_PID_KP_TILT    0.06f   /* Tilt 比例增益 */
#define FACE_DEAD_ZONE_PX   10      /* 像素死区,消除微抖 */
#define FACE_MAX_STEP_DEG   2.0f    /* 单帧最大移动角度 */

/* 图像中心坐标 (240×240 图像) */
#define IMG_CENTER_X  (FACE_IMG_WIDTH / 2)
#define IMG_CENTER_Y  (FACE_IMG_HEIGHT / 2)

typedef struct {
    float pan;   /* 当前 Pan 角度 */
    float tilt;  /* 当前 Tilt 角度 */
    int   lost_count;  /* 连续丢失帧数 */
    bool  tracking;    /* 是否正在追踪 */
} face_tracker_ctx_t;

/**
 * 初始化人脸追踪上下文
 */
void face_tracker_init(face_tracker_ctx_t *ctx);

/**
 * 根据人脸框位置执行 PID 控制
 * target: 目标人脸框 (NULL 表示丢失)
 * ctx: 追踪上下文
 * 返回: true=已驱动舵机, false=未动作(死区内/丢失)
 */
bool face_tracker_update(face_tracker_ctx_t *ctx, const face_box_t *target);

/**
 * 获取人脸丢失连续帧数
 */
int face_tracker_get_lost_count(const face_tracker_ctx_t *ctx);

#endif /* FACE_TRACKER_H */
```

- [ ] **Step 2: 创建 main/vision/face_tracker.c**

```c
#include "face_tracker.h"
#include "drv_servo.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "face_trk";

void face_tracker_init(face_tracker_ctx_t *ctx)
{
    ctx->pan  = 90.0f;  /* 居中 */
    ctx->tilt = 90.0f;
    ctx->lost_count = 0;
    ctx->tracking = false;
}

bool face_tracker_update(face_tracker_ctx_t *ctx, const face_box_t *target)
{
    if (!ctx) return false;

    /* 无人脸 */
    if (!target) {
        ctx->lost_count++;
        ctx->tracking = false;
        return false;
    }

    /* 有人脸，重置丢失计数 */
    ctx->lost_count = 0;
    ctx->tracking = true;

    /* 计算人脸中心与图像中心的偏差 */
    int face_cx = target->x + target->width / 2;
    int face_cy = target->y + target->height / 2;
    int err_x = face_cx - IMG_CENTER_X;  /* 正=人脸偏右,需左转(减小Pan角度) */
    int err_y = face_cy - IMG_CENTER_Y;  /* 正=人脸偏下,需上仰(减小Tilt角度) */

    /* 死区检查 */
    if (abs(err_x) < FACE_DEAD_ZONE_PX && abs(err_y) < FACE_DEAD_ZONE_PX) {
        /* 在死区内,不动作 */
        return false;
    }

    /* P 控制器: 像素偏差 → 角度增量 */
    float pan_delta  = -(float)err_x * FACE_PID_KP_PAN;   /* 负号: 偏右→减小Pan */
    float tilt_delta = -(float)err_y * FACE_PID_KP_TILT;

    /* 限速: 单帧最大 2° */
    if (pan_delta > FACE_MAX_STEP_DEG)  pan_delta = FACE_MAX_STEP_DEG;
    if (pan_delta < -FACE_MAX_STEP_DEG) pan_delta = -FACE_MAX_STEP_DEG;
    if (tilt_delta > FACE_MAX_STEP_DEG)  tilt_delta = FACE_MAX_STEP_DEG;
    if (tilt_delta < -FACE_MAX_STEP_DEG) tilt_delta = -FACE_MAX_STEP_DEG;

    /* 更新目标角度 */
    ctx->pan  += pan_delta;
    ctx->tilt += tilt_delta;

    /* 钳制到 0~180 */
    if (ctx->pan < 0)  ctx->pan = 0;
    if (ctx->pan > 180) ctx->pan = 180;
    if (ctx->tilt < 0) ctx->tilt = 0;
    if (ctx->tilt > 180) ctx->tilt = 180;

    /* 驱动舵机 */
    drv_servo_set_angle(SERVO_PAN, ctx->pan);
    drv_servo_set_angle(SERVO_TILT, ctx->tilt);

    return true;
}

int face_tracker_get_lost_count(const face_tracker_ctx_t *ctx)
{
    return ctx ? ctx->lost_count : -1;
}
```

- [ ] **Step 3: 更新 main/CMakeLists.txt**

在 SRCS 添加 `"vision/face_tracker.c"`

- [ ] **Step 4: 编译验证**

Run: `idf.py build`
Expected: 编译成功

- [ ] **Step 5: 提交**

```bash
git add PathFinder_Tracker
git commit -m "feat: add PID face tracker with dead zone and slew limiting"
```

---

## Task 4: Core1 视觉任务 + 双核通信

**Files:**
- Create: `main/vision/vision_task.h`
- Create: `main/vision/vision_task.c`
- Modify: `main/tracker_state_machine.h`
- Modify: `main/tracker_state_machine.c`
- Modify: `main/CMakeLists.txt`
- Modify: `main/main.c`

- [ ] **Step 1: 创建 main/vision/vision_task.h**

```c
#ifndef VISION_TASK_H
#define VISION_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include "face_detector.h"

/* Core0 ← Core1 的消息: 人脸检测结果 */
typedef struct {
    bool     face_found;       /* 是否检测到人脸 */
    int16_t  face_cx;          /* 人脸中心 X (像素) */
    int16_t  face_cy;          /* 人脸中心 Y (像素) */
    uint16_t face_w;           /* 人脸框宽 */
    uint16_t face_h;           /* 人脸框高 */
    uint32_t inference_ms;     /* 推理耗时 */
} vision_msg_t;

/**
 * 初始化视觉子系统 (摄像头 + 人脸检测 + Queue)
 * 创建 Core1 视觉任务
 */
esp_err_t vision_task_init(void);

/**
 * Core0 调用: 非阻塞读取最新人脸检测结果
 * msg: 输出参数
 * 返回 true 如果有新结果
 */
bool vision_get_latest(vision_msg_t *msg);

#endif /* VISION_TASK_H */
```

- [ ] **Step 2: 创建 main/vision/vision_task.c**

```c
#include "vision_task.h"
#include "drv_ov2640.h"
#include "face_detector.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "vision";

#define VISION_TASK_STACK   16384   /* ESP-DL 需要较大栈 */
#define VISION_QUEUE_LEN    2
#define VISION_CORE         1       /* 运行在 Core1 */

static QueueHandle_t s_vision_queue = NULL;
static bool s_vision_running = false;

/* Core1 视觉任务 */
static void vision_task(void *arg)
{
    ESP_LOGI(TAG, "Vision task started on Core %d", xPortGetCoreID());

    camera_frame_t frame;
    face_result_t  face_result;
    vision_msg_t   msg;

    while (s_vision_running) {
        /* 1. 采集一帧 */
        if (drv_ov2640_capture(&frame) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* 2. 人脸检测推理 */
        esp_err_t ret = face_detector_detect(&frame, &face_result);

        /* 3. 归还帧缓冲 */
        drv_ov2640_return_frame(&frame);

        if (ret != ESP_OK) {
            continue;
        }

        /* 4. 选择最大人脸 */
        const face_box_t *best = face_detector_pick_largest(&face_result);

        /* 5. 组装消息发送给 Core0 */
        msg.face_found     = (best != NULL);
        msg.inference_ms   = face_result.inference_ms;
        if (best) {
            msg.face_cx = best->x + best->width / 2;
            msg.face_cy = best->y + best->height / 2;
            msg.face_w  = best->width;
            msg.face_h  = best->height;
        }

        /* 覆盖式发送: Drop old message if queue is full */
        xQueueOverwrite(s_vision_queue, &msg);

        /* 6. 日志 (每秒一次) */
        static int64_t last_log = 0;
        int64_t now = esp_timer_get_time();
        if (now - last_log > 1000000) {  /* 1秒 */
            if (best) {
                ESP_LOGI(TAG, "Face: cx=%d cy=%d %dx%d inf=%lums",
                         msg.face_cx, msg.face_cy, msg.face_w, msg.face_h,
                         (unsigned long)msg.inference_ms);
            } else {
                ESP_LOGI(TAG, "No face, inf=%lums",
                         (unsigned long)msg.inference_ms);
            }
            last_log = now;
        }

        /* 帧率自适应: 检测到人脸时 8-10fps, 空闲时降到 2fps */
        vTaskDelay(pdMS_TO_TICKS(best ? 100 : 500));
    }

    ESP_LOGI(TAG, "Vision task exiting");
    vTaskDelete(NULL);
}

esp_err_t vision_task_init(void)
{
    if (s_vision_running) {
        ESP_LOGW(TAG, "Vision task already running");
        return ESP_OK;
    }

    /* 初始化摄像头 */
    esp_err_t ret = drv_ov2640_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 初始化人脸检测器 */
    ret = face_detector_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Face detector init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 创建 Queue (覆盖式) */
    s_vision_queue = xQueueCreate(VISION_QUEUE_LEN, sizeof(vision_msg_t));
    if (!s_vision_queue) {
        ESP_LOGE(TAG, "Failed to create vision queue");
        return ESP_ERR_NO_MEM;
    }

    /* 启动 Core1 视觉任务 */
    s_vision_running = true;
    BaseType_t xret = xTaskCreatePinnedToCore(
        vision_task,
        "vision_task",
        VISION_TASK_STACK,
        NULL,
        4,               /* 优先级 4 (低于音频任务) */
        NULL,
        VISION_CORE      /* Core 1 */
    );

    if (xret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create vision task");
        s_vision_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Vision subsystem ready on Core1, stack=%d", VISION_TASK_STACK);
    return ESP_OK;
}

bool vision_get_latest(vision_msg_t *msg)
{
    if (!s_vision_queue || !msg) return false;
    return xQueuePeek(s_vision_queue, msg, 0) == pdTRUE;
}
```

- [ ] **Step 3: 更新 tracker_state_machine.h**

在 `tracker_ctx_t` 中添加 face 相关字段：

```c
#include "vision_task.h"  /* 添加 include */

typedef struct {
    tracker_state_t current_state;
    float   smoothed_angle;
    float   last_valid_angle;
    int     invalid_count;
    int     search_count;
    /* Phase 3: 人脸追踪 */
    face_tracker_ctx_t face_ctx;  /* PID 人脸追踪上下文 */
    int     face_lost_count;       /* 连续无人脸帧数 */
} tracker_ctx_t;

/* Phase 3: 新增接口 */
void tracker_sm_face_step(tracker_ctx_t *ctx, const vision_msg_t *vmsg);
```

- [ ] **Step 4: 更新 tracker_state_machine.c**

在文件顶部添加：
```c
#include "face_tracker.h"
#include "vision_task.h"
```

实现 `tracker_sm_face_step()` — 这是 Core0 在主循环中调用的，读取 Core1 发来的视觉消息：

```c
#define FACE_LOST_THRESHOLD  30   /* ~3秒(30帧@10fps)无脸→退出FACE_TRACK */

void tracker_sm_face_step(tracker_ctx_t *ctx, const vision_msg_t *vmsg)
{
    if (!ctx || !vmsg) return;

    switch (ctx->current_state) {

    case TRACKER_STATE_ACOUSTIC_TRACK:
        /* 在声源追踪中,如果检测到人脸,切换到 FACE_TRACK */
        if (vmsg->face_found) {
            ctx->current_state = TRACKER_STATE_FACE_TRACK;
            ctx->face_lost_count = 0;
            face_tracker_init(&ctx->face_ctx);
            ESP_LOGI(TAG, "ACOUSTIC_TRACK -> FACE_TRACK (face detected)");
            drv_uart_send_state((uint8_t)TRACKER_STATE_FACE_TRACK);
        }
        break;

    case TRACKER_STATE_FACE_TRACK:
        if (vmsg->face_found) {
            /* 将消息转换为人脸框 */
            face_box_t box;
            box.x = vmsg->face_cx - vmsg->face_w / 2;
            box.y = vmsg->face_cy - vmsg->face_h / 2;
            box.width = vmsg->face_w;
            box.height = vmsg->face_h;
            box.confidence = 1.0f;  /* 可从检测器传入实际值 */

            /* PID 控制 */
            face_tracker_update(&ctx->face_ctx, &box);

            /* 上报人脸信息给板子A */
            drv_uart_send_state((uint8_t)TRACKER_STATE_FACE_TRACK);

            ctx->face_lost_count = 0;
        } else {
            ctx->face_lost_count++;
            if (ctx->face_lost_count >= FACE_LOST_THRESHOLD) {
                /* 人脸丢失超时,回到声源追踪 */
                ctx->current_state = TRACKER_STATE_ACOUSTIC_TRACK;
                ESP_LOGI(TAG, "FACE_TRACK -> ACOUSTIC_TRACK (face lost)");
                drv_uart_send_state((uint8_t)TRACKER_STATE_ACOUSTIC_TRACK);
            }
        }
        break;

    case TRACKER_STATE_SEARCH:
        /* 搜索中如果发现人脸,直接进入 FACE_TRACK */
        if (vmsg->face_found) {
            ctx->current_state = TRACKER_STATE_FACE_TRACK;
            ctx->face_lost_count = 0;
            face_tracker_init(&ctx->face_ctx);
            ESP_LOGI(TAG, "SEARCH -> FACE_TRACK (face found during search)");
            drv_uart_send_state((uint8_t)TRACKER_STATE_FACE_TRACK);
        }
        break;

    default:
        break;
    }
}
```

- [ ] **Step 5: 更新 main/CMakeLists.txt**

SRCS 添加 `"vision/vision_task.c"`。REQUIRES 添加 `esp-dl` 和 `esp_camera`。

- [ ] **Step 6: 更新 main.c**

添加视觉任务初始化和双核主循环：

```c
#include "vision_task.h"

void app_main(void)
{
    printf("[%s] PathFinder_Tracker Phase 3 starting\n", TAG);

    /* Core0 外设初始化 */
    drv_ws2812_init();
    drv_servo_init();
    drv_uart_init();

    /* Core1 视觉子系统 (摄像头 + 人脸检测) */
    esp_err_t vret = vision_task_init();
    if (vret != ESP_OK) {
        printf("[%s] Vision init FAILED (non-fatal): %s\n", TAG, esp_err_to_name(vret));
        /* 继续运行声源追踪 */
    }

    /* Core0 音频初始化 */
    esp_err_t ret = drv_es7210_init();
    if (ret != ESP_OK) {
        printf("[%s] ES7210 init FAILED: %s\n", TAG, esp_err_to_name(ret));
        return;
    }

    sound_localizer_init(ES7210_SAMPLE_RATE);
    sound_localizer_set_threshold(-2.7f);
    tracker_sm_init(&s_tracker_ctx);

    /* 双核主循环: Core0 音频 + 人脸协调 */
    static float mic_data[4][256];
    vision_msg_t vmsg;

    while (1) {
        /* 1. Core0: 音频采集 + 声源定位 + 声源状态机 */
        if (drv_es7210_read(mic_data) == ESP_OK) {
            localization_result_t result = sound_localizer_compute(mic_data);
            tracker_sm_step(&s_tracker_ctx, result.angle, result.valid);
        }

        /* 2. Core0: 检查 Core1 视觉结果,执行人脸状态转换 */
        if (vision_get_latest(&vmsg)) {
            tracker_sm_face_step(&s_tracker_ctx, &vmsg);
        }

        taskYIELD();
    }
}
```

- [ ] **Step 7: 编译验证**

Run: `idf.py build`
Expected: 编译成功。esp-dl 首次编译可能需要较长时间。

- [ ] **Step 8: 提交**

```bash
git add PathFinder_Tracker
git commit -m "feat: dual-core vision task with ESP-DL face detection on Core1"
```

---

## Task 5: 端到端联调与状态机整合

**Files:**
- Modify: `main/tracker_state_machine.c` — 完善 SEARCH 状态中的人脸检测
- Modify: `main/main.c` — 确保 FACE_TRACK 时 WS2812 状态指示
- Modify: `main/tracker_state_machine.c` — FACE_TRACK 时 WS2812 蓝色指示

- [ ] **Step 1: 在 FACE_TRACK 状态中添加 WS2812 蓝色指示**

在 `tracker_sm_face_step()` 的 `FACE_TRACK` case 中，当追踪到人脸时：
```c
/* 视觉追踪指示: 蓝色环 */
drv_ws2812_clear();
for (int i = 0; i < WS2812_LED_COUNT; i += 3) {
    drv_ws2812_set_led(i, 0, 0, 100);  /* 每3颗亮一颗蓝色 */
}
drv_ws2812_show();
```

- [ ] **Step 2: 确保 FACE_TRACK 退出时清理 WS2812**

在从 FACE_TRACK 切换到 ACOUSTIC_TRACK 时恢复红色指示逻辑。

- [ ] **Step 3: 在 FACE_TRACK 状态下抑制声源舵机控制**

在 `tracker_sm_step()` 的 `ACOUSTIC_TRACK` case 开头添加检查：
```c
/* 如果当前已进入 FACE_TRACK,声源状态机不再控制舵机 */
if (ctx->current_state != TRACKER_STATE_ACOUSTIC_TRACK) break;
```

- [ ] **Step 4: 编译验证**

Run: `idf.py build`
Expected: 编译成功

- [ ] **Step 5: 提交**

```bash
git add PathFinder_Tracker
git commit -m "feat: integrate face track state machine with WS2812 status and servo priority"
```

---

## Task 6: 性能验证与帧率优化

**Files:**
- Modify: `main/vision/vision_task.c` — 帧率自适应优化
- Modify: `main/vision/face_tracker.c` — PID 参数调优注释

- [ ] **Step 1: 添加帧率监控日志**

在 `vision_task.c` 中添加每5秒的平均 FPS 日志：

```c
static int frame_count = 0;
static int64_t fps_timer_start = 0;

/* 在每次采集后 */
frame_count++;
if (esp_timer_get_time() - fps_timer_start > 5000000) { /* 5秒 */
    float fps = (float)frame_count * 1000000.0f /
                (esp_timer_get_time() - fps_timer_start);
    ESP_LOGI(TAG, "FPS: %.1f (avg inference: %lums)",
             fps, (unsigned long)msg.inference_ms);
    frame_count = 0;
    fps_timer_start = esp_timer_get_time();
}
```

- [ ] **Step 2: 添加内存监控**

在 `vision_task` 启动后添加一次性内存日志：
```c
ESP_LOGI(TAG, "Free heap: %lu, Free PSRAM: %lu",
         (unsigned long)esp_get_free_heap_size(),
         (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
```

- [ ] **Step 3: 提交**

```bash
git add PathFinder_Tracker
git commit -m "feat: add FPS and memory monitoring for vision task"
```

---

## Phase 3 退出条件

全部满足后方可进入 Phase 4（xiaozhi AI）：

- [ ] OV2640 采集正常，RGB565 240×240 图像在串口可见
- [ ] ESP-DL 人脸检测推理成功，串口输出 face cx/cy/inf_ms
- [ ] 检测到人脸时，状态机从 ACOUSTIC_TRACK 切换到 FACE_TRACK
- [ ] PID 控制器驱动 Pan+Tilt 双轴追踪人脸
- [ ] 人脸在 ±10px 死区内时舵机不动作（无微抖）
- [ ] 人脸移动时舵机限速 2°/帧平滑跟随
- [ ] 人脸丢失后 ~3秒退出 FACE_TRACK 回到 ACOUSTIC_TRACK
- [ ] FACE_TRACK 时 WS2812 显示蓝色指示
- [ ] 推理帧率 ≥ 5 FPS（理想 8-10 FPS）
- [ ] 双核同时运行：Core0 音频 + Core1 视觉，无看门狗复位
- [ ] 连续运行15分钟无崩溃
