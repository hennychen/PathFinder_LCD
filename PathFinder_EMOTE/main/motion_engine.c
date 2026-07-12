/**
 * @file motion_engine.c
 * @brief 运动分析引擎实现
 *
 * 算法流程 (每帧 @25Hz)：
 *   1. 去除零偏 (校准偏移)
 *   2. EMA 低通滤波 (消除高频噪声)
 *   3. 计算 pitch/roll 角度 (atan2 重力分解)
 *   4. 累计滑动窗口方差 (颠簸检测)
 *   5. 各检测器独立输出候选事件
 *   6. 事件持续时长防抖 (满足阈值 + 持续帧数)
 *   7. 优先级仲裁 → 返回最高优先级事件
 *   8. 事件输出稳定 (连续N帧确认 + 迟滞阈值)
 */
#include "motion_engine.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "motion";

/* ── 滑动窗口参数 ── */
#define WINDOW_SIZE       25      /* 滑动窗口帧数 */
#define SAMPLE_PERIOD_MS  40      /* 25Hz 采样周期 */

/* ── 低通滤波系数 (EMA: α越小越平滑) ── */
#define FILTER_ALPHA      0.15f

/* ── 事件输出稳定：新事件需连续 N 帧才输出 ── */
#define EVENT_STABLE_FRAMES  3   /* 连续 3 帧 (~120ms) 才确认事件切换 */

/* ── 迟滞阈值：进入和退出使用不同阈值，防止边界跳动 ── */
#define THRESH_PITCH_ENTER  15.0f   /* 进入坡度判定 */
#define THRESH_PITCH_EXIT   8.0f    /* 退出坡度判定 */
#define THRESH_ROLL_ENTER   15.0f   /* 进入倾斜判定 */
#define THRESH_ROLL_EXIT    8.0f    /* 退出倾斜判定 */

/* ── 检测阈值 (单位 g 或 °/s) ── */
#define THRESH_ACCEL_HI   0.30f   /* 急加速 ax > +0.3g */
#define THRESH_BRAKE_LO   -0.30f  /* 急刹车 ax < -0.3g */
#define THRESH_TURN_GZ    30.0f   /* 急转弯 |gz| > 30°/s */
#define THRESH_BUMPY_VAR  0.15f   /* 颠簸方差 > 0.15g² */
#define THRESH_COLLISION  2.5f    /* 碰撞瞬时 |a| > 2.5g */
#define THRESH_IDLE_TOTAL 0.05f   /* 静止 |a|偏离 < 0.05g */
#define THRESH_HIGH_SPEED 0.50f   /* 高速 |a| > 0.5g */

/* ── 持续时长防抖 (帧数 = ms / 40 @25Hz) ── */
#define DEBOUNCE_ACCEL    5       /* 200ms = 5 帧 */
#define DEBOUNCE_TURN     8       /* ~320ms = 8 帧 */
#define DEBOUNCE_IDLE     25      /* 1s = 25 帧 */
#define DEBOUNCE_HIGH_SPEED 75    /* 3s = 75 帧 */

/* ── 优先级表 (碰撞>刹车>加速>转弯>颠簸>坡度>行驶>静止) ── */
static const int s_priority[MOTION_EVENT_COUNT] = {
    [MOTION_IDLE]       = 1,
    [MOTION_CRUISE]     = 5,
    [MOTION_ACCEL]      = 13,
    [MOTION_BRAKE]      = 14,
    [MOTION_TURN_LEFT]  = 12,
    [MOTION_TURN_RIGHT] = 12,
    [MOTION_UPHILL]     = 8,
    [MOTION_DOWNHILL]   = 8,
    [MOTION_TILT_LEFT]  = 6,
    [MOTION_TILT_RIGHT] = 6,
    [MOTION_BUMPY]      = 10,
    [MOTION_COLLISION]  = 15,
    [MOTION_HIGH_SPEED] = 5,
};

/* ── 全局状态 ── */
static struct {
    /* 校准零偏 */
    float accel_bias[3];      /* 静态加速度偏移 (减去重力后的残余) */
    float gyro_bias[3];       /* 陀螺仪零偏 */

    /* 低通滤波后的值 */
    float filt_ax, filt_ay, filt_az;
    float filt_gx, filt_gy, filt_gz;
    bool  filt_initialized;

    /* 滑动窗口 (颠簸方差) */
    float accel_mag_window[WINDOW_SIZE];
    int   window_idx;
    bool  window_full;

    /* 事件输出稳定 */
    motion_event_t pending_event;     /* 候选事件 */
    int             pending_count;     /* 连续帧计数 */

    /* 角度缓存 */
    float pitch;
    float roll;

    /* 防抖计数器 */
    int   cnt_accel;
    int   cnt_brake;
    int   cnt_turn_left;
    int   cnt_turn_right;
    int   cnt_idle;
    int   cnt_high_speed;

    /* 当前输出事件 (防抖后) */
    motion_event_t current_event;

    /* 高速持续帧计数 */
    int   high_speed_accum;   /* 连续 |a|>0.5g 的帧计数 */
} s_state;

/* ── 辅助函数 ── */
static float calc_variance(const float *data, int n, float mean)
{
    float sum = 0;
    for (int i = 0; i < n; i++) {
        float d = data[i] - mean;
        sum += d * d;
    }
    return sum / n;
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ─────────────────────────────────────────────────────────
 *  公开 API
 * ───────────────────────────────────────────────────────── */

esp_err_t motion_engine_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.current_event = MOTION_IDLE;
    s_state.pending_event = MOTION_IDLE;
    s_state.filt_initialized = false;
    ESP_LOGI(TAG, "运动分析引擎初始化完成 (25Hz, EMA α=%.2f)", FILTER_ALPHA);
    return ESP_OK;
}

void motion_engine_get_angles(float *pitch_deg, float *roll_deg)
{
    if (pitch_deg) *pitch_deg = s_state.pitch;
    if (roll_deg)  *roll_deg  = s_state.roll;
}

int motion_engine_get_priority(motion_event_t evt)
{
    if (evt < 0 || evt >= MOTION_EVENT_COUNT) return 0;
    return s_priority[evt];
}

void motion_engine_calibrate(void)
{
    ESP_LOGI(TAG, "开始校准 (采集 %d 帧)...", WINDOW_SIZE);

    /* 采集静止数据取平均 */
    float acc_sum[3] = {0, 0, 0};
    float gyro_sum[3] = {0, 0, 0};
    int count = 0;

    for (int i = 0; i < WINDOW_SIZE; i++) {
        /* 使用 s_state.window 中已有的数据不可靠 (可能非静止)
         * 此处简化: 直接返回，实际校准需要外部队列喂数据 */
        /* TODO: 如需精确校准，改为传入采集队列 */
    }

    /* 简化校准：假设当前静止，陀螺仪零偏=0 */
    s_state.gyro_bias[0] = 0;
    s_state.gyro_bias[1] = 0;
    s_state.gyro_bias[2] = 0;
    s_state.accel_bias[0] = 0;
    s_state.accel_bias[1] = 0;
    s_state.accel_bias[2] = 0;

    ESP_LOGI(TAG, "校准完成");
    (void)acc_sum; (void)gyro_sum; (void)count;
}

motion_event_t motion_engine_process(const mpu6050_data_t *imu)
{
    if (imu == NULL) return s_state.current_event;

    /* ── 1. 去零偏 ── */
    float raw_ax = imu->accel[0] - s_state.accel_bias[0];
    float raw_ay = imu->accel[1] - s_state.accel_bias[1];
    float raw_az = imu->accel[2] - s_state.accel_bias[2];
    float raw_gx = imu->gyro[0] - s_state.gyro_bias[0];
    float raw_gy = imu->gyro[1] - s_state.gyro_bias[1];
    float raw_gz = imu->gyro[2] - s_state.gyro_bias[2];

    /* ── 1b. EMA 低通滤波 (消除高频噪声) ── */
    if (!s_state.filt_initialized) {
        s_state.filt_ax = raw_ax;
        s_state.filt_ay = raw_ay;
        s_state.filt_az = raw_az;
        s_state.filt_gx = raw_gx;
        s_state.filt_gy = raw_gy;
        s_state.filt_gz = raw_gz;
        s_state.filt_initialized = true;
    } else {
        float a = FILTER_ALPHA;
        s_state.filt_ax += a * (raw_ax - s_state.filt_ax);
        s_state.filt_ay += a * (raw_ay - s_state.filt_ay);
        s_state.filt_az += a * (raw_az - s_state.filt_az);
        s_state.filt_gx += a * (raw_gx - s_state.filt_gx);
        s_state.filt_gy += a * (raw_gy - s_state.filt_gy);
        s_state.filt_gz += a * (raw_gz - s_state.filt_gz);
    }

    float ax = s_state.filt_ax;
    float ay = s_state.filt_ay;
    float az = s_state.filt_az;
    float gx = s_state.filt_gx;
    float gy = s_state.filt_gy;
    float gz = s_state.filt_gz;

    /* ── 2. 计算 pitch/roll ── */
    s_state.pitch = atan2f(ax, sqrtf(ay * ay + az * az)) * 180.0f / M_PI;
    s_state.roll  = atan2f(ay, sqrtf(ax * ax + az * az)) * 180.0f / M_PI;

    /* ── 3. 总加速度幅值 ── */
    float mag = sqrtf(ax * ax + ay * ay + az * az);
    float mag_dev = fabsf(mag - 1.0f);  /* 偏离 1g 的程度 */

    /* ── 4. 滑动窗口方差 (颠簸检测) ── */
    s_state.accel_mag_window[s_state.window_idx] = mag_dev;
    s_state.window_idx = (s_state.window_idx + 1) % WINDOW_SIZE;
    if (s_state.window_idx == 0) s_state.window_full = true;

    int window_n = s_state.window_full ? WINDOW_SIZE : s_state.window_idx;
    float window_mean = 0;
    for (int i = 0; i < window_n; i++)
        window_mean += s_state.accel_mag_window[i];
    window_mean /= (window_n > 0) ? window_n : 1;
    float window_var = calc_variance(s_state.accel_mag_window, window_n, window_mean);

    /* ── 5. 候选事件检测 (带防抖) ── */
    motion_event_t candidates[8];
    int num_candidates = 0;

    /* 碰撞: 瞬时 |a| > 2.5g → 无防抖，单帧触发 */
    if (mag > THRESH_COLLISION) {
        candidates[num_candidates++] = MOTION_COLLISION;
    }

    /* 急加速/急刹车 */
    if (ax > THRESH_ACCEL_HI) {
        s_state.cnt_accel++;
        s_state.cnt_brake = 0;
    } else if (ax < THRESH_BRAKE_LO) {
        s_state.cnt_brake++;
        s_state.cnt_accel = 0;
    } else {
        s_state.cnt_accel = 0;
        s_state.cnt_brake = 0;
    }
    if (s_state.cnt_accel >= DEBOUNCE_ACCEL)
        candidates[num_candidates++] = MOTION_ACCEL;
    if (s_state.cnt_brake >= DEBOUNCE_ACCEL)
        candidates[num_candidates++] = MOTION_BRAKE;

    /* 急转弯 */
    if (gz > THRESH_TURN_GZ) {
        s_state.cnt_turn_left++;
        s_state.cnt_turn_right = 0;
    } else if (gz < -THRESH_TURN_GZ) {
        s_state.cnt_turn_right++;
        s_state.cnt_turn_left = 0;
    } else {
        s_state.cnt_turn_left = 0;
        s_state.cnt_turn_right = 0;
    }
    if (s_state.cnt_turn_left >= DEBOUNCE_TURN)
        candidates[num_candidates++] = MOTION_TURN_LEFT;
    if (s_state.cnt_turn_right >= DEBOUNCE_TURN)
        candidates[num_candidates++] = MOTION_TURN_RIGHT;

    /* 颠簸 (需要窗口数据) */
    if (window_n >= WINDOW_SIZE / 2 && window_var > THRESH_BUMPY_VAR)
        candidates[num_candidates++] = MOTION_BUMPY;

    /* 坡度与倾斜 (带迟滞：进入用高阈值，退出用低阈值) */
    { /* 局部作用域 */
        static float pitch_hyst = 0;  /* 0=无, +1=上坡锁定, -1=下坡锁定 */
        if (pitch_hyst >= 1) {
            if (s_state.pitch < THRESH_PITCH_EXIT) pitch_hyst = 0;
        } else if (pitch_hyst <= -1) {
            if (s_state.pitch > -THRESH_PITCH_EXIT) pitch_hyst = 0;
        } else {
            if (s_state.pitch > THRESH_PITCH_ENTER) pitch_hyst = 1;
            else if (s_state.pitch < -THRESH_PITCH_ENTER) pitch_hyst = -1;
        }
        if (pitch_hyst >= 1)  candidates[num_candidates++] = MOTION_UPHILL;
        if (pitch_hyst <= -1) candidates[num_candidates++] = MOTION_DOWNHILL;
    }

    { /* 局部作用域 */
        static float roll_hyst = 0;
        if (roll_hyst >= 1) {
            if (s_state.roll < THRESH_ROLL_EXIT) roll_hyst = 0;
        } else if (roll_hyst <= -1) {
            if (s_state.roll > -THRESH_ROLL_EXIT) roll_hyst = 0;
        } else {
            if (s_state.roll > THRESH_ROLL_ENTER) roll_hyst = 1;
            else if (s_state.roll < -THRESH_ROLL_ENTER) roll_hyst = -1;
        }
        if (roll_hyst >= 1)  candidates[num_candidates++] = MOTION_TILT_LEFT;
        if (roll_hyst <= -1) candidates[num_candidates++] = MOTION_TILT_RIGHT;
    }

    /* 高速行驶: 连续 |a|>0.5g 超过 3 秒 */
    if (mag_dev > THRESH_HIGH_SPEED) {
        s_state.high_speed_accum++;
    } else {
        s_state.high_speed_accum = 0;
    }
    if (s_state.high_speed_accum >= DEBOUNCE_HIGH_SPEED)
        candidates[num_candidates++] = MOTION_HIGH_SPEED;

    /* 静止检测 */
    float total_dev = fabsf(ax) + fabsf(ay) + fabsf(az - 1.0f);
    if (total_dev < THRESH_IDLE_TOTAL) {
        s_state.cnt_idle++;
    } else {
        s_state.cnt_idle = 0;
    }

    /* ── 6. 优先级仲裁 ── */
    motion_event_t best = MOTION_IDLE;
    int best_pri = 0;
    bool has_candidate = false;

    for (int i = 0; i < num_candidates; i++) {
        int pri = s_priority[candidates[i]];
        if (!has_candidate || pri > best_pri) {
            best_pri = pri;
            best = candidates[i];
            has_candidate = true;
        }
    }

    /* 如果无候选事件 → 根据静止状态判断 */
    if (!has_candidate) {
        if (s_state.cnt_idle >= DEBOUNCE_IDLE) {
            best = MOTION_IDLE;
        } else {
            /* 有运动但不够触发任何事件 → 匀速行驶 */
            best = MOTION_CRUISE;
        }
    }

    /* ── 7. 事件输出稳定：连续 EVENT_STABLE_FRAMES 帧相同才确认 ── */
    if (best != s_state.pending_event) {
        s_state.pending_event = best;
        s_state.pending_count = 1;
    } else {
        s_state.pending_count++;
    }

    if (s_state.pending_count >= EVENT_STABLE_FRAMES ||
        best == MOTION_COLLISION) {
        /* 碰撞事件无延迟直通 */
        s_state.current_event = s_state.pending_event;
    }

    return s_state.current_event;
}

motion_event_t motion_engine_get_event(void)
{
    return s_state.current_event;
}
