/**
 * @file face_tracker.h
 * @brief Face Tracker Pipeline — ESP32-S3 + ESP-DL 人脸追踪系统主头文件
 *
 * =========================================================================
 *  硬件平台: ESP32-S3-N16R8 (16MB Flash + 8MB Octal PSRAM)
 *  摄像头:   OV2640 DVP (QVGA 320x240)
 *  云台:     2× MG90S 舵机 (Pan 水平 + Tilt 垂直)
 *  AI 模型:  ESP-DL face_detection_front (MSRMNP_S8_V1, INT8 量化, ~187KB)
 *
 * =========================================================================
 *  流水线架构 (Pipeline Architecture):
 *
 *  ┌──────────────┐  frame_queue  ┌────────────────┐  control_queue  ┌──────────────┐
 *  │   cam_task   │ ────────────▶ │  detect_task   │ ──────────────▶ │ control_task │
 *  │   (CPU 0)    │  camera_fb_t* │   (CPU 1)      │  face_offset_t  │   (CPU 0)    │
 *  │              │               │                │                 │              │
 *  │ OV2640 采集  │               │ ESP-DL 神经网络│                 │ PID → LEDC   │
 *  └──────────────┘               └────────────────┘                 └──────────────┘
 *
 *  目标帧率: 14–18 FPS (采集 ~30ms + 推理 ~44ms 并行流水)
 * =========================================================================
 */

#ifndef FACE_TRACKER_H
#define FACE_TRACKER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ── ESP-IDF 系统头文件 ── */
#include "esp_log.h"
#include "esp_err.h"
#include "esp_camera.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 *  1. OV2640 摄像头引脚映射 (适配 PathFinder Tracker B板)
 * ========================================================================
 *  与 bread-compact-wifi-s3cam 引脚一致，已验证匹配 ESP32-S3-EYE 兼容布局。
 *  DVP 8 位并行接口，SCCB 使用独立 I2C port。
 */
#define CAMERA_PIN_D0        GPIO_NUM_11    /* Y2  — 像素数据 D0 */
#define CAMERA_PIN_D1        GPIO_NUM_9     /* Y3  — 像素数据 D1 */
#define CAMERA_PIN_D2        GPIO_NUM_8     /* Y4  — 像素数据 D2 */
#define CAMERA_PIN_D3        GPIO_NUM_10    /* Y5  — 像素数据 D3 */
#define CAMERA_PIN_D4        GPIO_NUM_12    /* Y6  — 像素数据 D4 */
#define CAMERA_PIN_D5        GPIO_NUM_18    /* Y7  — 像素数据 D5 */
#define CAMERA_PIN_D6        GPIO_NUM_17    /* Y8  — 像素数据 D6 */
#define CAMERA_PIN_D7        GPIO_NUM_16    /* Y9  — 像素数据 D7 */
#define CAMERA_PIN_XCLK      GPIO_NUM_15    /* 主时钟输出 (Master Clock) */
#define CAMERA_PIN_PCLK      GPIO_NUM_13    /* 像素时钟输入 (Pixel Clock) */
#define CAMERA_PIN_VSYNC     GPIO_NUM_6     /* 帧同步 (Vertical Sync) */
#define CAMERA_PIN_HREF      GPIO_NUM_7     /* 行有效 (Horizontal Reference) */
#define CAMERA_PIN_SIOD      GPIO_NUM_4     /* SCCB SDA (I2C 数据) */
#define CAMERA_PIN_SIOC      GPIO_NUM_5     /* SCCB SCL (I2C 时钟) */
#define CAMERA_PIN_PWDN      (-1)           /* 无电源控制引脚 */
#define CAMERA_PIN_RESET     (-1)           /* 硬件复位 NC */

#define CAMERA_XCLK_FREQ_HZ  16000000       /* 16 MHz 主时钟 */

/* ========================================================================
 *  2. 双轴舵机云台控制引脚 (Pan / Tilt)
 * ========================================================================
 *  MG90S 舵机，50Hz PWM 控制，通过 LEDC 外设驱动。
 *  Pan = 水平旋转 (0°=左, 90°=中, 180°=右)
 *  Tilt = 俯仰     (0°=下, 90°=中, 180°=上)
 */
#define SERVO_PAN_GPIO       GPIO_NUM_47    /* Pan  水平舵机信号线 */
#define SERVO_TILT_GPIO      GPIO_NUM_14    /* Tilt 垂直舵机信号线 */

/* ========================================================================
 *  3. PSRAM 与帧缓存配置
 * ========================================================================
 *  ESP32-S3-N16R8 内置 8MB Octal PSRAM，用于摄像头帧缓冲区。
 *  双帧缓存 (fb_count=2) 实现 ping-pong 采集，避免丢帧。
 */
#define ENABLE_PSRAM                1       /* 编译期 PSRAM 使能标志 */

#define CAMERA_FB_COUNT             2       /* 双帧缓存 ping-pong */
#define CAMERA_FB_LOCATION          CAMERA_FB_IN_PSRAM  /* 帧缓冲位于 PSRAM */

/* 图像帧参数 */
#define FRAME_WIDTH                 320     /* QVGA 宽 */
#define FRAME_HEIGHT                240     /* QVGA 高 */
#define FRAME_CENTER_X              160     /* 画面中心 X (320/2) */
#define FRAME_CENTER_Y              120     /* 画面中心 Y (240/2) */

/* ========================================================================
 *  4. FreeRTOS 队列配置
 * ========================================================================
 *  两个队列连接三个流水线阶段：
 *    g_frame_queue  : cam_task → detect_task (传递 camera_fb_t 指针)
 *    g_control_queue: detect_task → control_task (传递人脸偏移量)
 */
#define FRAME_QUEUE_LEN             2       /* 帧队列深度 (匹配双帧缓存) */
#define CONTROL_QUEUE_LEN           1       /* xQueueOverwrite 要求 == 1 (mailbox 模式) */

/* ========================================================================
 *  5. 任务配置 (Task Configuration)
 * ========================================================================
 *  流水线跨核分配：采集+控制在 Core 0，AI 推理独占 Core 1。
 */
#define CAM_TASK_STACK_SIZE         8192    /* 摄像头采集任务栈 */
#define CAM_TASK_PRIORITY           4       /* 采集优先级 (中) */
#define CAM_TASK_CORE               0       /* 采集固定在 Core 0 */

#define DETECT_TASK_STACK_SIZE      16384   /* 检测任务栈 (ESP-DL 需较大栈) */
#define DETECT_TASK_PRIORITY        3       /* 检测优先级 (低于采集) */
#define DETECT_TASK_CORE            1       /* 推理固定在 Core 1 */

#define CONTROL_TASK_STACK_SIZE     4096    /* 控制任务栈 */
#define CONTROL_TASK_PRIORITY       5       /* 控制优先级 (最高，保证响应) */
#define CONTROL_TASK_CORE           0       /* 控制固定在 Core 0 */

/* ========================================================================
 *  6. LEDC PWM 舵机参数
 * ========================================================================
 *  50Hz (20ms 周期)，10 位精度 (1024 步)。
 *  舵机脉宽 0.5ms–2.5ms 对应 0°–180°。
 */
#define SERVO_PWM_FREQ_HZ           50              /* 50 Hz 舵机标准频率 */
#define SERVO_LEDC_TIMER_RES        LEDC_TIMER_10_BIT  /* 10-bit 精度 */
#define SERVO_LEDC_DUTY_MAX         (1024)          /* 2^10 = 1024 */
#define SERVO_PERIOD_US             20000           /* 1/50Hz = 20ms */

#define SERVO_PULSE_MIN_US          500             /* 0°   → 0.5ms */
#define SERVO_PULSE_MAX_US          2500            /* 180° → 2.5ms */

#define SERVO_ANGLE_MIN             0               /* 最小角度 */
#define SERVO_ANGLE_MAX             180             /* 最大角度 */
#define SERVO_ANGLE_CENTER          90              /* 居中角度 */

/* 软件安全限位（防舵机堵转过热）*/
#define SERVO_PAN_SAFE_MIN          30
#define SERVO_PAN_SAFE_MAX          150
#define SERVO_TILT_SAFE_MIN         45
#define SERVO_TILT_SAFE_MAX         135

#define SERVO_PAN_CHANNEL           LEDC_CHANNEL_0
#define SERVO_TILT_CHANNEL          LEDC_CHANNEL_1
#define SERVO_LEDC_TIMER            LEDC_TIMER_0
#define SERVO_LEDC_MODE             LEDC_LOW_SPEED_MODE

/* ========================================================================
 *  7. PID 控制器参数
 * ========================================================================
 *  双轴独立 PID，输入为像素偏移量 (误差)，输出为角度增量。
 */
#define PID_OUTPUT_LIMIT            30.0f   /* PID 输出限幅 (°/帧)，防抖动 */
#define PID_INTEGRAL_LIMIT          100.0f  /* 积分项限幅，防积分饱和 */
#define PID_DEADBAND_PX             10      /* 死区 (像素)，消除微小抖动 */

/* ========================================================================
 *  8. 数据结构定义
 * ======================================================================== */

/**
 * @brief PID 控制器结构体
 *
 * 经典增量式 PID 控制器，用于 Pan/Tilt 双轴角度修正。
 * 输入误差单位为像素 (px)，输出单位为角度 (°)。
 */
typedef struct {
    float Kp;           /* 比例系数 */
    float Ki;           /* 积分系数 */
    float Kd;           /* 微分系数 */
    float integral;     /* 积分累加值 */
    float prev_error;   /* 上一帧误差 (用于微分) */
} PID_Controller;

/**
 * @brief 人脸偏移量消息 (detect_task → control_task)
 *
 * detected=false 时表示人脸丢失，control_task 将执行回中逻辑。
 */
typedef struct {
    bool   detected;    /* 是否检测到人脸 */
    float  err_x;       /* X 方向偏移 (px)，正=人脸在右侧，负=左侧 */
    float  err_y;       /* Y 方向偏移 (px)，正=人脸在下方，负=上方 */
    float  score;       /* 检测置信度 (0.0–1.0) */
    int    face_w;      /* 人脸框宽度 (px) */
    int    face_h;      /* 人脸框高度 (px) */
} face_offset_t;

/* 人脸丢失特殊标志值 */
#define FACE_LOST_SENTINEL  ((face_offset_t){ .detected = false, \
                                              .err_x = 0.0f,     \
                                              .err_y = 0.0f,     \
                                              .score = 0.0f,     \
                                              .face_w = 0,       \
                                              .face_h = 0 })

/* ========================================================================
 *  9. 全局队列句柄 (由 main 创建，各任务共享)
 * ======================================================================== */
extern QueueHandle_t g_frame_queue;     /* cam_task → detect_task */
extern QueueHandle_t g_control_queue;   /* detect_task → control_task */

/* ========================================================================
 *  10. 公共 API 声明
 * ======================================================================== */

/* ── camera_pipeline.c ── */

/**
 * @brief 初始化 OV2640 摄像头。
 *
 * 配置 camera_config_t: QVGA (320×240)，灰度/JPEG 像素格式，
 * 使用 PSRAM 存放帧缓冲 (psramFound() 检测)。
 * 初始化失败时打印错误日志并重启。
 */
void camera_init(void);

/**
 * @brief 摄像头采集任务 (CPU 0)。
 *
 * 死循环获取帧，将 camera_fb_t 指针通过 g_frame_queue 发送给 detect_task。
 * 帧使用完毕后由 detect_task 调用 esp_camera_fb_return() 归还。
 *
 * @param arg 未使用 (FreeRTOS 任务参数)
 */
void cam_task(void *arg);

/* ── face_detect_pipeline.cc (C++ 编译) ── */

/**
 * @brief ESP-DL 人脸检测任务 (CPU 1)。
 *
 * 加载 face_detection_front 模型，从 g_frame_queue 接收帧指针，
 * 执行 INT8 推理 + NMS 过滤，将人脸偏移量发送到 g_control_queue。
 *
 * @param arg 未使用
 */
void detect_task(void *arg);

/* ── servo_pid.c ── */

/**
 * @brief 初始化双轴舵机 LEDC PWM 驱动。
 *
 * 配置 Timer 0 @ 50Hz, 10-bit；Pan=CH0, Tilt=CH1。
 * 初始化后两轴居中 (90°)。
 */
void servo_pid_init(void);

/**
 * @brief PID 计算函数。
 *
 * @param pid    PID 控制器实例
 * @param setpoint  目标值 (恒为 0，即人脸居中)
 * @param measured  当前测量值 (像素偏移量)
 * @return float    PID 输出 (角度增量)
 */
float pid_compute(PID_Controller *pid, float setpoint, float measured);

/**
 * @brief 将舵机角度映射为 LEDC 占空比值。
 *
 * @param angle 目标角度 [0°, 180°]
 * @return uint32_t LEDC 占空比 [0, 1023]
 */
uint32_t angle_to_duty(float angle);

/**
 * @brief 控制任务 (CPU 0)。
 *
 * 从 g_control_queue 接收人脸偏移量，经 PID 计算后
 * 驱动 Pan/Tilt 舵机平滑追踪。无人脸时回中。
 *
 * @param arg 未使用
 */
void control_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* FACE_TRACKER_H */
