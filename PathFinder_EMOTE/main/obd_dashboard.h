/**
 * @file obd_dashboard.h
 * @brief OBD 车辆仪表盘页面 — 中央转速表 + 车速/水温/电压/节气门/进气温
 *
 * 单页融合布局（圆形屏安全区内）：
 *   中央 — lv_arc 静态刻度转速表 + transform_angle 指针 + RPM 大数字
 *   顶部 — 车速大字 (km/h)
 *   底部 — 水温条 + 电压/节气门/进气温小字
 *
 * 数据源（只读）：obd_manager_get()
 * 离线态：整体半透明 + 中央显示 "OBD 未连接"
 *
 * 线程安全：所有 LVGL 操作在 lvgl_lock 内执行（与 flight_instruments 同线程）
 */
#ifndef OBD_DASHBOARD_H
#define OBD_DASHBOARD_H

#include "lvgl.h"
#include <stdbool.h>

/**
 * @brief 创建 OBD 仪表覆盖层（默认隐藏），在 ui_create() 中调用
 * @param parent 父级 LVGL 对象（通常为屏幕根对象）
 */
void obd_dashboard_create(lv_obj_t *parent);

/**
 * @brief 显示 OBD 仪表页（暂停 EAF 表情动画）
 */
void obd_dashboard_show(void);

/**
 * @brief 隐藏 OBD 仪表页（恢复 EAF 表情动画）
 */
void obd_dashboard_hide(void);

/**
 * @brief 检查 OBD 仪表页当前是否可见
 * @return true 如果仪表页正在显示
 */
bool obd_dashboard_is_visible(void);

/**
 * @brief 周期数据更新（在 lvgl_task 中调用，内部三级节流）
 *        仅在仪表页可见时实际执行更新
 */
void obd_dashboard_update(void);

#endif /* OBD_DASHBOARD_H */
