/**
 * @file servo_controller.h
 * @brief 双通道 PWM 舵机驱动（MG90S / ES9052 MD 兼容）
 *
 * 使用 ESP-IDF LEDC 外设：
 *   - Timer 0 @ 50Hz, 14-bit 分辨率（16384 步，每步 ≈1.22μs）
 *   - Pan  = LEDC_CHANNEL_0
 *   - Tilt = LEDC_CHANNEL_1
 *
 * 舵机脉宽范围：600μs（0°）~ 2400μs（180°），收窄留机械余量
 */

#ifndef SERVO_CONTROLLER_H
#define SERVO_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SERVO_ANGLE_MIN     0
#define SERVO_ANGLE_MAX     180
#define SERVO_ANGLE_CENTER  90

/* 软件安全角度限位（比机械 0-180 更窄）：
 * 防止命令到物理极限导致舵机堵转过热(糊味根因)。
 * 三次收窄：Tilt 发热严重，进一步限制到 ±45(tilt)。 */
#define SERVO_PAN_SAFE_MIN   30
#define SERVO_PAN_SAFE_MAX   150
#define SERVO_TILT_SAFE_MIN  45
#define SERVO_TILT_SAFE_MAX  135

/**
 * @brief 初始化舵机控制器（LEDC Timer + 双通道）。
 *        初始化后 Pan=Tilt=90°（居中）。
 *
 * @param pan_gpio   Pan 舵机 GPIO（水平旋转）
 * @param tilt_gpio  Tilt 舵机 GPIO（俯仰）
 */
void servo_init(int pan_gpio, int tilt_gpio);

/** 设置 Pan 角度，自动限幅到安全范围 [SERVO_PAN_SAFE_MIN, SERVO_PAN_SAFE_MAX] */
void servo_set_pan(int angle);

/** 设置 Tilt 角度，自动限幅到安全范围 [SERVO_TILT_SAFE_MIN, SERVO_TILT_SAFE_MAX] */
void servo_set_tilt(int angle);

/** 同时设置 Pan + Tilt */
void servo_set_pan_tilt(int pan, int tilt);

/** 获取当前 Pan 角度 */
int servo_get_pan(void);

/** 获取当前 Tilt 角度 */
int servo_get_tilt(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVO_CONTROLLER_H */
