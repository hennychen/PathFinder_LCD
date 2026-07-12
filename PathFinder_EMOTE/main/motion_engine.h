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
#include "drv_mpu6050.h"

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
 * @brief 初始化运动分析引擎（清零滑动窗口、重置校准）
 */
esp_err_t motion_engine_init(void);

/**
 * @brief 处理一帧 IMU 数据，返回当前的运动事件
 * @param imu 最新 IMU 数据
 * @return 当前检测到的运动事件（经过优先级仲裁+防抖）
 */
motion_event_t motion_engine_process(const mpu6050_data_t *imu);

/**
 * @brief 获取最新俯仰角和横滚角（度）
 */
void motion_engine_get_angles(float *pitch_deg, float *roll_deg);

/**
 * @brief 在静止水平状态校准零偏
 *        采集 1 秒数据取平均作为基准
 */
void motion_engine_calibrate(void);

/**
 * @brief 获取当前运动事件
 */
motion_event_t motion_engine_get_event(void);

/**
 * @brief 获取运动事件的优先级数值
 */
int motion_engine_get_priority(motion_event_t evt);

#endif /* MOTION_ENGINE_H */
