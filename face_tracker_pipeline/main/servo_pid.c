/**
 * @file servo_pid.c
 * @brief PID 控制器 + 双轴舵机驱动 + 控制任务 (Pipeline Stage 3, CPU 0)
 *
 * =========================================================================
 *  职责：
 *    1. servo_pid_init() — 初始化 LEDC PWM (50Hz, 10-bit) 双通道舵机
 *    2. pid_compute()    — 经典 PID 算法，输入像素误差，输出角度增量
 *    3. control_task()   — 从队列接收偏移量，PID 计算，驱动 Pan/Tilt 舵机
 *
 *  PID 控制策略：
 *    - 输入: 人脸中心相对于画面中心的像素偏移量 (err_x, err_y)
 *    - 目标: setpoint = 0 (人脸居中)
 *    - 输出: 角度修正量 (°)，累加到当前角度
 *    - 限幅: ±PID_OUTPUT_LIMIT (防过冲)，角度安全范围 [30°,150°]
 *
 *  LEDC PWM 映射：
 *    50Hz → 20ms 周期, 10-bit → 1024 步, 每步 ≈19.5μs
 *    0°   → 500μs  → duty ≈ 26
 *    90°  → 1500μs → duty ≈ 77
 *    180° → 2500μs → duty ≈ 128
 * =========================================================================
 */

#include "face_tracker.h"
#include <math.h>

#define TAG "ServoPID"

/* ======================================================================== */
/*                     内部辅助函数                                          */
/* ======================================================================== */

/**
 * @brief 将角度限制在安全范围内 (防止堵转过热)。
 */
static float clamp_angle(float angle, float min_angle, float max_angle)
{
    if (angle < min_angle) return min_angle;
    if (angle > max_angle) return max_angle;
    return angle;
}

/* ======================================================================== */
/*                     PID 控制算法                                          */
/* ======================================================================== */

float pid_compute(PID_Controller *pid, float setpoint, float measured)
{
    if (!pid) return 0.0f;

    /* 当前误差 = 目标值 - 测量值 */
    float error = setpoint - measured;

    /* ── 死区判断 ──
     * 误差小于 PID_DEADBAND_PX 像素时不输出，消除微小抖动。 */
    if (fabsf(error) < PID_DEADBAND_PX) {
        /* 在死区内：保持积分项不变，清零微分 */
        pid->prev_error = error;
        return 0.0f;
    }

    /* ── 积分项累加 ── */
    pid->integral += error;

    /* 积分限幅 (anti-windup)：防止积分饱和导致大超调 */
    if (pid->integral > PID_INTEGRAL_LIMIT)  pid->integral = PID_INTEGRAL_LIMIT;
    if (pid->integral < -PID_INTEGRAL_LIMIT) pid->integral = -PID_INTEGRAL_LIMIT;

    /* ── 微分项 ── */
    float derivative = error - pid->prev_error;
    pid->prev_error = error;

    /* ── PID 输出 ── */
    float output = (pid->Kp * error)
                 + (pid->Ki * pid->integral)
                 + (pid->Kd * derivative);

    /* ── 输出限幅 ──
     * 限制单帧角度增量，防舵机猛烈运动。 */
    if (output > PID_OUTPUT_LIMIT)  output = PID_OUTPUT_LIMIT;
    if (output < -PID_OUTPUT_LIMIT) output = -PID_OUTPUT_LIMIT;

    return output;
}

/* ======================================================================== */
/*                     LEDC PWM 舵机驱动                                     */
/* ======================================================================== */

uint32_t angle_to_duty(float angle)
{
    /* 角度限幅 [0°, 180°] */
    if (angle < SERVO_ANGLE_MIN) angle = SERVO_ANGLE_MIN;
    if (angle > SERVO_ANGLE_MAX) angle = SERVO_ANGLE_MAX;

    /* 角度 → 脉宽 (μs): 0.5ms ~ 2.5ms 线性映射 */
    float pulse_us = SERVO_PULSE_MIN_US
                   + (angle / (float)SERVO_ANGLE_MAX)
                   * (SERVO_PULSE_MAX_US - SERVO_PULSE_MIN_US);

    /* 脉宽 → LEDC 占空比 (10-bit):
     * duty = pulse_us / period_us * SERVO_LEDC_DUTY_MAX */
    uint32_t duty = (uint32_t)((pulse_us / (float)SERVO_PERIOD_US)
                               * (float)SERVO_LEDC_DUTY_MAX);

    return duty;
}

/**
 * @brief 设置 Pan 舵机角度 (直接 PWM 输出)。
 */
static void servo_write_pan(float angle)
{
    angle = clamp_angle(angle, SERVO_PAN_SAFE_MIN, SERVO_PAN_SAFE_MAX);
    uint32_t duty = angle_to_duty(angle);
    ledc_set_duty(SERVO_LEDC_MODE, SERVO_PAN_CHANNEL, duty);
    ledc_update_duty(SERVO_LEDC_MODE, SERVO_PAN_CHANNEL);
}

/**
 * @brief 设置 Tilt 舵机角度 (直接 PWM 输出)。
 *        硬件方向反转：逻辑角度高=上，但舵机实际需要 180-angle。
 */
static void servo_write_tilt(float angle)
{
    angle = clamp_angle(angle, SERVO_TILT_SAFE_MIN, SERVO_TILT_SAFE_MAX);
    /* 硬件方向校正：Tilt 舵机物理方向与逻辑相反 */
    uint32_t duty = angle_to_duty(SERVO_ANGLE_MAX - angle);
    ledc_set_duty(SERVO_LEDC_MODE, SERVO_TILT_CHANNEL, duty);
    ledc_update_duty(SERVO_LEDC_MODE, SERVO_TILT_CHANNEL);
}

/* ======================================================================== */
/*                     servo_pid_init                                        */
/* ======================================================================== */

void servo_pid_init(void)
{
    ESP_LOGI(TAG, "Initializing dual-axis servo PWM (50Hz, 10-bit)...");

    /* ── 1. 配置 LEDC Timer ──
     * 50Hz 频率, 10-bit 精度 (1024 步) */
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = SERVO_LEDC_MODE,
        .duty_resolution = SERVO_LEDC_TIMER_RES,   /* LEDC_TIMER_10_BIT */
        .timer_num       = SERVO_LEDC_TIMER,       /* LEDC_TIMER_0 */
        .freq_hz         = SERVO_PWM_FREQ_HZ,      /* 50 Hz */
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(err));
        return;
    }

    /* ── 2. 配置 Pan 通道 (CH0, GPIO47) ── */
    ledc_channel_config_t pan_cfg = {
        .gpio_num   = SERVO_PAN_GPIO,
        .speed_mode = SERVO_LEDC_MODE,
        .channel    = SERVO_PAN_CHANNEL,
        .timer_sel  = SERVO_LEDC_TIMER,
        .duty       = angle_to_duty(SERVO_ANGLE_CENTER),  /* 初始居中 90° */
        .hpoint     = 0,
        .flags      = { .output_invert = 0 },
    };
    err = ledc_channel_config(&pan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Pan channel config failed: %s", esp_err_to_name(err));
        return;
    }

    /* ── 3. 配置 Tilt 通道 (CH1, GPIO14) ── */
    ledc_channel_config_t tilt_cfg = {
        .gpio_num   = SERVO_TILT_GPIO,
        .speed_mode = SERVO_LEDC_MODE,
        .channel    = SERVO_TILT_CHANNEL,
        .timer_sel  = SERVO_LEDC_TIMER,
        .duty       = angle_to_duty(SERVO_ANGLE_MAX - SERVO_ANGLE_CENTER),  /* 反向后居中 */
        .hpoint     = 0,
        .flags      = { .output_invert = 0 },
    };
    err = ledc_channel_config(&tilt_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Tilt channel config failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Servos ready: Pan=GPIO%d CH0, Tilt=GPIO%d CH1 @ %dHz %d-bit",
             SERVO_PAN_GPIO, SERVO_TILT_GPIO, SERVO_PWM_FREQ_HZ, 10);
    ESP_LOGI(TAG, "  Safe range: Pan [%d°,%d°] Tilt [%d°,%d°]",
             SERVO_PAN_SAFE_MIN, SERVO_PAN_SAFE_MAX,
             SERVO_TILT_SAFE_MIN, SERVO_TILT_SAFE_MAX);
}

/* ======================================================================== */
/*                     control_task                                          */
/* ======================================================================== */

void control_task(void *arg)
{
    ESP_LOGI(TAG, "Control task started on Core %d", xPortGetCoreID());

    /* ── PID 控制器初始化 ──
     * 双轴独立 PID：
     *   Pan:  Kp=0.20 Ki=0.005 Kd=0.08 (水平响应较快)
     *   Tilt: Kp=0.15 Ki=0.003 Kd=0.06 (垂直稍慢，防上下抖动)
     *
     * 调参说明：
     *   Kp — 比例增益，决定追踪响应速度 (过大→振荡, 过小→迟钝)
     *   Ki — 积分增益，消除稳态误差 (过大→超调, 过小→残留偏差)
     *   Kd — 微分增益，抑制超调和振荡 (过大→噪声敏感) */
    PID_Controller pid_pan  = { .Kp = 0.20f, .Ki = 0.005f, .Kd = 0.08f,
                                .integral = 0.0f, .prev_error = 0.0f };
    PID_Controller pid_tilt = { .Kp = 0.15f, .Ki = 0.003f, .Kd = 0.06f,
                                .integral = 0.0f, .prev_error = 0.0f };

    /* 当前舵机角度 (从居中开始) */
    float current_pan  = (float)SERVO_ANGLE_CENTER;
    float current_tilt = (float)SERVO_ANGLE_CENTER;

    /* 人脸丢失计时器 */
    int lost_frames = 0;
    const int LOST_THRESHOLD = 10;  /* 连续 10 帧无人脸 → 回中 */

    /* 初始输出居中 PWM */
    servo_write_pan(current_pan);
    servo_write_tilt(current_tilt);

    face_offset_t offset;

    while (true) {
        /* ── 阻塞等待 detect_task 发来的偏移量 ──
         * g_control_queue 容量 = CONTROL_QUEUE_LEN，
         * detect_task 使用 xQueueOverwrite 写入，保证最新数据。 */
        if (xQueueReceive(g_control_queue, &offset, pdMS_TO_TICKS(500)) != pdTRUE) {
            /* 超时：detect_task 可能卡住，维持当前角度 */
            continue;
        }

        if (offset.detected) {
            /* ── 检测到人脸：PID 追踪 ── */
            lost_frames = 0;

            /* PID 计算：
             *   setpoint = 0 (目标：人脸居中)
             *   measured = err_x / err_y (当前偏移)
             *   输出 = 角度修正增量 */
            float pan_delta  = pid_compute(&pid_pan,  0.0f, offset.err_x);
            float tilt_delta = pid_compute(&pid_tilt, 0.0f, offset.err_y);

            /* 累加角度修正
             * Pan:  err_x > 0 (人脸在右) → pan_delta > 0 → Pan 增大 (右转)
             * Tilt: err_y > 0 (人脸在下) → tilt_delta > 0 → Tilt 增大 (下转)
             *        但硬件 Tilt 反向，需取负 */
            current_pan  += pan_delta;
            current_tilt -= tilt_delta;   /* Tilt 反向 */

            /* 限制角度到安全范围 */
            current_pan  = clamp_angle(current_pan,
                                       SERVO_PAN_SAFE_MIN, SERVO_PAN_SAFE_MAX);
            current_tilt = clamp_angle(current_tilt,
                                       SERVO_TILT_SAFE_MIN, SERVO_TILT_SAFE_MAX);

            /* 输出 PWM */
            servo_write_pan(current_pan);
            servo_write_tilt(current_tilt);

            ESP_LOGD(TAG, "PID: err=(%.1f,%.1f) delta=(%.2f,%.2f) angle=(%.1f,%.1f)",
                     offset.err_x, offset.err_y, pan_delta, tilt_delta,
                     current_pan, current_tilt);
        } else {
            /* ── 无人脸：等待后回中 ── */
            lost_frames++;
            if (lost_frames >= LOST_THRESHOLD) {
                /* 重置 PID 积分项 */
                pid_pan.integral  = 0.0f;
                pid_tilt.integral = 0.0f;

                /* 缓慢回中 (每帧移动 2°，约 1.5 秒回到中心) */
                float pan_step  = (SERVO_ANGLE_CENTER - current_pan) * 0.05f;
                float tilt_step = (SERVO_ANGLE_CENTER - current_tilt) * 0.05f;

                if (fabsf(pan_step)  < 0.5f) current_pan  = SERVO_ANGLE_CENTER;
                else                     current_pan  += pan_step;

                if (fabsf(tilt_step) < 0.5f) current_tilt = SERVO_ANGLE_CENTER;
                else                     current_tilt += tilt_step;

                servo_write_pan(current_pan);
                servo_write_tilt(current_tilt);

                ESP_LOGD(TAG, "Returning to center: pan=%.1f tilt=%.1f",
                         current_pan, current_tilt);
            }
        }

        /* 控制周期 20ms (50Hz)，匹配舵机响应频率 */
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    vTaskDelete(NULL);
}
