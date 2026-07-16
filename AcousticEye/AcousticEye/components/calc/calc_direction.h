#ifndef __CALC_DIRECTION_H
#define __CALC_DIRECTION_H

/**
 * @file calc_direction.h
 * @brief 四麦克风声源定位算法公开接口。
 *
 * 本模块负责初始化 ES7210 四通道麦克风、读取 I2S/TDM 音频数据，
 * 并使用 GCC-PHAT 算法估计声源方向角。
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Include system header files -----------------------------------------------*/
/* Include user header files -------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/**
 * @brief 每次参与一次声源定位计算的单通道采样点数。
 *
 * 该值同时作为 FFT 长度使用。数值越大，频域分辨率和稳定性通常越好，
 * 但计算量和响应延迟也会增加。
 */
#define PDM_SAMPLE_SIZE 256

/**
 * @brief 每次从 I2S 读取的分块采样点数。
 *
 * i2s_read_mics() 会循环读取多个分块，直到填满 PDM_SAMPLE_SIZE。
 */
#define I2S_READ_CHUNK_SAMPLES 64

/**
 * @brief 圆周率常量。
 */
#define PI 3.14159265358979323846
//#define FACTORY_CHECK_MODE

/* Exported types ------------------------------------------------------------*/
/**
 * @brief 兼容旧寄存器协议的寄存器索引。
 *
 * 当前工程主要通过函数接口读取角度和调试信息，这组枚举保留给可能的
 * 外部寄存器读写协议使用。
 */
enum {
    ENUM_REG_DEG_H = 0,  /**< 最终角度高字节。 */
    ENUM_REG_DEG_L,      /**< 最终角度低字节。 */
    ENUM_REG_DEG_ORG_H,  /**< 原始角度高字节。 */
    ENUM_REG_DEG_ORG_L,  /**< 原始角度低字节。 */
    ENUM_REG_ACT_H,      /**< 活动量高字节。 */
    ENUM_REG_ACT_L,      /**< 活动量低字节。 */
    ENUM_REG_REFRESH,    /**< 刷新触发寄存器。 */
    ENUM_REG_ID          /**< 设备 ID 寄存器。 */
};

/**
 * @brief 声源定位调试信息。
 *
 * calcAngle() 会在计算过程中更新该结构体，用于观察响度、GCC 峰值、
 * 延迟、原始角度、最终角度以及无效原因。
 */
typedef struct {
    float raw_levels[4];           /**< 四通道去直流 RMS。 */
    float raw_levels_high16[4];    /**< 兼容旧调试字段：高 16 位 RMS。 */
    float slot_low16_mean_abs[4];  /**< 兼容旧调试字段：低 16 位平均绝对值。 */
    float slot_high16_mean_abs[4]; /**< 兼容旧调试字段：高 16 位平均绝对值。 */
    float activity_value;          /**< 当前帧活动量，通常为平均 RMS 的 log10。 */
    float peak_ew;                 /**< X 轴麦克风对 GCC 峰值。 */
    float peak_ns;                 /**< Y 轴麦克风对 GCC 峰值。 */
    float delay_ew;                /**< X 轴麦克风对估计延迟，单位为采样点。 */
    float delay_ns;                /**< Y 轴麦克风对估计延迟，单位为采样点。 */
    float axis_x;                  /**< 归一化后的 X 轴方向分量。 */
    float axis_y;                  /**< 归一化后的 Y 轴方向分量。 */
    float angle_raw;               /**< 未经过稳定滤波的原始角度。 */
    float angle_final;             /**< 经过稳定滤波后的最终角度，-1 表示无效。 */
    int16_t sample_low16[4];       /**< 兼容旧调试字段：低 16 位样本。 */
    int16_t sample_high16[4];      /**< 兼容旧调试字段：高 16 位样本。 */
    uint32_t raw_words[8];         /**< 兼容旧调试字段：原始采样字。 */
    int channel_map_id;            /**< 兼容旧调试字段：通道映射 ID。 */
    int channel_map_count;         /**< 兼容旧调试字段：通道映射数量。 */
    int channel_map[4];            /**< 兼容旧调试字段：通道映射表。 */
    int axis_pair_mode;            /**< 兼容旧调试字段：轴对模式。 */
    int axis_pair_count;           /**< 兼容旧调试字段：轴对模式数量。 */
    int axis_pair[4];              /**< 兼容旧调试字段：轴对通道。 */
    int low_confidence;            /**< 峰值比不足，GCC 置信度偏低。 */
    int weak_peak;                 /**< GCC 峰值过弱。 */
    int weak_axis;                 /**< 方向向量过小，方向不明显。 */
    int clipped_delay;             /**< 延迟贴近搜索边界，可能已截断。 */
    int jump_unreliable;           /**< 兼容旧调试字段：角度跳变不可靠标志。 */
} calc_debug_info_t;

/* Exported enum tag ---------------------------------------------------------*/
/* Exported struct/union tag -------------------------------------------------*/
/* Exported variables --------------------------------------------------------*/
/* Exported function prototypes ----------------------------------------------*/
/**
 * @brief 初始化 FFT 窗函数和角度滤波器。
 *
 * @param samplingRate 采样率，单位 Hz。当前实现内部使用固定配置，参数保留用于接口兼容。
 */
void prepareWindow(float samplingRate);

/**
 * @brief 计算当前音频帧的声源方向角。
 *
 * @return float 0~360 度的稳定角度；返回 -1 表示当前帧无有效方向。
 */
float calcAngle(void);

/**
 * @brief 计算四通道平均原始声压等级。
 *
 * @return float 四通道去直流 RMS 的平均值。
 */
float calcRawLevel(void);

/**
 * @brief 计算四个麦克风通道各自的原始声压等级。
 *
 * @param levels 输出数组，长度必须至少为 4。
 */
void calcRawLevels(float levels[4]);

/**
 * @brief 打印麦克风 RMS 调试信息。
 *
 * 当前实现为了兼容旧接口保留为空函数。
 */
void print_mic_rms_debug(void);

/**
 * @brief 初始化 ES7210 和 I2S/TDM 麦克风采集通道。
 */
void initI2SMics(void);

/**
 * @brief 设置声音活动量阈值。
 *
 * @param fvalue 活动量阈值。当前 activity 小于该值时，calcAngle() 返回无效。
 */
void setActivityValue(float fvalue);

/**
 * @brief 读取一帧四通道麦克风采样数据。
 *
 * 读取结果写入 main.c 中定义的 i2s_mic_data 全局数组。
 */
void i2s_read_mics(void);

/**
 * @brief 获取最近一次 calcAngle() 的调试信息。
 *
 * @param info 输出结构体指针，不能为空。
 */
void getCalcDebugInfo(calc_debug_info_t *info);

/**
 * @brief 设置通道映射 ID。
 *
 * @param mapId 通道映射编号。当前实现为兼容接口，暂不改变实际映射。
 */
void setChannelMapId(int mapId);

/**
 * @brief 获取当前通道映射 ID。
 *
 * @return int 当前固定返回 0。
 */
int getChannelMapId(void);

/**
 * @brief 获取可用通道映射数量。
 *
 * @return int 当前固定返回 1。
 */
int getChannelMapCount(void);

/**
 * @brief 设置轴对模式。
 *
 * @param mode 轴对模式编号。当前实现为兼容接口，暂不改变实际轴对。
 */
void setAxisPairMode(int mode);

/**
 * @brief 获取当前轴对模式。
 *
 * @return int 当前固定返回 0。
 */
int getAxisPairMode(void);

/**
 * @brief 获取可用轴对模式数量。
 *
 * @return int 当前固定返回 1。
 */
int getAxisPairModeCount(void);

#ifdef __cplusplus
}
#endif

#endif /* __CALC_DIRECTION_H */
/***************************************************************END OF FILE****/
