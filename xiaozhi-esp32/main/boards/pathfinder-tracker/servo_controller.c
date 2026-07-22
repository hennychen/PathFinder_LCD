/**
 * @file servo_controller.c
 * @brief LEDC PWM 双舵机驱动实现
 */

#include "servo_controller.h"

#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_err.h"

#define TAG "Servo"

/* ── LEDC 配置 ── */
#define SERVO_FREQ_HZ           50      /* 舵机标准频率 */
#define SERVO_TIMER_BIT         LEDC_TIMER_14_BIT
#define SERVO_DUTY_MAX          16384   /* 2^14 */
#define SERVO_PERIOD_US         20000   /* 1/50Hz = 20ms */

/* 舵机脉宽范围（微秒），兼容 MG90S 和 ES9052 MD。
 * 收窄自 500/2500 → 600/2400，避免驱动舵机超越机械限位导致
 * 电机堵转持续发热(糊味根因)。若仍发热可进一步收窄到 700/2300。 */
#define SERVO_MIN_US            600
#define SERVO_MAX_US            2400

#define PAN_CHANNEL             LEDC_CHANNEL_0
#define TILT_CHANNEL            LEDC_CHANNEL_1
#define SERVO_TIMER             LEDC_TIMER_0
#define SERVO_SPEED_MODE        LEDC_LOW_SPEED_MODE

static int s_pan_angle  = SERVO_ANGLE_CENTER;
static int s_tilt_angle = SERVO_ANGLE_CENTER;

/* Per-servo 安全角度限位（防堵转过热），索引与 servo_id 对应：0=Pan 1=Tilt */
static const int s_safe_min[2] = { SERVO_PAN_SAFE_MIN,  SERVO_TILT_SAFE_MIN };
static const int s_safe_max[2] = { SERVO_PAN_SAFE_MAX,  SERVO_TILT_SAFE_MAX };

/* 将角度限制到指定舵机的安全范围（比 0-180 更窄） */
static inline int clamp_angle_safe(int id, int angle)
{
    if (angle < s_safe_min[id]) angle = s_safe_min[id];
    if (angle > s_safe_max[id]) angle = s_safe_max[id];
    return angle;
}

/* ── 角度 → LEDC duty 转换 ── */
static inline uint32_t angle_to_duty(int angle)
{
    /* 调用方负责 clamp；此处仅做线性映射：0° → 600μs, 180° → 2400μs */
    int us = SERVO_MIN_US + (SERVO_MAX_US - SERVO_MIN_US) * angle / 180;
    return (uint32_t)us * SERVO_DUTY_MAX / SERVO_PERIOD_US;
}

void servo_init(int pan_gpio, int tilt_gpio)
{
    /* Timer 配置 */
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = SERVO_SPEED_MODE,
        .duty_resolution = SERVO_TIMER_BIT,
        .timer_num       = SERVO_TIMER,
        .freq_hz         = SERVO_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    /* Pan 通道 */
    ledc_channel_config_t pan_cfg = {
        .gpio_num   = pan_gpio,
        .speed_mode = SERVO_SPEED_MODE,
        .channel    = PAN_CHANNEL,
        .timer_sel  = SERVO_TIMER,
        .duty       = angle_to_duty(SERVO_ANGLE_CENTER),
        .hpoint     = 0,
        .flags      = { .output_invert = 0 },
    };
    ESP_ERROR_CHECK(ledc_channel_config(&pan_cfg));

    /* Tilt 通道 */
    ledc_channel_config_t tilt_cfg = {
        .gpio_num   = tilt_gpio,
        .speed_mode = SERVO_SPEED_MODE,
        .channel    = TILT_CHANNEL,
        .timer_sel  = SERVO_TIMER,
        .duty       = angle_to_duty(SERVO_ANGLE_CENTER),
        .hpoint     = 0,
        .flags      = { .output_invert = 0 },
    };
    ESP_ERROR_CHECK(ledc_channel_config(&tilt_cfg));

    s_pan_angle  = SERVO_ANGLE_CENTER;
    s_tilt_angle = SERVO_ANGLE_CENTER;

    ESP_LOGI(TAG, "Servo init: Pan=GPIO%d CH0, Tilt=GPIO%d CH1 @%dHz",
             pan_gpio, tilt_gpio, SERVO_FREQ_HZ);
}

void servo_set_pan(int angle)
{
    /* 收窄到 Pan 安全范围，避免到达机械极限堵转过热 */
    angle = clamp_angle_safe(0, angle);
    s_pan_angle = angle;
    ledc_set_duty(SERVO_SPEED_MODE, PAN_CHANNEL, angle_to_duty(angle));
    ledc_update_duty(SERVO_SPEED_MODE, PAN_CHANNEL);
}

void servo_set_tilt(int angle)
{
    /* 收窄到 Tilt 安全范围，避免到达机械极限堵转过热 */
    angle = clamp_angle_safe(1, angle);
    s_tilt_angle = angle;
    /* 硬件方向反转：逻辑 0=下/180=上，实际舵机需翻转。
     * 翻转后的角度同样需要在安全窗口内，这里直接用 clamp 后的 angle
     * 计算 180-angle，结果落在 [45,135] 内(与 Tilt 安全窗一致)。 */
    ledc_set_duty(SERVO_SPEED_MODE, TILT_CHANNEL, angle_to_duty(180 - angle));
    ledc_update_duty(SERVO_SPEED_MODE, TILT_CHANNEL);
}

void servo_set_pan_tilt(int pan, int tilt)
{
    servo_set_pan(pan);
    servo_set_tilt(tilt);
}

int servo_get_pan(void)  { return s_pan_angle; }
int servo_get_tilt(void) { return s_tilt_angle; }
