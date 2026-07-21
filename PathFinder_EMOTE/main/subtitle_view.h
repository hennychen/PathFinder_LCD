/**
 * @file subtitle_view.h
 * @brief 中文字幕显示模块 — 使用 font_hzk_16（GB2312一级3755字）
 *
 * 在圆形屏底部显示半透明字幕条，支持中英文混合 UTF-8 文本。
 * 文字超宽自动换行，多行可滚动，5秒无新字幕自动淡出。
 *
 * 线程安全：
 *   所有函数必须在 LVGL 线程中调用
 */
#ifndef SUBTITLE_VIEW_H
#define SUBTITLE_VIEW_H

#include "lvgl.h"

/**
 * @brief 创建字幕控件（在 LVGL 线程中调用一次）
 * @param parent 父屏幕对象
 */
void subtitle_view_init(lv_obj_t *parent);

/**
 * @brief 更新字幕文本（在 LVGL 线程中调用）
 *        自动重置淡出计时器（5秒）
 * @param text UTF-8 文本（支持中文/英文混合，最多 240 字节）
 */
void subtitle_view_update(const char *text);

/**
 * @brief LVGL 线程周期调用，处理自动淡出
 */
void subtitle_view_tick(void);

#endif /* SUBTITLE_VIEW_H */
