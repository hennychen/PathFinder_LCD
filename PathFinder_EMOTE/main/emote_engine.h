/**
 * @file emote_engine.h
 * @brief 表情联动引擎 — 传感器数据 + 对话状态 + LLM情感 三级驱动
 *
 * 设计理念：
 *   不使用运动事件驱动表情切换（运动事件频繁翻转导致抖动/花屏），
 *   而是根据各传感器数据（温度、湿度、UV、气压、倾角）综合评估
 *   显示最合适的表情。传感器数据变化缓慢 → 表情自然稳定。
 *
 * 优先级（从高到低）：
 *   1. 手动覆盖（manual_next，10秒）
 *   2. 对话状态覆盖（on_dialogue，5秒，由 B板小智对话驱动）
 *   3. LLM 情感覆盖（on_emotion，5秒，由 B板小智 LLM emotion 驱动）
 *   4. 传感器评估（默认，5秒间隔）
 *
 * 线程安全：
 *   emote_engine_tick() 在 LVGL 线程调用，读取传感器快照并评估
 *   emote_engine_on_dialogue() / on_emotion() 必须在 LVGL 线程调用
 */
#ifndef EMOTE_ENGINE_H
#define EMOTE_ENGINE_H

#include "esp_err.h"
#include "lvgl.h"
#include <stdint.h>

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

/**
 * @brief 对话状态触发（必须在 LVGL 线程中调用）
 *        覆盖传感器评估 5 秒，对话状态优先
 * @param state 0=idle 1=listening 2=speaking 3=connecting
 */
void emote_engine_on_dialogue(uint8_t state);

/**
 * @brief LLM 情感触发（必须在 LVGL 线程中调用）
 *        优先级介于对话状态和传感器之间
 * @param emotion ASCII 字符串: happy/sad/neutral/angry/surprised/...
 */
void emote_engine_on_emotion(const char *emotion);

#endif /* EMOTE_ENGINE_H */
