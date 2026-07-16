/**
 * @file drv_servo.c
 * @brief MG90S dual-axis (Pan/Tilt) servo driver via LEDC PWM.
 *
 * Uses a single LEDC low-speed timer at 50 Hz (20 ms period) with 14-bit
 * duty resolution (16384 steps).  Two LEDC channels drive the Pan and Tilt
 * servos.  Servo control is achieved through pulse-width: 500 us = 0°,
 * 2500 us = 180°.
 */

#include "drv_servo.h"
#include "tracker_config.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "drv_servo";

/* ---- LEDC hardware mapping ---- */
#define SERVO_LEDC_MODE          LEDC_LOW_SPEED_MODE
#define SERVO_LEDC_TIMER         LEDC_TIMER_0
#define SERVO_LEDC_RESOLUTION    LEDC_TIMER_14_BIT
#define SERVO_DUTY_MAX           (1u << SERVO_LEDC_RESOLUTION)   /* 16384  */
#define SERVO_PERIOD_US          (1000000u / SERVO_FREQ_HZ)      /* 20000  */

#define SERVO_CENTER_ANGLE       ((SERVO_MIN_ANGLE + SERVO_MAX_ANGLE) / 2.0f)
/* Half-width of the frontal tracking cone (degrees around front). */
#define SERVO_TRACK_HALF_CONE    90.0f

/* Per-servo LEDC channel assignment: index by servo_id_t */
static const ledc_channel_t s_channel[2] = { LEDC_CHANNEL_0, LEDC_CHANNEL_1 };
static const int            s_gpio[2]     = { SERVO_PAN_GPIO, SERVO_TILT_GPIO };

/* Last commanded angle for each servo (used by get / hold-on-back). */
static float s_angle[2] = { SERVO_CENTER_ANGLE, SERVO_CENTER_ANGLE };
static bool  s_ready    = false;

/* ----------------------------------------------------------------- */
/*  Helpers                                                          */
/* ----------------------------------------------------------------- */

static inline float clamp_angle(float angle)
{
    if (angle < (float)SERVO_MIN_ANGLE) return (float)SERVO_MIN_ANGLE;
    if (angle > (float)SERVO_MAX_ANGLE) return (float)SERVO_MAX_ANGLE;
    return angle;
}

static inline uint32_t angle_to_duty(float angle)
{
    angle = clamp_angle(angle);

    /* Pulse width in microseconds for the requested angle */
    float span     = (float)(SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US);
    float pulse_us = (float)SERVO_MIN_PULSE_US
                   + (angle - (float)SERVO_MIN_ANGLE) * span / (float)SERVO_MAX_ANGLE;

    /* Convert microseconds to LEDC duty ticks */
    return (uint32_t)(pulse_us * (float)SERVO_DUTY_MAX / (float)SERVO_PERIOD_US);
}

static esp_err_t apply_duty(servo_id_t id, float angle)
{
    uint32_t duty = angle_to_duty(angle);

    esp_err_t ret = ledc_set_duty(SERVO_LEDC_MODE, s_channel[id], duty);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ledc_set_duty failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = ledc_update_duty(SERVO_LEDC_MODE, s_channel[id]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ledc_update_duty failed: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

/* ----------------------------------------------------------------- */
/*  Public API                                                       */
/* ----------------------------------------------------------------- */

esp_err_t drv_servo_init(void)
{
    if (s_ready) return ESP_OK;

    /* 1. LEDC timer @ 50 Hz, 14-bit resolution */
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = SERVO_LEDC_MODE,
        .duty_resolution = SERVO_LEDC_RESOLUTION,
        .timer_num       = SERVO_LEDC_TIMER,
        .freq_hz         = SERVO_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 2. Two LEDC channels, one per servo, both on the same timer.
       Centre both servos on init so they don't snap to an extreme. */
    for (int i = 0; i < 2; i++) {
        ledc_channel_config_t ch_cfg = {
            .gpio_num   = s_gpio[i],
            .speed_mode = SERVO_LEDC_MODE,
            .channel    = s_channel[i],
            .timer_sel  = SERVO_LEDC_TIMER,
            .duty       = angle_to_duty(SERVO_CENTER_ANGLE),
            .hpoint     = 0,
        };
        ret = ledc_channel_config(&ch_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ledc_channel_config[%d] failed: %s", i, esp_err_to_name(ret));
            return ret;
        }
        s_angle[i] = SERVO_CENTER_ANGLE;
    }

    s_ready = true;
    ESP_LOGI(TAG, "MG90S servos ready: Pan=GPIO%d, Tilt=GPIO%d @ %d Hz",
             SERVO_PAN_GPIO, SERVO_TILT_GPIO, SERVO_FREQ_HZ);
    return ESP_OK;
}

esp_err_t drv_servo_set_angle(servo_id_t id, float angle)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (id != SERVO_PAN && id != SERVO_TILT) {
        return ESP_ERR_INVALID_ARG;
    }

    angle = clamp_angle(angle);

    esp_err_t ret = apply_duty(id, angle);
    if (ret == ESP_OK) {
        s_angle[id] = angle;
    }
    return ret;
}

float drv_servo_get_angle(servo_id_t id)
{
    if (id != SERVO_PAN && id != SERVO_TILT) {
        return SERVO_CENTER_ANGLE;
    }
    return s_angle[id];
}

float drv_servo_angle_from_sound(float sound_angle)
{
    /* Reject out-of-range inputs; hold the last commanded Pan angle. */
    if (sound_angle < 0.0f || sound_angle >= 360.0f) {
        return s_angle[SERVO_PAN];
    }

    /* Signed relative angle: 0° = front, +ve = right, -ve = left. */
    float rel = sound_angle;
    if (rel > 180.0f) rel -= 360.0f;

    /* Rear hemisphere cannot be resolved by the front-facing mic array,
       so hold position instead of whipping around. */
    if (rel > SERVO_TRACK_HALF_CONE || rel < -SERVO_TRACK_HALF_CONE) {
        return s_angle[SERVO_PAN];
    }

    /* Map front(0)->90 (centre), right(+90)->0, left(-90)->180 */
    float pan = SERVO_CENTER_ANGLE - rel;
    return clamp_angle(pan);
}
