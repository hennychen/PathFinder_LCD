#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_tdm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_dsp.h"
#include "esp_err.h"
#include "esp_log.h"

#include "es7210.h"
#include "calc_direction.h"

/**
 * @file calc_direction.c
 * @brief 四麦克风 GCC-PHAT 声源定位实现。
 *
 * 本文件完成以下工作：
 * 1. 配置 ES7210 四通道 ADC 和 I2S/TDM 输入；
 * 2. 将 I2S 原始采样转换为 -1.0~1.0 的浮点音频数据；
 * 3. 对 X/Y 两组相对麦克风分别执行 GCC-PHAT 互相关；
 * 4. 根据两轴到达时间差估算 0~360 度声源方向；
 * 5. 通过峰值、峰值比、响度和角度窗口过滤无效结果。
 */

#ifndef PI
#define PI 3.14159265358979323846f
#endif

/**
 * @brief 主程序中定义的四通道麦克风采样缓存。
 *
 * i2s_read_mics() 负责填充该数组，calcAngle() 负责读取该数组并计算角度。
 */
extern float i2s_mic_data[4][PDM_SAMPLE_SIZE];

/**
 * @brief 兼容旧接口的 I2C 寄存器数据缓存。
 */
uint8_t I2CRegData[8];

/**
 * @brief FFT 长度，与每帧采样点数保持一致。
 */
#define FFT_SIZE                    PDM_SAMPLE_SIZE

/* ===================== Parameters ===================== */
/**
 * @name 声源定位基础参数
 * @{
 */
#define SSL_SAMPLE_RATE             48000.0f
#define ES7210_SAMPLE_RATE          48000
#define ES7210_MCLK_MULTIPLE        256

/** @brief 相对麦克风间距，单位米。 */
#define SSL_MIC_DISTANCE_M          0.050f
/** @brief 声速，单位 m/s。 */
#define SSL_SOUND_VELOCITY          343.0f
/** @brief 物理上允许的最大到达时间差。 */
#define SSL_MAX_DELAY_S             (SSL_MIC_DISTANCE_M / SSL_SOUND_VELOCITY)
/** @brief GCC-PHAT 在互相关中心左右搜索的最大采样点偏移。
 *  物理极限 = 0.050/343 * 48000 = 6.997 ≈ 7 samples。
 *  设为 9 给抛物线插值和边界留出余量，避免峰值被截断。 */
#define SSL_MAX_OFFSET              9
/** @} */

/**
 * @name GCC-PHAT 频带限制
 * @{
 */
/** @brief 参与方向估计的最低频率，低于该频率的频点会被抑制。 */
#define GCC_MIN_FREQ_HZ             500.0f
/** @brief 参与方向估计的最高频率，高于该频率的频点会被抑制。 */
#define GCC_MAX_FREQ_HZ             3200.0f
/** @} */

/**
 * @name 有效帧判定阈值
 * @{
 */
/** @brief 默认声音活动量阈值。activity 小于该值时认为没有足够声音。 */
#define MIN_ACTIVITY_VALUE          (-4.0f)
/** @brief 四通道平均 RMS 下限，过低表示声音太弱。 */
#define MIN_RAW_RMS_LEVEL           0.0005f
/** @brief 原始采样峰值上限，过高可能表示削顶或异常。 */
#define MAX_RAW_PEAK_LEVEL          0.98f

/** @brief GCC 互相关峰值下限，过低表示峰值不明显。 */
#define MIN_PEAK_VALUE_GCC          0.0015f
/** @brief GCC 主峰与次峰比值下限，过低表示方向置信度不足。 */
#define MIN_PEAK_RATIO_GCC          1.02f
/** @brief 两轴合成方向向量下限，过低表示方向性不足。 */
#define MIN_DIRECTION_VECTOR        0.02f
/** @brief 查找次峰时避开主峰附近的保护点数。 */
#define PEAK_RATIO_GUARD_BINS       1
/** @} */

/**
 * @name 角度稳定滤波参数
 * @{
 */
/** @brief 环形角度缓冲区长度。 */
#define GCC_ANGLE_WINDOW            5
/** @brief 输出稳定角度前要求的最少有效帧数量。 */
#define GCC_MIN_VALID_COUNT         2
/** @brief 窗口内角度相对均值的最大允许离散度。 */
#define GCC_MAX_SPREAD_DEG          35.0f
/** @brief 稳定角度指数平滑系数，越大响应越快、越小越平滑。 */
#define ANGLE_SMOOTH_ALPHA          0.35f
/** @brief 输出角度量化步进，单位度。 */
#define ANGLE_OUTPUT_STEP_DEG       1.0f
/** @} */

/**
 * @name 坐标轴修正参数
 * @{
 */
/** @brief 输出角度整体偏移，用于安装方向校准。 */
#define SSL_ANGLE_OFFSET_DEG        0.0f
/** @brief X 轴方向符号，安装方向反了可改为 -1。 */
#define SSL_AXIS_X_SIGN             1.0f
/** @brief Y 轴方向符号，安装方向反了可改为 -1。 */
#define SSL_AXIS_Y_SIGN             1.0f
/** @} */

/**
 * @name ES7210 音频 ADC 配置
 * @{
 */
/** @brief ES7210 I2C 总线频率。 */
#define ES7210_I2C_FREQ_HZ          100000

/** @brief I2C master bus handle for ES7210. */
static i2c_master_bus_handle_t s_i2c_bus_handle = NULL;
/** @brief ES7210 I2C 地址。 */
#define ES7210_I2C_ADDR             ES7210_ADDRRES_00
/** @brief ES7210 输出位宽。 */
#define ES7210_BIT_WIDTH            ES7210_I2S_BITS_16B
/** @brief 麦克风偏置电压配置。 */
#define ES7210_MIC_BIAS             ES7210_MIC_BIAS_2V87
/** @brief 麦克风模拟增益配置。 */
#define ES7210_MIC_GAIN             ES7210_MIC_GAIN_37_5DB
/** @brief ADC 数字音量，单位 dB，范围 -95~+32。+30dB 大幅提升信号。 */
#define ES7210_ADC_VOLUME_DB        30
/** @} */

/**
 * @name 麦克风物理方向到 I2S 槽位的映射
 * @{
 */
/** @brief 右侧麦克风对应的 I2S 槽位。 */
#define MIC_RIGHT_CH                0
/** @brief 左侧麦克风对应的 I2S 槽位。 */
#define MIC_LEFT_CH                 1
/** @brief 上方或前方麦克风对应的 I2S 槽位。 */
#define MIC_TOP_CH                  2
/** @brief 下方或后方麦克风对应的 I2S 槽位。 */
#define MIC_BOTTOM_CH               3

/** @brief X 轴 A 端麦克风，用于和 MIC_X_B 计算左右方向时间差。 */
#define MIC_X_A                     MIC_RIGHT_CH
/** @brief X 轴 B 端麦克风，用于和 MIC_X_A 计算左右方向时间差。 */
#define MIC_X_B                     MIC_LEFT_CH
/** @brief Y 轴 A 端麦克风，用于和 MIC_Y_B 计算前后方向时间差。 */
#define MIC_Y_A                     MIC_TOP_CH
/** @brief Y 轴 B 端麦克风，用于和 MIC_Y_A 计算前后方向时间差。 */
#define MIC_Y_B                     MIC_BOTTOM_CH
/** @} */

/**
 * @name ESP32-S3 与 ES7210 的 I2C/I2S 引脚连接
 * @{
 */
#define ES7210_I2C_PORT             I2C_NUM_0
#define ES7210_I2C_SDA_GPIO         GPIO_NUM_38
#define ES7210_I2C_SCL_GPIO         GPIO_NUM_39

#define ES7210_I2S_MCLK_GPIO        GPIO_NUM_42
#define ES7210_I2S_BCLK_GPIO        GPIO_NUM_41
#define ES7210_I2S_WS_GPIO          GPIO_NUM_40
#define ES7210_I2S_DIN_GPIO         GPIO_NUM_21
/** @} */


// #define ES7210_I2C_PORT             I2C_NUM_0
// #define ES7210_I2C_SDA_GPIO         GPIO_NUM_47
// #define ES7210_I2C_SCL_GPIO         GPIO_NUM_21

// #define ES7210_I2S_MCLK_GPIO        GPIO_NUM_14
// #define ES7210_I2S_BCLK_GPIO        GPIO_NUM_13
// #define ES7210_I2S_WS_GPIO          GPIO_NUM_12
// #define ES7210_I2S_DIN_GPIO         GPIO_NUM_11

static const char *TAG = "SSL_GCC";

/** @brief I2S 接收通道句柄。 */
static i2s_chan_handle_t rx_chan = NULL;
/** @brief ES7210 codec 设备句柄。 */
static es7210_dev_handle_t es7210_handle = NULL;
/** @brief ES7210 是否已经完成初始化。 */
static bool es7210_ready = false;

/** @brief Hann 窗系数，用于降低 FFT 频谱泄漏。 */
static float fft_window[FFT_SIZE];
/** @brief 去直流并加窗后的四通道 FFT 输入缓存。 */
static float fft_in[4][FFT_SIZE];

/** @brief GCC-PHAT 输入 A 的复数 FFT 缓存，实部和虚部交错存放。 */
static float p_input_a[FFT_SIZE * 2];
/** @brief GCC-PHAT 输入 B 的复数 FFT 缓存，实部和虚部交错存放。 */
static float p_input_b[FFT_SIZE * 2];
/** @brief 互功率谱和 IFFT 结果缓存。 */
static float p_output[FFT_SIZE * 2];
/** @brief IFFT 后的互相关结果。 */
static float xcorr[FFT_SIZE];
/** @brief fftshift 后的互相关结果，便于以中心点为零延迟搜索。 */
static float xcorr_shifted[FFT_SIZE];

/** @brief 最近一次计算的调试信息。 */
static calc_debug_info_t g_debug_info = {0};
/** @brief 运行时声音活动量阈值，可通过 setActivityValue() 修改。 */
static float ActivitySetValue = MIN_ACTIVITY_VALUE;
/** @brief 最近一次 I2S 读取的总字节数。 */
size_t bytes_read = 0;

/**
 * @brief 角度稳定滤波器状态。
 *
 * 保存最近若干个有效原始角度，只有窗口内角度足够集中时才输出稳定角度。
 */
typedef struct {
    float angle_buf[GCC_ANGLE_WINDOW]; /**< 环形缓冲区，保存最近有效角度。 */
    uint8_t count;                     /**< 当前缓冲区内有效角度数量。 */
    uint8_t index;                     /**< 下一次写入位置。 */
    bool has_stable;                   /**< 是否已经产生过稳定角度。 */
    float stable_angle;                /**< 当前平滑后的稳定角度。 */
} gcc_angle_filter_t;

/** @brief 全局角度滤波器实例。 */
static gcc_angle_filter_t s_angle_filter = {0};

/* ===================== Utilities ===================== */
/**
 * @brief 将浮点数限制在指定范围内。
 *
 * @param v 输入值。
 * @param min_v 最小允许值。
 * @param max_v 最大允许值。
 * @return float 被限制后的结果。
 */
static float clampf_local(float v, float min_v, float max_v)
{
    if (v < min_v) return min_v;
    if (v > max_v) return max_v;
    return v;
}

/**
 * @brief 将角度归一化到 [0, 360) 区间。
 *
 * @param angle 输入角度，单位度。
 * @return float 归一化后的角度，单位度。
 */
static float normalize_angle_deg(float angle)
{
    while (angle < 0.0f) angle += 360.0f;
    while (angle >= 360.0f) angle -= 360.0f;
    return angle;
}

/**
 * @brief 计算从一个角度转到另一个角度的最短有符号差值。
 *
 * @param from 起始角度，单位度。
 * @param to 目标角度，单位度。
 * @return float 最短差值，范围约为 [-180, 180]。
 */
static float angle_diff_signed(float from, float to)
{
    float diff = to - from;
    while (diff > 180.0f) diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;
    return diff;
}

/**
 * @brief 计算两个角度之间的最短绝对差值。
 *
 * @param a 第一个角度，单位度。
 * @param b 第二个角度，单位度。
 * @return float 最短绝对差值，单位度。
 */
static float angle_diff_abs(float a, float b)
{
    return fabsf(angle_diff_signed(a, b));
}

/**
 * @brief 在圆周角度上执行线性插值。
 *
 * @param old_angle 当前稳定角度，单位度。
 * @param new_angle 新观测角度，单位度。
 * @param alpha 插值系数，0 表示保持旧值，1 表示完全使用新值。
 * @return float 插值后的归一化角度。
 */
static float angle_lerp_circular(float old_angle, float new_angle, float alpha)
{
    return normalize_angle_deg(old_angle + alpha * angle_diff_signed(old_angle, new_angle));
}

/**
 * @brief 按指定步进量化角度。
 *
 * @param angle 输入角度，单位度。
 * @param step_deg 量化步进，单位度。
 * @return float 量化并归一化后的角度。
 */
static float quantize_angle(float angle, float step_deg)
{
    if (step_deg <= 0.0f) return normalize_angle_deg(angle);
    return normalize_angle_deg(roundf(angle / step_deg) * step_deg);
}

/**
 * @brief 将算法内部角度转换为方向字符串。
 *
 * @param angle 声源方向角，单位度。
 * @return const char* 方向文本。
 */
static const char *angle_to_direction(float angle)
{
    if (angle < 0.0f) return "invalid";
    if (angle >= 337.5f || angle < 22.5f) return "right";
    if (angle < 67.5f) return "front-right";
    if (angle < 112.5f) return "front";
    if (angle < 157.5f) return "front-left";
    if (angle < 202.5f) return "left";
    if (angle < 247.5f) return "back-left";
    if (angle < 292.5f) return "back";
    return "back-right";
}

/**
 * @brief 根据 FFT bin 下标计算折叠后的实际频率。
 *
 * 实数 FFT 的高半区对应负频率，这里将其折叠到正频率，便于做频带筛选。
 *
 * @param index FFT bin 下标。
 * @param length FFT 长度。
 * @return float 该 bin 对应的频率，单位 Hz。
 */
static float folded_frequency_hz(size_t index, uint16_t length)
{
    size_t k = index;
    if (index > (size_t)(length / 2)) {
        k = (size_t)length - index;
    }
    return (float)k * SSL_SAMPLE_RATE / (float)length;
}

/**
 * @brief 将数组前后半区交换，实现类似 fftshift 的效果。
 *
 * @param input 输入数组。
 * @param output 输出数组。
 * @param size 数组长度。
 */
static void shift_array(const float *input, float *output, uint16_t size)
{
    uint16_t half = size / 2;
    memcpy(output, &input[half], sizeof(float) * (size - half));
    memcpy(&output[size - half], input, sizeof(float) * half);
}

/**
 * @brief 查找数组中的最大值及其下标。
 *
 * @param data 输入数组。
 * @param size 数组长度。
 * @param max_val 输出最大值。
 * @param max_index 输出最大值下标。
 */
static void find_max_index(const float *data, uint16_t size, float *max_val, uint16_t *max_index)
{
    float max_v = data[0];
    uint16_t max_i = 0;

    for (uint16_t i = 1; i < size; i++) {
        if (data[i] > max_v) {
            max_v = data[i];
            max_i = i;
        }
    }

    *max_val = max_v;
    *max_index = max_i;
}

/**
 * @brief 计算数组均值。
 *
 * @param x 输入数组。
 * @param len 数组长度。
 * @return float 均值。
 */
static float calc_mean(const float *x, size_t len)
{
    float sum = 0.0f;
    for (size_t i = 0; i < len; i++) {
        sum += x[i];
    }
    return sum / (float)len;
}

/**
 * @brief 去除直流分量后计算 RMS。
 *
 * @param x 输入音频数组。
 * @param len 数组长度。
 * @return float 去直流 RMS，用于衡量当前帧音量。
 */
static float calc_rms_without_dc(const float *x, size_t len)
{
    float mean = calc_mean(x, len);
    float sum = 0.0f;

    for (size_t i = 0; i < len; i++) {
        float d = x[i] - mean;
        sum += d * d;
    }

    return sqrtf(sum / (float)len);
}

/**
 * @brief 计算当前四通道平均声音活动量。
 *
 * 活动量定义为四通道平均 RMS 的 log10，用于粗略过滤过弱声音和环境静音。
 *
 * @return float 当前帧活动量。
 */
static float calc_activity_value(void)
{
    float rms_sum = 0.0f;

    for (int ch = 0; ch < 4; ch++) {
        rms_sum += calc_rms_without_dc(i2s_mic_data[ch], FFT_SIZE);
    }

    float avg_rms = rms_sum * 0.25f;
    if (avg_rms < 0.0001f) avg_rms = 0.0001f;

    float activity = log10f(avg_rms);
    if (isnan(activity) || isinf(activity)) activity = -10.0f;
    return activity;
}

/**
 * @brief 计算四通道原始采样绝对峰值。
 *
 * @return float 0~1 范围内的最大绝对采样值。
 */
static float calc_peak_abs_all(void)
{
    float peak = 0.0f;

    for (int ch = 0; ch < 4; ch++) {
        for (int i = 0; i < FFT_SIZE; i++) {
            float v = fabsf(i2s_mic_data[ch][i]);
            if (v > peak) peak = v;
        }
    }

    return peak;
}

/**
 * @brief 对单通道音频进行 GCC-PHAT 前预处理。
 *
 * 预处理包括去直流和乘 Hann 窗，降低直流偏置与频谱泄漏对互相关结果的影响。
 *
 * @param src 原始输入采样。
 * @param dst 预处理后的输出采样。
 */
static void preprocess_channel(const float *src, float *dst)
{
    float mean = calc_mean(src, FFT_SIZE);

    for (uint32_t i = 0; i < FFT_SIZE; i++) {
        dst[i] = (src[i] - mean) * fft_window[i];
    }
}

/* ===================== GCC-PHAT ===================== */
/**
 * @brief 计算两路麦克风信号的 GCC-PHAT 到达时间差。
 *
 * GCC-PHAT 的核心思想：
 * 1. 分别对两路信号做 FFT；
 * 2. 计算互功率谱 B * conj(A)；
 * 3. 对互功率谱做 PHAT 归一化，只保留相位信息；
 * 4. IFFT 得到互相关曲线；
 * 5. 在物理允许的延迟范围内寻找主峰位置。
 *
 * @param a A 端麦克风预处理后的采样。
 * @param b B 端麦克风预处理后的采样。
 * @param length FFT 长度。
 * @param offset 以零延迟为中心向两侧搜索的最大采样点偏移。
 * @param peak_out 输出 GCC 主峰值，可为 NULL。
 * @param peak_ratio_out 输出主峰/次峰比值，可为 NULL。
 * @return float B 相对 A 的估计延迟，单位为采样点，可带小数。
 */
static float calc_gcc_phat_delay(const float *a,
                                 const float *b,
                                 uint16_t length,
                                 uint16_t offset,
                                 float *peak_out,
                                 float *peak_ratio_out)
{
    uint16_t center_i = length / 2;
    uint16_t start_i = center_i - offset;
    uint16_t search_len = 2 * offset + 1;

    /** @brief 组装 ESP-DSP 复数 FFT 输入，实部为音频采样，虚部为 0。 */
    for (size_t i = 0; i < length; i++) {
        p_input_a[2 * i] = a[i];
        p_input_a[2 * i + 1] = 0.0f;
        p_input_b[2 * i] = b[i];
        p_input_b[2 * i + 1] = 0.0f;
    }

    /** @brief 分别计算两路信号的频谱。 */
    dsps_fft2r_fc32(p_input_a, length);
    dsps_fft2r_fc32(p_input_b, length);
    dsps_bit_rev_fc32(p_input_a, length);
    dsps_bit_rev_fc32(p_input_b, length);

    /** @brief 计算 PHAT 归一化互功率谱，并抑制定位不关心的频段。 */
    for (size_t i = 0; i < length; i++) {
        float ar = p_input_a[2 * i];
        float ai = p_input_a[2 * i + 1];
        float br = p_input_b[2 * i];
        float bi = p_input_b[2 * i + 1];
        float freq = folded_frequency_hz(i, length);

        if (freq < GCC_MIN_FREQ_HZ || freq > GCC_MAX_FREQ_HZ) {
            p_output[2 * i] = 0.0f;
            p_output[2 * i + 1] = 0.0f;
            continue;
        }

        /** @brief 互功率谱：B * conj(A)。 */
        float real = br * ar + bi * ai;
        float imag = -br * ai + bi * ar;
        float mag = sqrtf(real * real + imag * imag);

        if (mag < 1e-10f) mag = 1e-10f;

        p_output[2 * i] = real / mag;
        p_output[2 * i + 1] = imag / mag;
    }

    /** @brief 通过共轭 + FFT + 缩放实现 IFFT，得到互相关序列。 */
    for (size_t i = 0; i < length; i++) {
        p_output[2 * i + 1] = -p_output[2 * i + 1];
    }

    dsps_fft2r_fc32(p_output, length);
    dsps_bit_rev_fc32(p_output, length);

    for (size_t i = 0; i < length; i++) {
        p_output[2 * i] /= (float)length;
        xcorr[i] = p_output[2 * i];
    }

    /** @brief 将零延迟移动到数组中心，方便只搜索物理可达的延迟范围。 */
    shift_array(xcorr, xcorr_shifted, length);

    float peak = 0.0f;
    uint16_t peak_i = 0;
    find_max_index(xcorr_shifted + start_i, search_len, &peak, &peak_i);

    /** @brief 查找避开主峰附近后的次峰，用于估计主峰置信度。 */
    float second_peak = 1e-6f;
    for (uint16_t i = 0; i < search_len; i++) {
        uint16_t distance = (i > peak_i) ? (uint16_t)(i - peak_i) : (uint16_t)(peak_i - i);
        if (distance <= PEAK_RATIO_GUARD_BINS) continue;

        float v = xcorr_shifted[start_i + i];
        if (v > second_peak) second_peak = v;
    }

    if (peak_out) *peak_out = peak;
    if (peak_ratio_out) *peak_ratio_out = peak / second_peak;

    float delay = (float)peak_i - (float)offset;

    /** @brief 对主峰附近三个点做抛物线插值，获得亚采样级延迟估计。 */
    if (peak_i > 0 && peak_i < (uint16_t)(search_len - 1)) {
        float y0 = xcorr_shifted[start_i + peak_i - 1];
        float y1 = xcorr_shifted[start_i + peak_i];
        float y2 = xcorr_shifted[start_i + peak_i + 1];
        float denom = y0 - 2.0f * y1 + y2;

        if (fabsf(denom) > 1e-10f) {
            delay += (y0 - y2) / (2.0f * denom);
        }
    }

    return delay;
}

/**
 * @brief 根据 X/Y 两轴采样延迟估算二维声源角度。
 *
 * delay_x 和 delay_y 会先换算为归一化方向分量，再通过 atan2 得到角度。
 *
 * @param delay_x_samples X 轴麦克风对的延迟，单位采样点。
 * @param delay_y_samples Y 轴麦克风对的延迟，单位采样点。
 * @return float 0~360 度角度；若方向向量近似为 0，返回 -1。
 */
static float estimate_angle_deg(float delay_x_samples, float delay_y_samples)
{
    float raw_x = delay_x_samples * SSL_SOUND_VELOCITY / (SSL_SAMPLE_RATE * SSL_MIC_DISTANCE_M);
    float raw_y = delay_y_samples * SSL_SOUND_VELOCITY / (SSL_SAMPLE_RATE * SSL_MIC_DISTANCE_M);

    raw_x = clampf_local(raw_x, -1.0f, 1.0f);
    raw_y = clampf_local(raw_y, -1.0f, 1.0f);

    float x = SSL_AXIS_X_SIGN * raw_x;
    float y = SSL_AXIS_Y_SIGN * raw_y;

    if (fabsf(x) < 1e-5f && fabsf(y) < 1e-5f) return -1.0f;

    return normalize_angle_deg((atan2f(y, x) * 180.0f / PI) + SSL_ANGLE_OFFSET_DEG);
}

/* ===================== Angle filter ===================== */
/**
 * @brief 重置角度稳定滤波器。
 *
 * 当声音过弱、结果无效或出现明显异常时调用，避免旧角度继续影响后续结果。
 */
static void angle_filter_reset(void)
{
    memset(&s_angle_filter, 0, sizeof(s_angle_filter));
    s_angle_filter.stable_angle = -1.0f;
}

/**
 * @brief 计算多个圆周角度的均值。
 *
 * 直接对 359 度和 1 度做普通平均会得到 180 度，因此这里先转换为单位圆
 * 向量，再对向量求平均。
 *
 * @param angles 角度数组，单位度。
 * @param count 角度数量。
 * @return float 圆周均值角度；向量抵消时返回 -1。
 */
static float circular_mean_angles(const float *angles, uint8_t count)
{
    float sx = 0.0f;
    float sy = 0.0f;

    for (uint8_t i = 0; i < count; i++) {
        float rad = angles[i] * PI / 180.0f;
        sx += cosf(rad);
        sy += sinf(rad);
    }

    if (fabsf(sx) < 1e-6f && fabsf(sy) < 1e-6f) return -1.0f;

    return normalize_angle_deg(atan2f(sy, sx) * 180.0f / PI);
}

/**
 * @brief 计算窗口内角度相对圆周均值的最大离散度。
 *
 * @param angles 角度数组，单位度。
 * @param count 角度数量。
 * @param mean 圆周均值角度，单位度。
 * @return float 最大角度偏差，单位度。
 */
static float max_spread_around_mean(const float *angles, uint8_t count, float mean)
{
    float spread = 0.0f;

    for (uint8_t i = 0; i < count; i++) {
        float d = angle_diff_abs(angles[i], mean);
        if (d > spread) spread = d;
    }

    return spread;
}

/**
 * @brief 更新角度稳定滤波器。
 *
 * 只有当最近若干个原始角度数量足够、且围绕圆周均值的离散度不超过阈值时，
 * 才输出稳定角度。该函数用于减少瞬时噪声导致的 LED 跳动。
 *
 * @param raw_angle 当前帧原始角度。
 * @return float 稳定角度；返回 -1 表示窗口内结果暂不稳定。
 */
static float angle_filter_update(float raw_angle)
{
    if (raw_angle < 0.0f) return -1.0f;

    s_angle_filter.angle_buf[s_angle_filter.index] = normalize_angle_deg(raw_angle);
    s_angle_filter.index = (uint8_t)((s_angle_filter.index + 1) % GCC_ANGLE_WINDOW);

    if (s_angle_filter.count < GCC_ANGLE_WINDOW) {
        s_angle_filter.count++;
    }

    if (s_angle_filter.count < GCC_MIN_VALID_COUNT) {
        return -1.0f;
    }

    float mean = circular_mean_angles(s_angle_filter.angle_buf, s_angle_filter.count);
    if (mean < 0.0f) return -1.0f;

    float spread = max_spread_around_mean(s_angle_filter.angle_buf, s_angle_filter.count, mean);
    if (spread > GCC_MAX_SPREAD_DEG) return -1.0f;

    if (!s_angle_filter.has_stable) {
        s_angle_filter.has_stable = true;
        s_angle_filter.stable_angle = mean;
    } else {
        s_angle_filter.stable_angle = angle_lerp_circular(s_angle_filter.stable_angle,
                                                          mean,
                                                          ANGLE_SMOOTH_ALPHA);
    }

    return quantize_angle(s_angle_filter.stable_angle, ANGLE_OUTPUT_STEP_DEG);
}

/* ===================== Public calculation API ===================== */
/**
 * @brief 计算当前音频帧的稳定声源方向角。
 *
 * 本函数会依次执行：
 * 1. 检查声音活动量、RMS 和削顶峰值；
 * 2. 对四路音频去直流并加窗；
 * 3. 对 X/Y 两组麦克风执行 GCC-PHAT；
 * 4. 根据延迟估算原始角度；
 * 5. 使用峰值、峰值比、延迟边界和方向向量过滤低可信结果；
 * 6. 使用角度窗口滤波输出最终角度。
 *
 * @return float 0~360 度稳定角度；返回 -1 表示当前帧没有有效定位结果。
 */
float calcAngle(void)
{
    static uint32_t dbg_skip = 0;  /* 调试打印节流计数器 */
    static uint32_t warmup_cnt = 0; /* 启动后前 5 帧丢弃（初始化瞬态 peak=1.0） */

    if (warmup_cnt < 5) {
        warmup_cnt++;
        if (warmup_cnt == 5) {
            ESP_LOGI(TAG, "Warmup done, starting normal detection");
        }
        return -1.0f;
    }

    /** @brief 第一层过滤：过弱声音、过低 RMS 或削顶峰值直接判为无效。 */
    float activity = calc_activity_value();
    float raw_rms = calcRawLevel();
    float raw_peak = calc_peak_abs_all();

    if (activity < ActivitySetValue || raw_rms < MIN_RAW_RMS_LEVEL || raw_peak > MAX_RAW_PEAK_LEVEL) {
        angle_filter_reset();
        g_debug_info.angle_final = -1.0f;
        g_debug_info.activity_value = activity;
        if (dbg_skip++ % 50 == 0) {
            ESP_LOGW(TAG, "L1-REJECT act=%.3f (thr=%.3f) rms=%.4f (thr=%.4f) peak=%.4f",
                     activity, ActivitySetValue, raw_rms, MIN_RAW_RMS_LEVEL, raw_peak);
        }
        return -1.0f;
    }

    /** @brief 去直流并加窗，准备进入频域互相关计算。 */
    preprocess_channel(i2s_mic_data[0], fft_in[0]);
    preprocess_channel(i2s_mic_data[1], fft_in[1]);
    preprocess_channel(i2s_mic_data[2], fft_in[2]);
    preprocess_channel(i2s_mic_data[3], fft_in[3]);

    float peak_x = 0.0f;
    float peak_y = 0.0f;
    float ratio_x = 0.0f;
    float ratio_y = 0.0f;

    /** @brief 分别计算 X 轴和 Y 轴两对麦克风的到达时间差。 */
    float delay_x = calc_gcc_phat_delay(fft_in[MIC_X_A], fft_in[MIC_X_B], FFT_SIZE,
                                        SSL_MAX_OFFSET, &peak_x, &ratio_x);
    float delay_y = calc_gcc_phat_delay(fft_in[MIC_Y_A], fft_in[MIC_Y_B], FFT_SIZE,
                                        SSL_MAX_OFFSET, &peak_y, &ratio_y);

    /** @brief 将两轴时间差合成为原始方向角。 */
    float angle_raw = estimate_angle_deg(delay_x, delay_y);

    /** @brief 将采样点延迟换算为归一化方向向量，用于判断方向性是否足够明显。 */
    float dir_x = delay_x * SSL_SOUND_VELOCITY / (SSL_SAMPLE_RATE * SSL_MIC_DISTANCE_M);
    float dir_y = delay_y * SSL_SOUND_VELOCITY / (SSL_SAMPLE_RATE * SSL_MIC_DISTANCE_M);
    float dir_vec = sqrtf(dir_x * dir_x + dir_y * dir_y);

    /**
     * @brief 第二层过滤：判断 GCC 结果是否可信。
     *
     * 任意条件不满足都会返回 -1，APP 层也就不会打印 APP angle。
     */
    bool valid = true;
    valid &= (angle_raw >= 0.0f);
    valid &= (peak_x >= MIN_PEAK_VALUE_GCC && peak_y >= MIN_PEAK_VALUE_GCC);
    valid &= (ratio_x >= MIN_PEAK_RATIO_GCC && ratio_y >= MIN_PEAK_RATIO_GCC);
    valid &= (fabsf(delay_x) < ((float)SSL_MAX_OFFSET - 0.25f));
    valid &= (fabsf(delay_y) < ((float)SSL_MAX_OFFSET - 0.25f));
    valid &= (dir_vec >= MIN_DIRECTION_VECTOR);
    valid &= (fabsf(delay_x / SSL_SAMPLE_RATE) <= SSL_MAX_DELAY_S);
    valid &= (fabsf(delay_y / SSL_SAMPLE_RATE) <= SSL_MAX_DELAY_S);

    /** @brief 保存本帧调试数据，方便外部读取失败原因和中间结果。 */
    g_debug_info.activity_value = activity;
    g_debug_info.peak_ew = peak_x;
    g_debug_info.peak_ns = peak_y;
    g_debug_info.delay_ew = delay_x;
    g_debug_info.delay_ns = delay_y;
    g_debug_info.axis_x = dir_x;
    g_debug_info.axis_y = dir_y;
    g_debug_info.angle_raw = angle_raw;
    g_debug_info.low_confidence = (int)(ratio_x < MIN_PEAK_RATIO_GCC || ratio_y < MIN_PEAK_RATIO_GCC);
    g_debug_info.weak_peak = (int)(peak_x < MIN_PEAK_VALUE_GCC || peak_y < MIN_PEAK_VALUE_GCC);
    g_debug_info.weak_axis = (int)(dir_vec < MIN_DIRECTION_VECTOR);
    g_debug_info.clipped_delay = (int)(fabsf(delay_x) >= ((float)SSL_MAX_OFFSET - 0.25f) ||
                                       fabsf(delay_y) >= ((float)SSL_MAX_OFFSET - 0.25f));

    if (!valid) {
        g_debug_info.angle_final = -1.0f;
        if (dbg_skip++ % 50 == 0) {
            ESP_LOGW(TAG, "L2-REJECT ang=%.1f delay=[%.2f,%.2f] ratio=[%.2f,%.2f] "
                     "peak=[%.4f,%.4f] act=%.2f dir=%.3f flags: %s%s%s%s",
                     angle_raw, delay_x, delay_y, ratio_x, ratio_y,
                     peak_x, peak_y, activity, dir_vec,
                     g_debug_info.weak_peak ? "WP " : "",
                     g_debug_info.low_confidence ? "LC " : "",
                     g_debug_info.weak_axis ? "WA " : "",
                     g_debug_info.clipped_delay ? "CD" : "");
        }
        return -1.0f;
    }

    /** @brief 第三层过滤：要求角度窗口稳定后才输出最终角度。 */
    float angle_stable = angle_filter_update(angle_raw);
    if (angle_stable < 0.0f) {
        g_debug_info.angle_final = -1.0f;
        return -1.0f;
    }

    g_debug_info.angle_final = angle_stable;
    return angle_stable;
}

/**
 * @brief 准备 FFT 窗函数并初始化角度滤波器。
 *
 * @param samplingRate 采样率，当前实现使用固定宏配置，该参数保留用于兼容。
 */
void prepareWindow(float samplingRate)
{
    (void)samplingRate;

    dsps_fft2r_init_fc32(NULL, FFT_SIZE);

    /** @brief 生成 Hann 窗，降低截断帧带来的频谱泄漏。 */
    float tmp = 2.0f * PI / (float)FFT_SIZE;
    for (uint32_t i = 0; i < FFT_SIZE; i++) {
        fft_window[i] = 0.5f - 0.5f * cosf((float)i * tmp);
    }

    angle_filter_reset();
}

/* ===================== ES7210 + I2S ===================== */
/**
 * @brief 初始化 ES7210 四通道音频 ADC。
 *
 * 本函数完成 I2C 主机初始化、创建 ES7210 codec 句柄、配置采样率、
 * TDM 模式、位宽、麦克风偏置和增益。
 *
 * @return esp_err_t ESP_OK 表示初始化成功，其他值表示初始化失败。
 */
static esp_err_t init_es7210(void)
{
    if (es7210_ready) return ESP_OK;

    /** @brief 使用新版 I2C master 驱动初始化 I2C 总线。 */
    i2c_master_bus_config_t i2c_bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = ES7210_I2C_SDA_GPIO,
        .scl_io_num = ES7210_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&i2c_bus_cfg, &s_i2c_bus_handle);
    if (err != ESP_OK) return err;

    /** @brief I2C 总线扫描：检测哪些地址有设备响应。 */
    ESP_LOGI(TAG, "Scanning I2C bus (SDA=%d, SCL=%d)...", ES7210_I2C_SDA_GPIO, ES7210_I2C_SCL_GPIO);
    int found = 0;
    for (uint8_t addr = 1; addr < 0x78; addr++) {
        esp_err_t probe = i2c_master_probe(s_i2c_bus_handle, addr, 10);
        if (probe == ESP_OK) {
            ESP_LOGI(TAG, "  I2C device found at 0x%02x", addr);
            found++;
        }
    }
    if (found == 0) {
        ESP_LOGE(TAG, "No I2C devices found! Check SDA/SCL wiring and ES7210 power.");
    }

    /** @brief 创建 ES7210 codec 设备句柄，后续配置都通过该句柄完成。 */
    if (es7210_handle == NULL) {
        es7210_i2c_config_t es7210_i2c_conf = {
            .i2c_bus_handle = s_i2c_bus_handle,
            .i2c_addr = ES7210_I2C_ADDR,
        };

        err = es7210_new_codec(&es7210_i2c_conf, &es7210_handle);
        if (err != ESP_OK) return err;
    }

    /** @brief 配置 ES7210 为 48 kHz、16 bit、I2S/TDM 四通道采集。 */
    es7210_codec_config_t codec_conf = {
        .sample_rate_hz = ES7210_SAMPLE_RATE,
        .mclk_ratio = ES7210_MCLK_MULTIPLE,
        .i2s_format = ES7210_I2S_FMT_I2S,
        .bit_width = ES7210_BIT_WIDTH,
        .mic_bias = ES7210_MIC_BIAS,
        .mic_gain = ES7210_MIC_GAIN,
        .flags.tdm_enable = true,
    };

    err = es7210_config_codec(es7210_handle, &codec_conf);
    if (err != ESP_OK) return err;

    err = es7210_config_volume(es7210_handle, ES7210_ADC_VOLUME_DB);
    if (err != ESP_OK) return err;

    es7210_ready = true;
    return ESP_OK;
}

/**
 * @brief 初始化四通道麦克风采集链路。
 *
 * 初始化顺序：
 * 1. 初始化 ES7210；
 * 2. 拉高功放或麦克风相关使能 GPIO；
 * 3. 创建 I2S RX 通道；
 * 4. 配置 I2S TDM 模式并使能接收。
 */
void initI2SMics(void)
{
    /** @brief 先拉高板级音频使能引脚，确保 ES7210 上电后才进行 I2C 通信。
     *
     * PA_EN 连接 GPIO45（NS4150B 功放使能）。 */
    gpio_config_t pa_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_45),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pa_conf);
    gpio_set_level(GPIO_NUM_45, 1);

    /* 等待 ES7210 电源稳定 */
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "PA_EN enabled (GPIO45), initializing ES7210...");

    esp_err_t err = init_es7210();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init_es7210 failed: %s", esp_err_to_name(err));
        rx_chan = NULL;
        return;
    }

    /** @brief I2S 接收通道基础配置，使用 I2S0 主机模式。 */
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 4,
        .dma_frame_num = 256,
    };

    /** @brief I2S TDM 配置：四槽位输入，只使用 DIN，不输出音频。 */
    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(ES7210_SAMPLE_RATE),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO,
            I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
        .gpio_cfg = {
            .mclk = ES7210_I2S_MCLK_GPIO,
            .bclk = ES7210_I2S_BCLK_GPIO,
            .ws = ES7210_I2S_WS_GPIO,
            .din = ES7210_I2S_DIN_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    tdm_cfg.clk_cfg.mclk_multiple = ES7210_MCLK_MULTIPLE;
    tdm_cfg.slot_cfg.total_slot = 4;
    tdm_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT;
    tdm_cfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;

    err = i2s_new_channel(&chan_cfg, NULL, &rx_chan);
    if (err != ESP_OK) {
        printf("i2s_new_channel failed: %s\n", esp_err_to_name(err));
        rx_chan = NULL;
        return;
    }

    err = i2s_channel_init_tdm_mode(rx_chan, &tdm_cfg);
    if (err != ESP_OK) {
        printf("i2s_channel_init_tdm_mode failed: %s\n", esp_err_to_name(err));
        rx_chan = NULL;
        return;
    }

    err = i2s_channel_enable(rx_chan);
    if (err != ESP_OK) {
        printf("i2s_channel_enable failed: %s\n", esp_err_to_name(err));
        rx_chan = NULL;
        return;
    }

    printf("I2S mic array initialized, sample_rate=%d Hz, 16bit x 4slot\n", ES7210_SAMPLE_RATE);
}

/**
 * @brief 从 I2S/TDM 接收一帧四通道麦克风数据。
 *
 * ES7210 输出四个 TDM 槽位，每个槽位 16 bit。函数会按
 * [ch0, ch1, ch2, ch3] 顺序拆分数据，并归一化到 -1.0~1.0 后写入
 * i2s_mic_data。
 */
void i2s_read_mics(void)
{
    int16_t buf[4 * I2S_READ_CHUNK_SAMPLES];

    if (rx_chan == NULL) {
        static uint32_t null_warn = 0;
        if (null_warn++ % 50 == 0) {
            ESP_LOGE(TAG, "i2s_read_mics: rx_chan is NULL (init failed?)");
        }
        return;
    }

    bytes_read = 0;

    /** @brief 分块读取，直到填满 PDM_SAMPLE_SIZE 个采样点。 */
    for (int chunk = 0; chunk < (PDM_SAMPLE_SIZE / I2S_READ_CHUNK_SAMPLES); chunk++) {
        size_t chunk_bytes = 0;
        esp_err_t err = i2s_channel_read(rx_chan, buf, sizeof(buf), &chunk_bytes, 1000);

        if (err != ESP_OK) {
            printf("i2s_channel_read failed: %s\n", esp_err_to_name(err));
            return;
        }

        if (chunk_bytes != sizeof(buf)) {
            printf("short read: %u / %u\n", (unsigned)chunk_bytes, (unsigned)sizeof(buf));
            return;
        }

        bytes_read += chunk_bytes;

        /** @brief 将交错的四槽位 int16 数据拆到四个浮点通道。 */
        for (int i = 0; i < I2S_READ_CHUNK_SAMPLES; i++) {
            int dst = chunk * I2S_READ_CHUNK_SAMPLES + i;

            i2s_mic_data[0][dst] = (float)buf[i * 4 + 0] / 32768.0f;
            i2s_mic_data[1][dst] = (float)buf[i * 4 + 1] / 32768.0f;
            i2s_mic_data[2][dst] = (float)buf[i * 4 + 2] / 32768.0f;
            i2s_mic_data[3][dst] = (float)buf[i * 4 + 3] / 32768.0f;
        }

        /** @brief 前 3 帧转储原始 int16 样本，验证 TDM 数据完整性。 */
        if (chunk == 0) {
            static int raw_dump_cnt = 0;
            if (raw_dump_cnt < 3) {
                raw_dump_cnt++;
                ESP_LOGI(TAG, "RAW DUMP #%d (first 16 int16 of chunk):", raw_dump_cnt);
                for (int i = 0; i < 16; i++) {
                    ESP_LOGI(TAG, "  s[%d]=%d", i, buf[i]);
                }
            }
        }
    }
}

/* ===================== Compatibility APIs ===================== */
/**
 * @brief 设置声音活动量阈值。
 *
 * @param fvalue 新阈值。activity 小于该阈值时，当前帧不会进入 GCC-PHAT。
 */
void setActivityValue(float fvalue)
{
    ActivitySetValue = fvalue;
}

/**
 * @brief 计算四通道平均去直流 RMS。
 *
 * @return float 四通道 RMS 平均值。
 */
float calcRawLevel(void)
{
    float rms_sum = 0.0f;
    for (int ch = 0; ch < 4; ch++) {
        rms_sum += calc_rms_without_dc(i2s_mic_data[ch], FFT_SIZE);
    }
    return rms_sum * 0.25f;
}

/**
 * @brief 计算每个通道各自的去直流 RMS。
 *
 * @param levels 输出数组，长度至少为 4；传入 NULL 时函数直接返回。
 */
void calcRawLevels(float levels[4])
{
    if (levels == NULL) return;

    for (int ch = 0; ch < 4; ch++) {
        levels[ch] = calc_rms_without_dc(i2s_mic_data[ch], FFT_SIZE);
    }
}

/**
 * @brief 兼容旧版本调试接口。
 *
 * 当前版本不在该函数内打印 RMS，保留空实现是为了避免旧调用代码编译失败。
 */
void print_mic_rms_debug(void)
{
}

/**
 * @brief 获取最近一次声源定位计算的调试信息。
 *
 * @param info 输出调试信息结构体指针；传入 NULL 时函数直接返回。
 */
void getCalcDebugInfo(calc_debug_info_t *info)
{
    if (info == NULL) return;
    *info = g_debug_info;
}

/**
 * @brief 兼容旧接口：设置通道映射 ID。
 *
 * @param mapId 通道映射 ID，当前实现不使用该参数。
 */
void setChannelMapId(int mapId) { (void)mapId; }

/**
 * @brief 兼容旧接口：获取通道映射 ID。
 *
 * @return int 当前固定返回 0。
 */
int getChannelMapId(void) { return 0; }

/**
 * @brief 兼容旧接口：获取通道映射数量。
 *
 * @return int 当前固定返回 1。
 */
int getChannelMapCount(void) { return 1; }

/**
 * @brief 兼容旧接口：设置轴对模式。
 *
 * @param mode 轴对模式，当前实现不使用该参数。
 */
void setAxisPairMode(int mode) { (void)mode; }

/**
 * @brief 兼容旧接口：获取轴对模式。
 *
 * @return int 当前固定返回 0。
 */
int getAxisPairMode(void) { return 0; }

/**
 * @brief 兼容旧接口：获取轴对模式数量。
 *
 * @return int 当前固定返回 1。
 */
int getAxisPairModeCount(void) { return 1; }
