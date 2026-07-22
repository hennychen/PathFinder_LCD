/**
 * @file sound_localizer.c
 * @brief GCC-PHAT 四麦克风声源定位实现（移植自 AcousticEye calc_direction.c）
 *
 * 核心算法：
 *   1. 四通道音频去直流 + Hann 窗
 *   2. X/Y 两组对角麦克风分别做 GCC-PHAT 互相关
 *   3. 两轴到达时间差 → atan2 → 0~360° 角度
 *   4. 峰值/峰值比/响度/角度窗口多级过滤
 *
 * 与原版 AcousticEye 差异：
 *   - 采样率 48kHz → 24kHz，SSL_MAX_OFFSET 从 7 降到 3
 *   - 不含 I2S/ES7210 初始化（由 BoxAudioCodec 管理）
 *   - 数据通过 sound_localizer_feed() 喂入而非 i2s_read_mics()
 */

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_dsp.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_timer.h"

#include "sound_localizer.h"

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#define TAG "SoundLoc"

/* GCC-PHAT 频带限制 */
#define GCC_MIN_FREQ_HZ     500.0f
#define GCC_MAX_FREQ_HZ     3200.0f

/* ── 全局状态 ── */

/* 四通道环形缓冲区（浮点归一化） — 放入 PSRAM 释放 17KB 内部 SRAM */
EXT_RAM_BSS_ATTR static float s_mic_buf[SL_NUM_CHANNELS][SL_FFT_SIZE];
static volatile bool s_buf_ready = false;

/* 最新角度结果 */
static volatile float s_latest_angle = -1.0f;
static volatile int64_t s_last_valid_time_us = 0;

/* 自适应噪声门限追踪器 */
static float s_noise_floor = SL_NOISE_FLOOR_INIT;  /* 环境底噪RMS估计值 */
static float s_detect_threshold = 0.01f;           /* 自适应检测阈值 = floor × ratio */

/* 调试信息 */
static float s_dbg_activity = 0;
static float s_dbg_peak_x = 0;
static float s_dbg_peak_y = 0;
static float s_dbg_delay_x = 0;
static float s_dbg_delay_y = 0;
static float s_dbg_noise_floor = 0;
static float s_dbg_rms_ratio = 0;

/* ── FFT / GCC-PHAT 工作缓冲区 — 放入 PSRAM 释放内部 SRAM ── */
EXT_RAM_BSS_ATTR static float s_fft_window[SL_FFT_SIZE];
EXT_RAM_BSS_ATTR static float s_fft_in[SL_NUM_CHANNELS][SL_FFT_SIZE];
EXT_RAM_BSS_ATTR static float s_fft_a[SL_FFT_SIZE * 2];
EXT_RAM_BSS_ATTR static float s_fft_b[SL_FFT_SIZE * 2];
EXT_RAM_BSS_ATTR static float s_fft_out[SL_FFT_SIZE * 2];
EXT_RAM_BSS_ATTR static float s_xcorr[SL_FFT_SIZE];
EXT_RAM_BSS_ATTR static float s_xcorr_shifted[SL_FFT_SIZE];

/* ── 角度稳定滤波器 ── */
typedef struct {
    float angle_buf[SL_ANGLE_WINDOW];
    uint8_t count;
    uint8_t index;
    bool has_stable;
    float stable_angle;
} angle_filter_t;

static angle_filter_t s_angle_filter = {0};

/* ===================== 工具函数 ===================== */

static float clampf(float v, float min_v, float max_v)
{
    if (v < min_v) return min_v;
    if (v > max_v) return max_v;
    return v;
}

static float normalize_angle(float angle)
{
    while (angle < 0.0f) angle += 360.0f;
    while (angle >= 360.0f) angle -= 360.0f;
    return angle;
}

static float angle_diff_signed(float from, float to)
{
    float diff = to - from;
    while (diff > 180.0f) diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;
    return diff;
}

static float angle_diff_abs(float a, float b)
{
    return fabsf(angle_diff_signed(a, b));
}

static float angle_lerp_circular(float old_angle, float new_angle, float alpha)
{
    return normalize_angle(old_angle + alpha * angle_diff_signed(old_angle, new_angle));
}

static float circular_mean_angles(const float *angles, int count)
{
    float sx = 0, sy = 0;
    for (int i = 0; i < count; i++) {
        float rad = angles[i] * PI / 180.0f;
        sx += cosf(rad);
        sy += sinf(rad);
    }
    if (fabsf(sx) < 1e-6f && fabsf(sy) < 1e-6f) return -1.0f;
    return normalize_angle(atan2f(sy, sx) * 180.0f / PI);
}

static float max_spread_around_mean(const float *angles, int count, float mean)
{
    float spread = 0;
    for (int i = 0; i < count; i++) {
        float d = angle_diff_abs(angles[i], mean);
        if (d > spread) spread = d;
    }
    return spread;
}

static void shift_array(const float *input, float *output, int size)
{
    int half = size / 2;
    memcpy(output, &input[half], sizeof(float) * (size - half));
    memcpy(&output[size - half], input, sizeof(float) * half);
}

static void find_max_index(const float *data, int size, float *max_val, int *max_index)
{
    float max_v = data[0];
    int max_i = 0;
    for (int i = 1; i < size; i++) {
        if (data[i] > max_v) {
            max_v = data[i];
            max_i = i;
        }
    }
    *max_val = max_v;
    *max_index = max_i;
}

static float calc_mean(const float *x, int len)
{
    float sum = 0;
    for (int i = 0; i < len; i++) sum += x[i];
    return sum / (float)len;
}

static float calc_rms_dc(const float *x, int len)
{
    float mean = calc_mean(x, len);
    float sum = 0;
    for (int i = 0; i < len; i++) {
        float d = x[i] - mean;
        sum += d * d;
    }
    return sqrtf(sum / (float)len);
}

static float folded_freq(int index, int length)
{
    int k = (index > length / 2) ? (length - index) : index;
    return (float)k * SL_SAMPLE_RATE / (float)length;
}

static void preprocess_channel(const float *src, float *dst)
{
    float mean = calc_mean(src, SL_FFT_SIZE);
    for (int i = 0; i < SL_FFT_SIZE; i++) {
        dst[i] = (src[i] - mean) * s_fft_window[i];
    }
}

/* ===================== GCC-PHAT 核心算法 ===================== */

/**
 * @brief 计算两路麦克风信号的 GCC-PHAT 到达时间差
 *
 * @param a      A 端麦克风预处理数据
 * @param b      B 端麦克风预处理数据
 * @param length FFT 长度
 * @param offset 最大搜索偏移
 * @param peak_out     GCC 主峰值输出
 * @param peak_ratio_out 主/次峰比值输出
 * @return float B 相对 A 的延迟（采样点，可带小数）
 */
static float gcc_phat_delay(const float *a, const float *b,
                            int length, int offset,
                            float *peak_out, float *peak_ratio_out)
{
    int center = length / 2;
    int start = center - offset;
    int search_len = 2 * offset + 1;

    /* 组装复数 FFT 输入 */
    for (int i = 0; i < length; i++) {
        s_fft_a[2 * i]     = a[i];
        s_fft_a[2 * i + 1] = 0.0f;
        s_fft_b[2 * i]     = b[i];
        s_fft_b[2 * i + 1] = 0.0f;
    }

    dsps_fft2r_fc32(s_fft_a, length);
    dsps_fft2r_fc32(s_fft_b, length);
    dsps_bit_rev_fc32(s_fft_a, length);
    dsps_bit_rev_fc32(s_fft_b, length);

    /* PHAT 归一化互功率谱 */
    for (int i = 0; i < length; i++) {
        float ar = s_fft_a[2 * i], ai = s_fft_a[2 * i + 1];
        float br = s_fft_b[2 * i], bi = s_fft_b[2 * i + 1];
        float freq = folded_freq(i, length);

        if (freq < GCC_MIN_FREQ_HZ || freq > GCC_MAX_FREQ_HZ) {
            s_fft_out[2 * i]     = 0.0f;
            s_fft_out[2 * i + 1] = 0.0f;
            continue;
        }

        float real = br * ar + bi * ai;
        float imag = -br * ai + bi * ar;
        float mag = sqrtf(real * real + imag * imag);
        if (mag < 1e-10f) mag = 1e-10f;

        s_fft_out[2 * i]     = real / mag;
        s_fft_out[2 * i + 1] = imag / mag;
    }

    /* IFFT 得到互相关 */
    for (int i = 0; i < length; i++) {
        s_fft_out[2 * i + 1] = -s_fft_out[2 * i + 1];
    }
    dsps_fft2r_fc32(s_fft_out, length);
    dsps_bit_rev_fc32(s_fft_out, length);
    for (int i = 0; i < length; i++) {
        s_fft_out[2 * i] /= (float)length;
        s_xcorr[i] = s_fft_out[2 * i];
    }

    shift_array(s_xcorr, s_xcorr_shifted, length);

    /* 搜索主峰 */
    float peak = 0.0f;
    int peak_i = 0;
    find_max_index(s_xcorr_shifted + start, search_len, &peak, &peak_i);

    /* 搜索次峰（避开主峰附近） */
    float second_peak = 1e-6f;
    for (int i = 0; i < search_len; i++) {
        int distance = (i > peak_i) ? (i - peak_i) : (peak_i - i);
        if (distance <= 1) continue;
        float v = s_xcorr_shifted[start + i];
        if (v > second_peak) second_peak = v;
    }

    if (peak_out) *peak_out = peak;
    if (peak_ratio_out) *peak_ratio_out = peak / second_peak;

    float delay = (float)peak_i - (float)offset;

    /* 抛物线插值获得亚采样精度 */
    if (peak_i > 0 && peak_i < search_len - 1) {
        float y0 = s_xcorr_shifted[start + peak_i - 1];
        float y1 = s_xcorr_shifted[start + peak_i];
        float y2 = s_xcorr_shifted[start + peak_i + 1];
        float denom = y0 - 2.0f * y1 + y2;
        if (fabsf(denom) > 1e-10f) {
            delay += (y0 - y2) / (2.0f * denom);
        }
    }

    return delay;
}

/**
 * @brief 根据 X/Y 两轴延迟估算二维角度（atan2 双轴 → 0~360°）
 *
 * 同 AcousticEye calc_direction.c 的 estimate_angle_deg()
 */
static float estimate_angle(float delay_x, float delay_y)
{
    float raw_x = delay_x * SL_SOUND_VELOCITY / (SL_SAMPLE_RATE * SL_MIC_DISTANCE_M);
    float raw_y = delay_y * SL_SOUND_VELOCITY / (SL_SAMPLE_RATE * SL_MIC_DISTANCE_M);

    raw_x = clampf(raw_x, -1.0f, 1.0f);
    raw_y = clampf(raw_y, -1.0f, 1.0f);

    float x = SL_AXIS_X_SIGN * raw_x;
    float y = SL_AXIS_Y_SIGN * raw_y;

    if (fabsf(x) < 1e-5f && fabsf(y) < 1e-5f) return 0.0f;  /* center */

    return normalize_angle((atan2f(y, x) * 180.0f / PI) + SL_ANGLE_OFFSET_DEG);
}

/* ===================== 角度稳定滤波器 ===================== */

static void angle_filter_reset(void)
{
    memset(&s_angle_filter, 0, sizeof(s_angle_filter));
    s_angle_filter.stable_angle = -1.0f;
}

static float angle_filter_update(float raw_angle)
{
    if (raw_angle < 0.0f) return -1.0f;

    s_angle_filter.angle_buf[s_angle_filter.index] = normalize_angle(raw_angle);
    s_angle_filter.index = (s_angle_filter.index + 1) % SL_ANGLE_WINDOW;

    if (s_angle_filter.count < SL_ANGLE_WINDOW) s_angle_filter.count++;
    if (s_angle_filter.count < SL_MIN_VALID_COUNT) return -1.0f;

    float mean = circular_mean_angles(s_angle_filter.angle_buf, s_angle_filter.count);
    if (mean < 0.0f) return -1.0f;

    float spread = max_spread_around_mean(s_angle_filter.angle_buf,
                                           s_angle_filter.count, mean);
    if (spread > SL_MAX_SPREAD_DEG) return -1.0f;

    if (!s_angle_filter.has_stable) {
        s_angle_filter.has_stable = true;
        s_angle_filter.stable_angle = mean;
    } else {
        s_angle_filter.stable_angle = angle_lerp_circular(
            s_angle_filter.stable_angle, mean, SL_ANGLE_SMOOTH_ALPHA);
    }

    return normalize_angle(s_angle_filter.stable_angle);
}

/* ===================== 单帧声源定位计算 ===================== */

static float calc_angle_frame(void)
{
    static uint32_t warmup_cnt = 0;  /* 启动后前 5 帧热身，跳过初始化瞬态 */
    static uint32_t dbg_skip = 0;    /* 调试打印节流 */

    if (warmup_cnt < 5) {
        warmup_cnt++;
        if (warmup_cnt == 5) {
            ESP_LOGI(TAG, "Warmup done, starting normal detection");
        }
        return -1.0f;
    }

    /* 计算活动量、RMS、峰值 */
    float rms_sum = 0;
    float per_rms[SL_NUM_CHANNELS];
    for (int ch = 0; ch < SL_NUM_CHANNELS; ch++) {
        per_rms[ch] = calc_rms_dc(s_mic_buf[ch], SL_FFT_SIZE);
        rms_sum += per_rms[ch];
    }
    float avg_rms = rms_sum / SL_NUM_CHANNELS;
    if (avg_rms < 0.0001f) avg_rms = 0.0001f;
    float activity = log10f(avg_rms);
    if (isnan(activity) || isinf(activity)) activity = -10.0f;

    /* ── 自适应噪声门限更新（不对称：快速下降，缓慢上升）──
     * 问题：对称alpha导致说话间隙帧拉高floor，使后续声音无法触发。
     * 修复：rms低于floor时快速追踪（alpha=DOWN），高于floor时极慢上升（alpha=UP）。
     * 这样floor始终锁定在环境底噪水平，不被声音污染。 */
    float floor_alpha;
    if (avg_rms <= s_noise_floor) {
        /* rms ≤ floor：环境变安静了，快速下降 */
        floor_alpha = SL_NOISE_FLOOR_ALPHA_DOWN;
    } else if (avg_rms < s_noise_floor * SL_NOISE_FLOOR_UPDATE_MAX) {
        /* floor < rms < floor×1.5：略微高于floor，极慢上升 */
        floor_alpha = SL_NOISE_FLOOR_ALPHA_UP;
    } else {
        /* rms ≥ floor×1.5：有声音，不更新 */
        floor_alpha = 0.0f;
    }
    s_noise_floor = s_noise_floor * (1.0f - floor_alpha)
                  + avg_rms * floor_alpha;
    if (s_noise_floor < 0.0001f) s_noise_floor = 0.0001f;

    s_detect_threshold = s_noise_floor * SL_DETECT_RATIO;
    float rms_ratio = avg_rms / s_noise_floor;  /* 当前RMS相对底噪的倍数 */

    /* 峰值 */
    float raw_peak = 0;
    for (int ch = 0; ch < SL_NUM_CHANNELS; ch++) {
        for (int i = 0; i < SL_FFT_SIZE; i++) {
            float v = fabsf(s_mic_buf[ch][i]);
            if (v > raw_peak) raw_peak = v;
        }
    }

    /* 第一层过滤：固定阈值 + 自适应阈值双重判断 */
    bool l1_fail_activity = (activity < SL_MIN_ACTIVITY);
    bool l1_fail_adaptive = (avg_rms < s_detect_threshold);  /* RMS未显著高于底噪 */
    bool l1_fail_clip     = (raw_peak > SL_MAX_RAW_PEAK);

    if (l1_fail_activity || l1_fail_adaptive || l1_fail_clip) {
        angle_filter_reset();
        s_dbg_activity = activity;
        s_dbg_noise_floor = s_noise_floor;
        s_dbg_rms_ratio = rms_ratio;
        if (dbg_skip++ % 50 == 0) {
            ESP_LOGD(TAG, "L1-REJECT act=%.3f (thr=%.3f) rms=%.4f floor=%.4f ratio=%.1fx "
                     "det_thr=%.4f peak=%.4f %s%s",
                     activity, (float)SL_MIN_ACTIVITY, avg_rms,
                     s_noise_floor, rms_ratio, s_detect_threshold,
                     raw_peak,
                     l1_fail_activity ? "[ACT] " : "",
                     l1_fail_adaptive ? "[ADAPT]" : "");
        }
        return -1.0f;
    }

    /* 去直流 + 加窗 */
    for (int ch = 0; ch < SL_NUM_CHANNELS; ch++) {
        preprocess_channel(s_mic_buf[ch], s_fft_in[ch]);
    }

    /* X轴 GCC-PHAT（左右：CH0↔CH1） */
    float peak_x, ratio_x;
    float delay_x = gcc_phat_delay(s_fft_in[SL_MIC_X_A], s_fft_in[SL_MIC_X_B],
                                    SL_FFT_SIZE, SL_MAX_OFFSET, &peak_x, &ratio_x);

    /* Y轴 GCC-PHAT（前后：CH2↔CH3） */
    float peak_y, ratio_y;
    float delay_y = gcc_phat_delay(s_fft_in[SL_MIC_Y_A], s_fft_in[SL_MIC_Y_B],
                                    SL_FFT_SIZE, SL_MAX_OFFSET, &peak_y, &ratio_y);

    /* atan2 双轴角度估算 → 0~360° */
    float angle_raw = estimate_angle(delay_x, delay_y);

    /* 方向向量幅度（X²+Y²） */
    float dir_x = delay_x * SL_SOUND_VELOCITY / (SL_SAMPLE_RATE * SL_MIC_DISTANCE_M);
    float dir_y = delay_y * SL_SOUND_VELOCITY / (SL_SAMPLE_RATE * SL_MIC_DISTANCE_M);
    float dir_vec = sqrtf(dir_x * dir_x + dir_y * dir_y);

    /* 第二层过滤：GCC 可信度 */
    bool valid = true;
    valid &= (angle_raw >= 0.0f);
    valid &= (peak_x >= SL_MIN_PEAK_GCC && peak_y >= SL_MIN_PEAK_GCC);
    valid &= (ratio_x >= SL_MIN_PEAK_RATIO && ratio_y >= SL_MIN_PEAK_RATIO);
    valid &= (fabsf(delay_x) < ((float)SL_MAX_OFFSET - 0.25f));
    valid &= (fabsf(delay_y) < ((float)SL_MAX_OFFSET - 0.25f));
    valid &= (dir_vec >= SL_MIN_DIR_VECTOR);

    /* 保存调试信息 */
    s_dbg_activity = activity;
    s_dbg_peak_x = peak_x;
    s_dbg_peak_y = peak_y;
    s_dbg_delay_x = delay_x;
    s_dbg_delay_y = delay_y;
    s_dbg_noise_floor = s_noise_floor;
    s_dbg_rms_ratio = rms_ratio;

    if (!valid) {
        angle_filter_reset();
        if (dbg_skip++ % 50 == 0) {
            ESP_LOGD(TAG, "L2-REJECT ang=%.1f delay=[%.2f,%.2f] ratio=[%.2f,%.2f] "
                     "peak=[%.4f,%.4f] act=%.2f dir=%.3f flags: %s%s%s%s",
                     angle_raw, delay_x, delay_y, ratio_x, ratio_y,
                     peak_x, peak_y, activity, dir_vec,
                     (peak_x < SL_MIN_PEAK_GCC || peak_y < SL_MIN_PEAK_GCC) ? "WP " : "",
                     (ratio_x < SL_MIN_PEAK_RATIO || ratio_y < SL_MIN_PEAK_RATIO) ? "LC " : "",
                     (dir_vec < SL_MIN_DIR_VECTOR) ? "WA " : "",
                     (fabsf(delay_x) >= ((float)SL_MAX_OFFSET - 0.25f) ||
                      fabsf(delay_y) >= ((float)SL_MAX_OFFSET - 0.25f)) ? "CD" : "");
        }
        return -1.0f;
    }

    /* 第三层过滤：角度窗口稳定 */
    return angle_filter_update(angle_raw);
}

/* ===================== 公开 API ===================== */

void sound_localizer_init(void)
{
    dsps_fft2r_init_fc32(NULL, SL_FFT_SIZE);

    /* Hann 窗 */
    float tmp = 2.0f * PI / (float)SL_FFT_SIZE;
    for (int i = 0; i < SL_FFT_SIZE; i++) {
        s_fft_window[i] = 0.5f - 0.5f * cosf((float)i * tmp);
    }

    angle_filter_reset();
    s_latest_angle = -1.0f;
    s_buf_ready = false;
    s_noise_floor = SL_NOISE_FLOOR_INIT;
    s_detect_threshold = s_noise_floor * SL_DETECT_RATIO;
    ESP_LOGI(TAG, "Sound localizer initialized: %dHz, FFT=%d, max_offset=%d, noise_floor=%.4f",
             (int)SL_SAMPLE_RATE, SL_FFT_SIZE, SL_MAX_OFFSET, s_noise_floor);
}

void sound_localizer_feed(const int16_t *tdm_data, int frames)
{
    if (!tdm_data || frames <= 0) return;

    /* 将 int16 交错数据拆分为浮点通道数据，存入环形缓冲区 */
    static int s_write_idx = 0;

    for (int i = 0; i < frames; i++) {
        for (int ch = 0; ch < SL_NUM_CHANNELS; ch++) {
            s_mic_buf[ch][s_write_idx] = (float)tdm_data[i * SL_NUM_CHANNELS + ch] / 32768.0f;
        }
        s_write_idx++;
        if (s_write_idx >= SL_FFT_SIZE) {
            s_write_idx = 0;
            s_buf_ready = true;
        }
    }
}

float sound_localizer_get_angle(void)
{
    return s_latest_angle;
}

const char *sound_localizer_get_direction(float angle)
{
    /* 4-mic atan2 模式下角度定义（同 AcousticEye）：
     *   0°   = 正右 (X+)
     *   90°  = 正前 (Y+)
     *   180° = 正左 (X-)
     *   270° = 正后 (Y-)
     * 全 360° 均可达 */
    if (angle < 0.0f) return "unknown";
    if (angle < 22.5f || angle >= 337.5f) return "right";
    if (angle < 67.5f) return "front-right";
    if (angle < 112.5f) return "front";
    if (angle < 157.5f) return "front-left";
    if (angle < 202.5f) return "left";
    if (angle < 247.5f) return "back-left";
    if (angle < 292.5f) return "back";
    return "back-right";                        /* 292.5 - 337.5 */
}

bool sound_localizer_is_active(void)
{
    if (s_last_valid_time_us == 0) return false;
    int64_t now = esp_timer_get_time();
    return (now - s_last_valid_time_us) < 2000000; /* 2秒内有效 */
}

void sound_localizer_get_debug(float *activity_out, float *peak_x_out,
                                float *peak_y_out, float *delay_x_out,
                                float *delay_y_out)
{
    if (activity_out) *activity_out = s_dbg_activity;
    if (peak_x_out) *peak_x_out = s_dbg_peak_x;
    if (peak_y_out) *peak_y_out = s_dbg_peak_y;
    if (delay_x_out) *delay_x_out = s_dbg_delay_x;
    if (delay_y_out) *delay_y_out = s_dbg_delay_y;
}

/* ===================== 后台计算任务 ===================== */

static void sound_localizer_task(void *arg)
{
    ESP_LOGI(TAG, "Sound localizer task started");

    while (1) {
        if (s_buf_ready) {
            s_buf_ready = false;

            float angle = calc_angle_frame();

            if (angle >= 0.0f) {
                s_latest_angle = angle;
                s_last_valid_time_us = esp_timer_get_time();

                /* 追踪调试：每次有效角度都输出 */
                ESP_LOGI(TAG, "Angle: %.1f° (%s) act=%.2f rms_ratio=%.1fx peak=[%.4f,%.4f] delay=[%.2f,%.2f]",
                         angle, sound_localizer_get_direction(angle),
                         s_dbg_activity, s_dbg_rms_ratio,
                         s_dbg_peak_x, s_dbg_peak_y,
                         s_dbg_delay_x, s_dbg_delay_y);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20)); /* 50Hz 检查频率 */
    }
}

void sound_localizer_start_task(void)
{
    xTaskCreate(sound_localizer_task, "sound_loc", 8192, NULL, 5, NULL);
}
