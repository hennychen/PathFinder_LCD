/**
 * @file motion_engine.h
 * @brief 运动分析引擎 — 从 IMU 数据中检测车辆动态事件
 *
 * 功能：
 *   - 加减速检测 (急加速/急刹车)
 *   - 急转弯检测 (左转/右转)
 *   - 坡度与倾斜检测 (上坡/下坡/左倾/右倾)
 *   - 颠簸与碰撞检测
 *   - 高速行驶检测
 *   - 事件优先级仲裁（同一时刻只输出最高优先级事件）
 */
#ifndef MOTION_ENGINE_H
#define MOTION_ENGINE_H

#include "esp_err.h"
#include "drv_mpu9250.h"

/**
 * @brief 运动事件类型（按优先级从高到低排列）
 */
typedef enum {
    MOTION_IDLE = 0,        /**< 静止 */
    MOTION_CRUISE,          /**< 匀速行驶 */
    MOTION_ACCEL,           /**< 急加速 */
    MOTION_BRAKE,           /**< 急刹车 */
    MOTION_TURN_LEFT,       /**< 急左转 */
    MOTION_TURN_RIGHT,      /**< 急右转 */
    MOTION_UPHILL,          /**< 上坡 */
    MOTION_DOWNHILL,        /**< 下坡 */
    MOTION_TILT_LEFT,       /**< 左倾斜 */
    MOTION_TILT_RIGHT,      /**< 右倾斜 */
    MOTION_BUMPY,           /**< 颠簸 */
    MOTION_COLLISION,       /**< 碰撞/冲击 */
    MOTION_HIGH_SPEED,      /**< 高速行驶 */
    MOTION_EVENT_COUNT,     /**< 事件总数（用于边界检查） */
} motion_event_t;

/**
 * @brief IMU 校准状态机
 */
typedef enum {
    MOTION_CALIB_IDLE = 0,    /**< 空闲（未校准或已结束） */
    MOTION_CALIB_RUNNING,     /**< 正在采集数据 */
    MOTION_CALIB_DONE,        /**< 校准成功完成 */
    MOTION_CALIB_FAILED,      /**< 检测到剧烈晃动，失败 */
} motion_calib_state_t;

/**
 * @brief 初始化运动分析引擎（清零滑动窗口、重置校准）
 */
esp_err_t motion_engine_init(void);

/**
 * @brief 处理一帧 IMU 数据，返回当前的运动事件
 * @param imu 最新 IMU 数据
 * @return 当前检测到的运动事件（经过优先级仲裁+防抖）
 */
motion_event_t motion_engine_process(const mpu9250_data_t *imu);

/**
 * @brief 获取最新俯仰角和横滚角（度）
 */
void motion_engine_get_angles(float *pitch_deg, float *roll_deg);

/**
 * @brief 在静止水平状态校准零偏（旧接口，仅清零 bias）
 *        采集 1 秒数据取平均作为基准
 */
void motion_engine_calibrate(void);

/**
 * @brief 启动 IMU 六轴联合校准
 *        用户保持设备水平静止，采集 frames 帧数据取平均
 *        自动写入 NVS 持久化。结果通过 motion_engine_get_calib_state() 查询
 * @param frames 采集帧数（推荐 75 = 3秒 @25Hz）
 */
void motion_engine_start_calibration(int frames);

/**
 * @brief 查询当前校准状态
 */
motion_calib_state_t motion_engine_get_calib_state(void);

/**
 * @brief 查询校准进度百分比 (0-100)
 */
int motion_engine_get_calib_progress(void);

/**
 * @brief 从 NVS 加载 bias（motion_engine_init 内自动调用）
 * @return ESP_OK 成功，ESP_ERR_NVS_NOT_FOUND 无历史数据
 */
esp_err_t motion_engine_load_bias_from_nvs(void);

/**
 * @brief 获取当前运动事件
 */
motion_event_t motion_engine_get_event(void);

/**
 * @brief 获取运动事件的优先级数值
 */
int motion_engine_get_priority(motion_event_t evt);

#endif /* MOTION_ENGINE_H */
