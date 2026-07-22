/**
 * @file sound_localizer.h
 * @brief GCC-PHAT 四麦克风声源定位模块（移植自 AcousticEye calc_direction）
 *
 * 数据流：
 *   ES7210 TDM 4-ch → BoxAudioCodec::Read() 去交错 → sound_localizer_feed()
 *   → 环形缓冲区 → 后台任务 X/Y 双轴 GCC-PHAT → atan2 → 0~360° 角度
 *
 * 适配 PathFinder Tracker B板：
 *   - 采样率：24kHz（AcousticEye 原版 48kHz）
 *   - 麦克风间距：50mm（对角麦克风）
 *   - 最大延迟采样点：3（24kHz × 0.05m / 343m/s ≈ 3.5）
 *
 * 4麦克风物理布局（同 AcousticEye）：
 *   CH0=MIC1=右  CH1=MIC2=左  CH2=MIC3=前/上  CH3=MIC4=后/下
 *   X轴: CH0↔CH1 (左右)
 *   Y轴: CH2↔CH3 (前后)
 */

#ifndef SOUND_LOCALIZER_H
#define SOUND_LOCALIZER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 配置常量 ── */
#define SL_SAMPLE_RATE          24000.0f   /* B板 ES7210 采样率 */
#define SL_MIC_DISTANCE_M       0.050f     /* 麦克风间距 50mm */
#define SL_SOUND_VELOCITY       343.0f     /* 声速 m/s */
#define SL_FFT_SIZE             256        /* 每帧采样点数（同 AcousticEye） */
#define SL_MAX_OFFSET           5          /* 24kHz物理极限≈3.5，设5给插值余量 */
#define SL_NUM_CHANNELS         4          /* 四麦克风 TDM 模式 */

/* 有效帧判定阈值（基于37.5dB增益实测）
 * 底噪特征：activity≈-2.85, RMS≈0.0014
 * 说话特征：activity≈-2.0, RMS≈0.009, ratio≈5x
 * 门限设计：固定门限+自适应门限双重保险（两者都需通过）*/
#define SL_MIN_ACTIVITY         (-2.5f)    /* 绝对保护：底噪-2.85被拒绝，远距离说话-2.0通过 */
#define SL_MIN_RAW_RMS          0.003f     /* 对应activity≈-2.5（仅作兜底参考）*/
#define SL_MAX_RAW_PEAK         0.98f
#define SL_MIN_PEAK_GCC         0.04f      /* GCC峰值阈值，配合ratio过滤 */
#define SL_MIN_PEAK_RATIO       1.5f       /* 主/次峰比值，底噪≈1.2被拒绝 */
#define SL_MIN_DIR_VECTOR       0.05f      /* 方向向量至少0.05 */

/* 自适应噪声门限（Noise Floor Tracker）*/
#define SL_NOISE_FLOOR_INIT     0.002f     /* 初始底噪RMS估计 */
#define SL_NOISE_FLOOR_ALPHA_DOWN  0.05f   /* rms<floor时快速追踪底噪 */
#define SL_NOISE_FLOOR_ALPHA_UP    0.003f  /* rms>floor时极慢上升（防声音污染）*/
#define SL_NOISE_FLOOR_UPDATE_MAX  1.5f    /* rms<floor×1.5时才更新（过滤声音）*/
#define SL_DETECT_RATIO         3.0f       /* 检测阈值=底噪×3倍，底噪ratio≈1x被拒绝 */

/* 角度稳定滤波参数 */
#define SL_ANGLE_WINDOW         6
#define SL_MIN_VALID_COUNT      3          /* 至少3帧一致才输出（原2帧太松）*/
#define SL_MAX_SPREAD_DEG       20.0f      /* 窗口内角度散布≤20°（原35°太松）*/
#define SL_ANGLE_SMOOTH_ALPHA   0.30f

/* 麦克风通道映射（4-ch TDM：CH0=右, CH1=左, CH2=前/上, CH3=后/下） */
#define SL_MIC_RIGHT            0   /* X轴A端（MIC1） */
#define SL_MIC_LEFT             1   /* X轴B端（MIC2） */
#define SL_MIC_FRONT            2   /* Y轴A端（MIC3） */
#define SL_MIC_BACK             3   /* Y轴B端（MIC4） */

#define SL_MIC_X_A              SL_MIC_RIGHT
#define SL_MIC_X_B              SL_MIC_LEFT
#define SL_MIC_Y_A              SL_MIC_FRONT
#define SL_MIC_Y_B              SL_MIC_BACK

/* 角度偏移与轴符号校准（同 AcousticEye） */
#define SL_AXIS_X_SIGN          1.0f
#define SL_AXIS_Y_SIGN          1.0f
#define SL_ANGLE_OFFSET_DEG     0.0f

/**
 * @brief 初始化声源定位模块（FFT 窗函数、角度滤波器）。
 *        必须在使用其他函数前调用。
 */
void sound_localizer_init(void);

/**
 * @brief 从 BoxAudioCodec::Read() 喂入四通道 TDM 数据。
 *
 * @param tdm_data  四通道交错 int16_t 数据，长度 = frames × 4
 * @param frames    帧数（每帧含 4 个采样值，每通道一个）
 *
 * 数据会写入环形缓冲区，后台任务会从中读取并计算角度。
 */
void sound_localizer_feed(const int16_t *tdm_data, int frames);

/**
 * @brief 获取最新稳定声源角度。
 *
 * @return float 0~360 度角度，-1 表示当前无有效定位。
 */
float sound_localizer_get_angle(void);

/**
 * @brief 获取角度的方向文字描述。
 *
 * @param angle 角度值（0~360 或 -1）
 * @return const char* 方向描述字符串
 */
const char *sound_localizer_get_direction(float angle);

/**
 * @brief 判断当前是否有有效的声源定位结果。
 *
 * @return true 如果最近 2 秒内有有效角度
 */
bool sound_localizer_is_active(void);

/**
 * @brief 启动后台 GCC-PHAT 计算任务。
 *
 * 任务会以约 20Hz 的频率从环形缓冲区读取数据并计算角度。
 * 栈大小 8192，优先级 5。
 */
void sound_localizer_start_task(void);

/**
 * @brief 获取最近一次 GCC-PHAT 计算的调试信息。
 *
 * @param activity_out   活动量输出口
 * @param peak_x_out     X轴GCC峰值
 * @param peak_y_out     Y轴GCC峰值
 * @param delay_x_out    X轴延迟
 * @param delay_y_out    Y轴延迟
 */
void sound_localizer_get_debug(float *activity_out, float *peak_x_out,
                                float *peak_y_out, float *delay_x_out,
                                float *delay_y_out);

#ifdef __cplusplus
}
#endif

#endif /* SOUND_LOCALIZER_H */
