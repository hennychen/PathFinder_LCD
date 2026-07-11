/**
 * @file emote_engine.h
 * @brief 表情联动引擎 — 基于传感器数据显示对应表情
 *
 * 设计理念：
 *   不使用运动事件驱动表情切换（运动事件频繁翻转导致抖动/花屏），
 *   而是根据各传感器数据（温度、湿度、UV、气压、倾角）综合评估
 *   显示最合适的表情。传感器数据变化缓慢 → 表情自然稳定。
 *
 * 线程安全：
 *   emote_engine_tick() 在 LVGL 线程调用，读取传感器快照并评估
 */
#ifndef EMOTE_ENGINE_H
#define EMOTE_ENGINE_H

#include "esp_err.h"
#include "lvgl.h"

/**
 * @brief 初始化表情引擎，绑定 EAF widget
 * @param eaf_obj LVGL EAF 控件指针
 * @param name_label 名称标签指针 (可 NULL)
 */
esp_err_t emote_engine_init(lv_obj_t *eaf_obj, lv_obj_t *name_label);

/**
 * @brief LVGL 线程周期调用
 *        读取传感器快照 → 评估表情规则 → 按需切换
 *        内部有 5 秒评估间隔，不会频繁切换
 */
void emote_engine_tick(void);

/**
 * @brief 手动轮播切换（点击屏幕时调用，在 LVGL 线程中）
 *        手动切换后 10 秒内不自动评估
 */
void emote_engine_manual_next(void);

/**
 * @brief 获取当前正在播放的表情名称
 */
const char *emote_engine_get_current_name(void);

#endif /* EMOTE_ENGINE_H */
/**
 * @file emote_engine.h
 * @brief 表情联动引擎 — 基于传感器数据显示对应表情
 *
 * 设计理念：
 *   不使用运动事件驱动表情切换（运动事件频繁翻转导致抖动/花屏），
 *   而是根据各传感器数据（温度、湿度、UV、气压、倾角）综合评估
 *   显示最合适的表情。传感器数据变化缓慢 → 表情自然稳定。
 *
 * 线程安全：
 *   emote_engine_tick() 在 LVGL 线程调用，读取传感器快照并评估
 */
#ifndef EMOTE_ENGINE_H
#define EMOTE_ENGINE_H

#include "esp_err.h"
#include "lvgl.h"

/**
 * @brief 初始化表情引擎，绑定 EAF widget
 * @param eaf_obj LVGL EAF 控件指针
 * @param name_label 名称标签指针 (可 NULL)
 */
esp_err_t emote_engine_init(lv_obj_t *eaf_obj, lv_obj_t *name_label);

/**
 * @brief LVGL 线程周期调用
 *        读取传感器快照 → 评估表情规则 → 按需切换
 *        内部有 5 秒评估间隔，不会频繁切换
 */
void emote_engine_tick(void);

/**
 * @brief 手动轮播切换（点击屏幕时调用，在 LVGL 线程中）
 *        手动切换后 10 秒内不自动评估
 */
void emote_engine_manual_next(void);

/**
 * @brief 获取当前正在播放的表情名称
 */
const char *emote_engine_get_current_name(void);

#endif /* EMOTE_ENGINE_H */
/**
 * @file emote_engine.h
 * @brief 表情联动引擎 — 将运动事件映射到 EAF 表情动画
 *
 * 线程安全设计：
 *   - emote_engine_on_motion() 在 imu_task 被调用 → 只设置 pending_event
 *   - emote_engine_tick() 在 LVGL 线程被调用 → 执行实际表情切换
 */
#ifndef EMOTE_ENGINE_H
#define EMOTE_ENGINE_H

#include "esp_err.h"
#include "lvgl.h"
#include "motion_engine.h"

/**
 * @brief 初始化表情引擎，绑定 EAF widget
 * @param eaf_obj LVGL EAF 控件指针
 * @param name_label 名称标签指针 (可 NULL)
 */
esp_err_t emote_engine_init(lv_obj_t *eaf_obj, lv_obj_t *name_label);

/**
 * @brief 运动事件回调（在 imu_task 中调用，非 LVGL 线程）
 *        只更新 pending_event 标志，不执行 LVGL 操作
 */
void emote_engine_on_motion(motion_event_t evt);

/**
 * @brief LVGL 线程周期调用（在 LVGL 定时器中）
 *        处理 pending_event、检查 hold_ms 过期、切换表情
 */
void emote_engine_tick(void);

/**
 * @brief 手动轮播切换（点击屏幕时调用，在 LVGL 线程中）
 */
void emote_engine_manual_next(void);

/**
 * @brief 获取当前正在播放的表情名称
 */
const char *emote_engine_get_current_name(void);

#endif /* EMOTE_ENGINE_H */
