/**
 * @file subtitle_view.c
 * @brief 中文字幕显示模块实现（2倍放大版）
 *
 * 原理：LVGL 8.3 label 不支持 transform_zoom，因此采用
 *       canvas（绘制16px原始文字）+ img（zoom=512 显示2倍放大）方案。
 *       加粗通过在 canvas 上两次绘制偏移1像素实现。
 */
#include "subtitle_view.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "subtitle";

#define SUBTITLE_MAX_LEN     240
#define SUBTITLE_TIMEOUT_MS  5000
#define SUBTITLE_FADE_MS     500

/* ── Canvas 尺寸（绘制原始 16px CJK 文字，隐藏） ──
 * 宽度180 容纳约 11 个 16px 汉字；放大2倍后显示宽度360 */
#define CANVAS_W  180
#define CANVAS_H  22

/* ── 容器布局（适配 480×480 圆形屏安全区，向下移） ──
 * Y偏移130 → 字幕中心约 Y370，可用宽度 ≈ 403px */
#define SUBTITLE_WIDTH     380
#define SUBTITLE_HEIGHT    56
#define SUBTITLE_Y_OFFSET  130

/* 静态 canvas 缓冲区（RAM: 180×22×4 ≈ 15.8KB） */
static lv_color_t s_canvas_buf[LV_CANVAS_BUF_SIZE_TRUE_COLOR(CANVAS_W, CANVAS_H)];

/* ── 全局状态 ── */
static struct {
    lv_obj_t *container;     /* 半透明黑底容器 */
    lv_obj_t *canvas;        /* 隐藏的绘制层（16px原始） */
    lv_obj_t *img;           /* 显示层（2倍放大） */
    int64_t  last_update_us;
    bool     visible;
    bool     fading;
} s_sub;

/* ──────────────── 栀点 sanitize ──────────────── */

#define REPLACE1(p, rep) do { *(p) = (rep); memmove((p)+1, (p)+3, strlen((char*)(p)+3) + 1); } while(0)
#define REPLACE3(p, a, b, c) do { *(p) = (a); *((p)+1) = (b); *((p)+2) = (c); } while(0)

/**
 * @brief 预处理字幕文本：将 SimSun CJK 字体不支持的中文标点
 *        替换为已支持的等价字符，避免矩形占位符（tofu）。
 */
static void subtitle_sanitize(char *s)
{
    if (!s) return;
    char *p = s;
    while (*p) {
        unsigned char c  = (unsigned char)*p;
        unsigned char n1 = (unsigned char)*(p + 1);
        unsigned char n2 = (unsigned char)*(p + 2);
        if (c == 0xEF && n1 == 0xBC) {
            /* U+FFxx 全角ASCII */
            if      (n2 == 0x81) { REPLACE1(p, '!');  p++; }  /* ！ */
            else if (n2 == 0x9F) { REPLACE1(p, '?');  p++; }  /* ？ */
            else if (n2 == 0x9A) { REPLACE1(p, ':');  p++; }  /* ： */
            else if (n2 == 0x9B) { REPLACE1(p, ';');  p++; }  /* ； */
            else p += 3;
        }
        else if (c == 0xE2 && n1 == 0x80) {
            /* U+201x / U+202x 区间 */
            if      (n2 == 0x9C || n2 == 0x9D) { REPLACE1(p, '\"'); p++; }  /* “” */
            else if (n2 == 0x98 || n2 == 0x99) { REPLACE1(p, '\''); p++; }  /* ‘’ */
            else if (n2 == 0x94)               { REPLACE1(p, '-');  p++; }  /* — */
            else if (n2 == 0xA6) {
                /* … → ... */
                REPLACE3(p, '.', '.', '.');
                p += 3;
            }
            else p += 3;
        }
        else p++;
    }
}

/* ──────────────── API 实现 ──────────────── */

void subtitle_view_init(lv_obj_t *parent)
{
    if (!parent) return;

    /* 半透明黑底容器 — 向下移到 Y≈370 */
    s_sub.container = lv_obj_create(parent);
    lv_obj_set_size(s_sub.container, SUBTITLE_WIDTH, SUBTITLE_HEIGHT);
    lv_obj_align(s_sub.container, LV_ALIGN_CENTER, 0, SUBTITLE_Y_OFFSET);
    lv_obj_set_style_bg_color(s_sub.container, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_sub.container, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_sub.container, 0, 0);
    lv_obj_set_style_radius(s_sub.container, 28, 0);
    lv_obj_set_style_pad_all(s_sub.container, 4, 0);
    lv_obj_clear_flag(s_sub.container, LV_OBJ_FLAG_SCROLLABLE);

    /* Canvas — 绘制原始 16px 文字（隐藏，作为 img 数据源） */
    s_sub.canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(s_sub.canvas, s_canvas_buf,
                         CANVAS_W, CANVAS_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_add_flag(s_sub.canvas, LV_OBJ_FLAG_HIDDEN);
    lv_canvas_fill_bg(s_sub.canvas, lv_color_black(), LV_OPA_TRANSP);

    /* Img — 显示放大2倍的文字 */
    s_sub.img = lv_img_create(s_sub.container);
    lv_img_set_src(s_sub.img, lv_canvas_get_img(s_sub.canvas));
    lv_img_set_zoom(s_sub.img, 512);                        /* 256=1.0x, 512=2.0x */
    lv_img_set_pivot(s_sub.img, CANVAS_W / 2, CANVAS_H / 2);
    lv_obj_center(s_sub.img);

    /* 初始隐藏 */
    lv_obj_clear_flag(s_sub.container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_fade_out(s_sub.container, 0, 0);
    s_sub.visible = false;
    s_sub.fading = false;
    s_sub.last_update_us = 0;

    ESP_LOGI(TAG, "字幕模块初始化完成 (CJK 16px × 2x zoom = 32px视觉)");
}

void subtitle_view_update(const char *text)
{
    if (!s_sub.container || !text || !*text) return;

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

    /* 清空 canvas（透明） */
    lv_canvas_fill_bg(s_sub.canvas, lv_color_black(), LV_OPA_TRANSP);

    /* 绘制 16px 文字到 canvas — 两次偏移1px叠加实现加粗 */
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.color = lv_color_white();
    dsc.font  = &lv_font_simsun_16_cjk;
    dsc.align = LV_TEXT_ALIGN_CENTER;

    lv_canvas_draw_text(s_sub.canvas, 0, 2, CANVAS_W, &dsc, buf);
    lv_canvas_draw_text(s_sub.canvas, 1, 2, CANVAS_W, &dsc, buf);  /* +1px 加粗 */

    /* 强制 img 刷新（canvas 内容变化后 img 不会自动 invalidate） */
    lv_obj_invalidate(s_sub.img);

    s_sub.last_update_us = esp_timer_get_time();
    ESP_LOGI(TAG, "字幕更新(2x): %s", buf);
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
