/**
 * @file tracking_coordinator.h
 * @brief 多模态云台追踪协调器
 *
 * 协调四种追踪模式：
 *   1. AUTO   — 声源定位角度驱动 Pan 舵机自动追踪
 *   2. FACE   — 摄像头肤色检测驱动 Pan+Tilt 双轴追踪
 *   3. MANUAL — AI 通过 MCP 工具手动控制 Pan/Tilt
 *   4. IDLE   — 空闲保持当前位置
 *
 * 声源→Pan 映射（正弦投影，只追踪前方 180°）：
 *   0°(前)  → Pan 90°(居中)
 *   90°(右) → Pan 180°(最右)
 *   270°(左)→ Pan 0°(最左)
 *   后方    → 忽略（无法区分前后）
 */

#ifndef TRACKING_COORDINATOR_H
#define TRACKING_COORDINATOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TRACK_MODE_IDLE = 0,     /* 空闲，保持当前位置 */
    TRACK_MODE_AUTO,         /* 自动追踪（声源驱动 Pan） */
    TRACK_MODE_FACE,         /* 人脸追踪（摄像头驱动 Pan+Tilt） */
    TRACK_MODE_MANUAL,       /* 手动控制（MCP / AI 指令） */
} track_mode_t;

/**
 * @brief 初始化追踪协调器（舵机居中，模式 = IDLE）。
 *        依赖 servo_init() 已完成。
 */
void tracking_init(void);

/**
 * @brief 启动后台追踪平滑任务（20Hz，栈 4096）。
 */
void tracking_start_task(void);

/**
 * @brief 切换追踪模式。
 */
void tracking_set_mode(track_mode_t mode);
track_mode_t tracking_get_mode(void);

/**
 * @brief 声源角度更新（由 board 转发 sound_localizer 结果）。
 *        仅在 AUTO 模式下驱动 Pan 舵机。
 *
 * @param angle 声源角度 0~360°，-1 表示无效
 */
void tracking_on_sound_angle(float angle);

/**
 * @brief 手动设置 Pan（切换到 MANUAL 模式）。
 */
void tracking_manual_set_pan(int angle);

/**
 * @brief 手动设置 Tilt（切换到 MANUAL 模式）。
 */
void tracking_manual_set_tilt(int angle);

/**
 * @brief 手动同时设置 Pan + Tilt。
 */
void tracking_manual_set_pan_tilt(int pan, int tilt);

/**
 * @brief 人脸追踪更新（由 face_tracker 模块调用）。
 *        仅在 FACE 模式下生效，将偏移增量叠加到目标角度。
 *
 * @param pan_delta  Pan 增量（正=右转）
 * @param tilt_delta Tilt 增量（正=上转）
 */
void tracking_on_face_update(int pan_delta, int tilt_delta);

/**
 * @brief 人脸丢失通知（由 face_tracker 模块调用）。
 *        将 Pan/Tilt 目标回到居中 (90, 90)。
 */
void tracking_face_lost(void);

/** 获取目标 Pan 角度 */
int tracking_get_pan(void);

/** 获取目标 Tilt 角度 */
int tracking_get_tilt(void);

#ifdef __cplusplus
}
#endif

#endif /* TRACKING_COORDINATOR_H */
