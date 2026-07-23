/**
 * @file face_tracker.h
 * @brief 人脸检测与追踪模块（ESP-DL 神经网络 + PID 控制 + 流水线架构）
 *
 * =========================================================================
 *  流水线架构 (Pipeline Architecture):
 *
 *  ┌──────────────┐  frame_queue  ┌────────────────┐  tracking_coord  ┌──────────────┐
 *  │  cam_sub     │ ────────────▶ │  detect_task   │ ───────────────▶ │ servo (PWM)  │
 *  │  (内联)      │  camera_fb_t* │   (CPU 1)      │  PID → pan/tilt  │              │
 *  │              │               │                │  增量            │              │
 *  │ OV2640 采集  │               │ ESP-DL MSRMNP  │                 │ 通过 coordinator│
 *  └──────────────┘               └────────────────┘                 └──────────────┘
 *
 *  目标帧率: 14–18 FPS (采集 ~30ms + 推理 ~44ms 并行流水)
 * =========================================================================
 */

#ifndef FACE_TRACKER_H
#define FACE_TRACKER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 *  数据结构
 * ======================================================================== */

typedef struct {
    bool   detected;
    int    cx, cy;
    int    w, h;
    float  score;
} face_info_t;

/**
 * @brief PID 控制器结构体
 *
 * 经典 PID 控制器，输入为像素偏移量 (误差)，输出为角度增量。
 */
typedef struct {
    float Kp;           /* 比例系数 */
    float Ki;           /* 积分系数 */
    float Kd;           /* 微分系数 */
    float integral;     /* 积分累加值 */
    float prev_error;   /* 上一帧误差 (用于微分) */
} face_pid_t;

/* ========================================================================
 *  公共 API（保持与原有接口完全兼容）
 * ======================================================================== */

/**
 * @brief 启动人脸追踪（创建流水线任务，加载 ESP-DL 模型）。
 *        内部自动切换 tracking 模式为 FACE。
 */
void face_tracker_start(void);

/**
 * @brief 停止人脸追踪，切换回 AUTO 模式。
 */
void face_tracker_stop(void);

/**
 * @brief 是否正在运行。
 */
bool face_tracker_is_running(void);

/**
 * @brief 当前是否检测到人脸。
 */
bool face_tracker_detected(void);

/**
 * @brief 获取最近一次人脸信息。
 */
void face_tracker_get_info(face_info_t *info);

/* ========================================================================
 *  PID 控制器公共 API
 * ======================================================================== */

/**
 * @brief PID 计算。
 * @param pid       PID 控制器实例
 * @param setpoint  目标值 (恒为 0，即人脸居中)
 * @param measured  当前测量值 (像素偏移量)
 * @param deadband  死区 (像素)，误差小于此值时不输出
 * @return float    PID 输出 (角度增量)
 */
float face_pid_compute(face_pid_t *pid, float setpoint, float measured, int deadband);

#ifdef __cplusplus
}
#endif

#endif /* FACE_TRACKER_H */
