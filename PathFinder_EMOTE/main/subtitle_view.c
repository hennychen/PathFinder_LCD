/**
 * @file subtitle_view.c
 * @brief 中文字幕显示模块（自动换行 + 滚动版）
 *
 * 使用 lv_label + LV_LABEL_LONG_WRAP 实现自动换行，
 * 容器可垂直滚动查看溢出内容。
 * 字体：font_hzk_32（GB2312一级常用字3755字，32px 2bpp）。
 *
 * 布局（480×480 圆形屏）：
 *   字幕容器底部对齐，宽度380px，最大高度160px（约4行32px文字）
 *   文字超宽自动换行，超出容器可滚动，新字幕自动滚到底部。
 */
#include "subtitle_view.h"
#include "font_hzk_32.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "subtitle";

#define SUBTITLE_MAX_LEN     240
#define SUBTITLE_TIMEOUT_MS  5000
#define SUBTITLE_FADE_MS     500

/* ── 容器布局（适配 480×480 圆形屏安全区） ──
 * 宽度 380px 约在 Y=400 处圆形弦长 356px 以内
 * 最大高度 160px ≈ 8 行 16px 文字
 * Y偏移 80 → 底部约 Y=320+160=480（贴近屏底） */
#define SUBTITLE_WIDTH       380
#define SUBTITLE_MAX_HEIGHT  160
#define SUBTITLE_Y_OFFSET    80

/* ── 全局状态 ── */
static struct {
    lv_obj_t *container;     /* 半透明黑底滚动容器 */
    lv_obj_t *label;         /* 文字标签（自动换行） */
    int64_t  last_update_us;
    bool     visible;
    bool     fading;
} s_sub;

/* ──────────────── 标点 sanitize ──────────────── */

/**
 * @brief 预处理字幕文本：将 font_hzk_16 不支持的特殊 Unicode 标点
 *        替换为已支持的等价字符，避免矩形占位符（tofu）。
 *        font_hzk_16 已覆盖 GB2312 全部标点和全角符号，
 *        仅需处理 Unicode 特殊符号区间（U+2010-U+2027）。
 */
static void subtitle_sanitize(char *s)
{
    if (!s) return;
    char *p = s;
    while (*p) {
        unsigned char c  = (unsigned char)*p;
        unsigned char n1 = (unsigned char)*(p + 1);
        unsigned char n2 = (unsigned char)*(p + 2);
        if (c == 0xE2 && n1 == 0x80) {
            /* U+201x / U+202x 区间 */
            if      (n2 == 0x94)               { *p = '-';  memmove(p+1, p+3, strlen((char*)(p+3)) + 1); p++; }  /* — → - */
            else if (n2 == 0x9C || n2 == 0x9D) { *p = '\"'; memmove(p+1, p+3, strlen((char*)(p+3)) + 1); p++; }  /* “” → \" */
            else if (n2 == 0x98 || n2 == 0x99) { *p = '\''; memmove(p+1, p+3, strlen((char*)(p+3)) + 1); p++; }  /* ‘’ → ' */
            else if (n2 == 0xA6)               { /* … → ...（3字节→3字节，长度不变） */
                *p = '.'; *(p+1) = '.'; *(p+2) = '.';
                p += 3;
            }
            else p += 3;  /* 跳过其他 U+20xx 特殊字符 */
        }
        else {
            p++;
        }
    }
}

/* ──────────────── API 实现 ──────────────── */

void subtitle_view_init(lv_obj_t *parent)
{
    if (!parent) return;

    /* 半透明黑底滚动容器 — 底部居中 */
    s_sub.container = lv_obj_create(parent);
    lv_obj_set_size(s_sub.container, SUBTITLE_WIDTH, SUBTITLE_MAX_HEIGHT);
    lv_obj_align(s_sub.container, LV_ALIGN_CENTER, 0, SUBTITLE_Y_OFFSET);
    lv_obj_set_style_bg_color(s_sub.container, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_sub.container, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_sub.container, 0, 0);
    lv_obj_set_style_radius(s_sub.container, 16, 0);
    lv_obj_set_style_pad_all(s_sub.container, 8, 0);
    lv_obj_clear_flag(s_sub.container, LV_OBJ_FLAG_CLICKABLE);

    /* 启用垂直滚动 — 文字超出容器高度时可上下滑动 */
    lv_obj_set_scrollbar_mode(s_sub.container, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(s_sub.container, LV_DIR_VER);

    /* 文字标签 — 自动换行 */
    s_sub.label = lv_label_create(s_sub.container);
    lv_label_set_long_mode(s_sub.label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_sub.label, SUBTITLE_WIDTH - 16);  /* 减去左右 padding */
    lv_obj_set_style_text_font(s_sub.label, &font_hzk_32, 0);
    lv_obj_set_style_text_color(s_sub.label, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(s_sub.label, 0, 0);
    lv_obj_set_style_text_line_space(s_sub.label, 4, 0);
    lv_label_set_text(s_sub.label, "");
    lv_obj_align(s_sub.label, LV_ALIGN_TOP_MID, 0, 0);

    /* 初始隐藏 */
    lv_obj_fade_out(s_sub.container, 0, 0);
    s_sub.visible = false;
    s_sub.fading = false;
    s_sub.last_update_us = 0;

    ESP_LOGI(TAG, "字幕模块初始化 (GB2312 32px 2bpp, 自动换行+滚动, 宽%d×高%d)",
             SUBTITLE_WIDTH, SUBTITLE_MAX_HEIGHT);
}

void subtitle_view_update(const char *text)
{
    if (!s_sub.container || !s_sub.label || !text || !*text) return;

    /* 显示容器 */
    if (!s_sub.visible) {
        lv_obj_fade_in(s_sub.container, 300, 0);
        s_sub.visible = true;
        s_sub.fading = false;
    }

    /* 预处理标点 */
    char buf[SUBTITLE_MAX_LEN + 8];
    strncpy(buf, text, SUBTITLE_MAX_LEN);
    buf[SUBTITLE_MAX_LEN] = '\0';
    subtitle_sanitize(buf);

    /* 更新文字 — LV_LABEL_LONG_WRAP 会自动换行 */
    lv_label_set_text(s_sub.label, buf);

    /* 自动滚动到底部（显示最新文字） */
    lv_coord_t scroll_y = lv_obj_get_scroll_bottom(s_sub.container);
    if (scroll_y > 0) {
        lv_obj_scroll_to_y(s_sub.container, LV_COORD_MAX, LV_ANIM_ON);
    }

    s_sub.last_update_us = esp_timer_get_time();
    ESP_LOGI(TAG, "字幕更新: %s", buf);
}

/* 字幕淡出完成回调 */
static void subtitle_fade_done_cb(lv_timer_t *timer)
{
    s_sub.visible = false;
    s_sub.fading = false;
    if (timer) lv_timer_del(timer);
}

void subtitle_view_tick(void)
{
    if (!s_sub.container || !s_sub.visible || s_sub.fading) return;
    if (s_sub.last_update_us == 0) return;

    int64_t elapsed_ms = (esp_timer_get_time() - s_sub.last_update_us) / 1000;
    if (elapsed_ms > SUBTITLE_TIMEOUT_MS) {
        s_sub.fading = true;
        lv_obj_fade_out(s_sub.container, SUBTITLE_FADE_MS, 0);
        s_sub.last_update_us = 0;
        lv_timer_t *timer = lv_timer_create(subtitle_fade_done_cb,
                                            SUBTITLE_FADE_MS + 50, NULL);
        if (timer) lv_timer_set_repeat_count(timer, 1);
    }
}
