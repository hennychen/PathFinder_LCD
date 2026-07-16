/**
 * @file sound_localizer.c
 * @brief GCC-PHAT four-microphone sound source localisation.
 *
 * Migrated from the AcousticEye project's calc_direction.c.  The core
 * algorithm is unchanged: cross-correlate opposite microphone pairs in
 * the frequency domain (GCC-PHAT), convert the time-delay estimates to
 * an angle with atan2, and reject frames that are too quiet or ambiguous.
 */

#include <math.h>
#include <string.h>
#include "esp_dsp.h"
#include "sound_localizer.h"

#ifndef PI
#define PI 3.14159265358979323846f
#endif

/* ===================== Configuration ===================== */

#define SSL_FFT_SIZE            256         /* matches ES7210_SAMPLE_SIZE       */
#define SSL_CHANNELS            4
#define SSL_SOUND_VELOCITY      343.0f      /* m/s                               */

/*
 * Microphone geometry: opposite-pair spacing in metres.
 * max_lag is the maximum sample offset to search in cross-correlation,
 * derived from the physical round-trip delay between opposite mics.
 */
#define SSL_MIC_DISTANCE_M      0.040f

/* GCC-PHAT frequency band limits */
#define GCC_MIN_FREQ_HZ         500.0f
#define GCC_MAX_FREQ_HZ         3200.0f

/* Validity thresholds */
#define MIN_RAW_RMS_LEVEL       0.0060f
#define MAX_RAW_PEAK_LEVEL      0.98f
#define MIN_PEAK_VALUE_GCC      0.0025f
#define MIN_PEAK_RATIO_GCC      1.05f
#define MIN_DIRECTION_VECTOR    0.03f
#define PEAK_RATIO_GUARD_BINS   1

/* Axis signs (adjust for physical mounting) */
#define SSL_AXIS_X_SIGN         1.0f
#define SSL_AXIS_Y_SIGN         1.0f
#define SSL_ANGLE_OFFSET_DEG    0.0f

/* Microphone channel mapping (opposite pairs for X and Y axes) */
#define MIC_X_A                 0           /* CH0 — right  */
#define MIC_X_B                 1           /* CH1 — left   */
#define MIC_Y_A                 2           /* CH2 — top    */
#define MIC_Y_B                 3           /* CH3 — bottom */

/* ===================== State ===================== */

static float s_sample_rate = 48000.0f;
static int   s_max_lag     = 5;
static float s_threshold   = -2.20f;         /* default activity threshold       */

/* FFT / GCC-PHAT work buffers (static to avoid stack overflow) */
static float s_fft_window[SSL_FFT_SIZE];
static float s_fft_in[SSL_CHANNELS][SSL_FFT_SIZE];
static float s_fft_a[SSL_FFT_SIZE * 2];      /* complex FFT buffer A             */
static float s_fft_b[SSL_FFT_SIZE * 2];      /* complex FFT buffer B             */
static float s_xcorr_buf[SSL_FFT_SIZE * 2];  /* cross-power spectrum / IFFT      */
static float s_xcorr[SSL_FFT_SIZE];          /* real cross-correlation           */
static float s_xcorr_shifted[SSL_FFT_SIZE];  /* fftshifted xcorr                 */

/* ===================== Utilities ===================== */

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float normalize_angle_deg(float angle)
{
    while (angle < 0.0f)   angle += 360.0f;
    while (angle >= 360.0f) angle -= 360.0f;
    return angle;
}

static float calc_mean(const float *x, int len)
{
    float sum = 0.0f;
    for (int i = 0; i < len; i++) sum += x[i];
    return sum / (float)len;
}

static float calc_rms_dc_removed(const float *x, int len)
{
    float mean = calc_mean(x, len);
    float sum  = 0.0f;
    for (int i = 0; i < len; i++) {
        float d = x[i] - mean;
        sum += d * d;
    }
    return sqrtf(sum / (float)len);
}

static void shift_array(const float *in, float *out, int size)
{
    int half = size / 2;
    memcpy(out, &in[half], sizeof(float) * (size - half));
    memcpy(&out[size - half], in, sizeof(float) * half);
}

static void find_max_index(const float *data, int size, float *max_val, int *max_idx)
{
    float mv = data[0];
    int   mi = 0;
    for (int i = 1; i < size; i++) {
        if (data[i] > mv) { mv = data[i]; mi = i; }
    }
    *max_val = mv;
    *max_idx = mi;
}

static float folded_frequency_hz(int index, int length)
{
    int k = (index > length / 2) ? (length - index) : index;
    return (float)k * s_sample_rate / (float)length;
}

/* ===================== Preprocessing ===================== */

static void preprocess_channel(const float *src, float *dst)
{
    float mean = calc_mean(src, SSL_FFT_SIZE);
    for (int i = 0; i < SSL_FFT_SIZE; i++) {
        dst[i] = (src[i] - mean) * s_fft_window[i];
    }
}

/* ===================== GCC-PHAT ===================== */

/**
 * @brief Compute GCC-PHAT time-delay between two preprocessed channels.
 *
 * @return Delay in samples (B relative to A), can be fractional.
 */
static float gcc_phat_delay(const float *a, const float *b,
                            float *peak_out, float *ratio_out)
{
    int length = SSL_FFT_SIZE;
    int offset = s_max_lag;
    int center_i = length / 2;
    int start_i  = center_i - offset;
    int search_len = 2 * offset + 1;

    /* Pack into complex FFT input: real = sample, imag = 0 */
    for (int i = 0; i < length; i++) {
        s_fft_a[2 * i]     = a[i];  s_fft_a[2 * i + 1] = 0.0f;
        s_fft_b[2 * i]     = b[i];  s_fft_b[2 * i + 1] = 0.0f;
    }

    dsps_fft2r_fc32(s_fft_a, length);
    dsps_fft2r_fc32(s_fft_b, length);
    dsps_bit_rev_fc32(s_fft_a, length);
    dsps_bit_rev_fc32(s_fft_b, length);

    /* Cross-power spectrum with PHAT normalisation + band-pass mask */
    for (int i = 0; i < length; i++) {
        float ar = s_fft_a[2 * i], ai = s_fft_a[2 * i + 1];
        float br = s_fft_b[2 * i], bi = s_fft_b[2 * i + 1];
        float freq = folded_frequency_hz(i, length);

        if (freq < GCC_MIN_FREQ_HZ || freq > GCC_MAX_FREQ_HZ) {
            s_xcorr_buf[2 * i]     = 0.0f;
            s_xcorr_buf[2 * i + 1] = 0.0f;
            continue;
        }

        /* Cross-spectrum: B * conj(A) */
        float real = br * ar + bi * ai;
        float imag = -br * ai + bi * ar;
        float mag  = sqrtf(real * real + imag * imag);
        if (mag < 1e-10f) mag = 1e-10f;

        s_xcorr_buf[2 * i]     = real / mag;
        s_xcorr_buf[2 * i + 1] = imag / mag;
    }

    /* IFFT via conjugate + forward FFT + scale */
    for (int i = 0; i < length; i++) {
        s_xcorr_buf[2 * i + 1] = -s_xcorr_buf[2 * i + 1];
    }
    dsps_fft2r_fc32(s_xcorr_buf, length);
    dsps_bit_rev_fc32(s_xcorr_buf, length);
    for (int i = 0; i < length; i++) {
        s_xcorr[i] = s_xcorr_buf[2 * i] / (float)length;
    }

    shift_array(s_xcorr, s_xcorr_shifted, length);

    /* Find main peak in the physically valid search window */
    float peak = 0.0f;
    int   peak_i = 0;
    find_max_index(s_xcorr_shifted + start_i, search_len, &peak, &peak_i);

    /* Find second peak (excluding main-peak neighbourhood) for ratio */
    float second_peak = 1e-6f;
    for (int i = 0; i < search_len; i++) {
        int dist = abs(i - peak_i);
        if (dist <= PEAK_RATIO_GUARD_BINS) continue;
        float v = s_xcorr_shifted[start_i + i];
        if (v > second_peak) second_peak = v;
    }

    if (peak_out)  *peak_out  = peak;
    if (ratio_out) *ratio_out = peak / second_peak;

    float delay = (float)peak_i - (float)offset;

    /* Parabolic interpolation for sub-sample accuracy */
    if (peak_i > 0 && peak_i < search_len - 1) {
        float y0 = s_xcorr_shifted[start_i + peak_i - 1];
        float y1 = s_xcorr_shifted[start_i + peak_i];
        float y2 = s_xcorr_shifted[start_i + peak_i + 1];
        float denom = y0 - 2.0f * y1 + y2;
        if (fabsf(denom) > 1e-10f) {
            delay += (y0 - y2) / (2.0f * denom);
        }
    }

    return delay;
}

/**
 * @brief Convert X/Y sample delays to an angle in degrees.
 * @return [0, 360) or -1 if direction vector is negligible.
 */
static float estimate_angle(float delay_x, float delay_y)
{
    float rx = delay_x * SSL_SOUND_VELOCITY / (s_sample_rate * SSL_MIC_DISTANCE_M);
    float ry = delay_y * SSL_SOUND_VELOCITY / (s_sample_rate * SSL_MIC_DISTANCE_M);
    rx = clampf(rx, -1.0f, 1.0f);
    ry = clampf(ry, -1.0f, 1.0f);

    float x = SSL_AXIS_X_SIGN * rx;
    float y = SSL_AXIS_Y_SIGN * ry;

    if (fabsf(x) < 1e-5f && fabsf(y) < 1e-5f) return -1.0f;

    return normalize_angle_deg(atan2f(y, x) * 180.0f / PI + SSL_ANGLE_OFFSET_DEG);
}

/* ===================== Public API ===================== */

void sound_localizer_init(float sample_rate)
{
    s_sample_rate = sample_rate;

    /* Pre-compute max lag from physical geometry */
    s_max_lag = (int)(SSL_MIC_DISTANCE_M * sample_rate / SSL_SOUND_VELOCITY);
    if (s_max_lag < 1) s_max_lag = 1;

    /* Initialise esp-dsp FFT */
    dsps_fft2r_init_fc32(NULL, SSL_FFT_SIZE);

    /* Generate Hann window */
    float tmp = 2.0f * PI / (float)SSL_FFT_SIZE;
    for (int i = 0; i < SSL_FFT_SIZE; i++) {
        s_fft_window[i] = 0.5f - 0.5f * cosf((float)i * tmp);
    }
}

void sound_localizer_set_threshold(float threshold)
{
    s_threshold = threshold;
}

localization_result_t sound_localizer_compute(float mic_data[4][256])
{
    localization_result_t result = { .angle = -1.0f, .confidence = 0.0f, .valid = false };

    /* ---- Activity check ---- */
    float rms_sum = 0.0f;
    for (int ch = 0; ch < SSL_CHANNELS; ch++) {
        rms_sum += calc_rms_dc_removed(mic_data[ch], SSL_FFT_SIZE);
    }
    float avg_rms = rms_sum * 0.25f;
    if (avg_rms < 0.0001f) avg_rms = 0.0001f;
    float activity = log10f(avg_rms);
    if (isnan(activity) || isinf(activity)) activity = -10.0f;

    /* Peak check (clipping detection) */
    float peak = 0.0f;
    for (int ch = 0; ch < SSL_CHANNELS; ch++) {
        for (int i = 0; i < SSL_FFT_SIZE; i++) {
            float v = fabsf(mic_data[ch][i]);
            if (v > peak) peak = v;
        }
    }

    if (activity < s_threshold || avg_rms < MIN_RAW_RMS_LEVEL || peak > MAX_RAW_PEAK_LEVEL) {
        return result;
    }

    /* ---- Preprocess: DC removal + Hann window ---- */
    preprocess_channel(mic_data[0], s_fft_in[0]);
    preprocess_channel(mic_data[1], s_fft_in[1]);
    preprocess_channel(mic_data[2], s_fft_in[2]);
    preprocess_channel(mic_data[3], s_fft_in[3]);

    /* ---- GCC-PHAT on opposite pairs ---- */
    float peak_x = 0.0f, peak_y = 0.0f;
    float ratio_x = 0.0f, ratio_y = 0.0f;

    float delay_x = gcc_phat_delay(s_fft_in[MIC_X_A], s_fft_in[MIC_X_B], &peak_x, &ratio_x);
    float delay_y = gcc_phat_delay(s_fft_in[MIC_Y_A], s_fft_in[MIC_Y_B], &peak_y, &ratio_y);

    /* ---- Estimate angle ---- */
    float angle = estimate_angle(delay_x, delay_y);

    /* Direction vector magnitude */
    float dx = delay_x * SSL_SOUND_VELOCITY / (s_sample_rate * SSL_MIC_DISTANCE_M);
    float dy = delay_y * SSL_SOUND_VELOCITY / (s_sample_rate * SSL_MIC_DISTANCE_M);
    float dir_vec = sqrtf(dx * dx + dy * dy);

    /* ---- Validate ---- */
    bool valid = true;
    valid &= (angle >= 0.0f);
    valid &= (peak_x >= MIN_PEAK_VALUE_GCC && peak_y >= MIN_PEAK_VALUE_GCC);
    valid &= (ratio_x >= MIN_PEAK_RATIO_GCC && ratio_y >= MIN_PEAK_RATIO_GCC);
    valid &= (fabsf(delay_x) < ((float)s_max_lag - 0.25f));
    valid &= (fabsf(delay_y) < ((float)s_max_lag - 0.25f));
    valid &= (dir_vec >= MIN_DIRECTION_VECTOR);

    if (!valid) return result;

    result.angle      = angle;
    result.confidence = clampf(dir_vec, 0.0f, 1.0f);
    result.valid      = true;
    return result;
}
