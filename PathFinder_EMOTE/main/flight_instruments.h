/**
 * @file flight_instruments.h
 * @brief 仿飞行仪表盘图形化页面 — 姿态指引仪 + 指南针/海拔/气压
 *
 * 双页滑动切换：
 *   第1页 — 全屏姿态指引仪 (Artificial Horizon)
 *   第2页 — 指南针 + 海拔计 + 气压计
 *
 * 数据源（只读）：
 *   motion_engine_get_angles() → pitch / roll
 *   sensor_manager_get_env()   → altitude / pressure
 *   sensor_manager_get_imu()   → 预留磁力计接入
 *
 * 线程安全：所有 LVGL 操作在 lvgl_lock 内执行（与 overlay_update 同一线程）
 */
#ifndef FLIGHT_INSTRUMENTS_H
#define FLIGHT_INSTRUMENTS_H

#include "lvgl.h"
#include <stdbool.h>

/**
 * @brief 创建仪表覆盖层（默认隐藏），在 ui_create() 中调用
 * @param parent 父级 LVGL 对象（通常为屏幕根对象）
 */
void flight_instruments_create(lv_obj_t *parent);

/**
 * @brief 显示仪表页（隐藏 EAF 表情页）
 */
void flight_instruments_show(void);

/**
 * @brief 隐藏仪表页（恢复 EAF 表情页）
 */
void flight_instruments_hide(void);

/**
 * @brief 检查仪表页当前是否可见
 * @return true 如果仪表页正在显示
 */
bool flight_instruments_is_visible(void);

/**
 * @brief 周期数据更新（在 lvgl_task 中调用，内部 20Hz 限速）
 *        仅在仪表页可见时实际执行更新
 */
void flight_instruments_update(void);

#endif /* FLIGHT_INSTRUMENTS_H */
