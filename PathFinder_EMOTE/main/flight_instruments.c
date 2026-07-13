/**
 * @file flight_instruments.c
 * @brief 仿飞行仪表盘图形化页面 — 姿态指引仪 + 指南针/海拔/气压
 *
 * 双页滑动切换：
 *   第1页 — 全屏姿态指引仪 (Artificial Horizon)
 *   第2页 — 指南针 + 海拔计 + 气压计
 *
 * 数据源（只读）：motion_engine / sensor_manager
 * 线程安全：所有 LVGL 操作在 lvgl_lock 内执行
 */
#include <math.h>
#include <stdio.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "sensor_manager.h"
#include "motion_engine.h"
#include "flight_instruments.h"

static const char *TAG = "flight_inst";

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ===================== 常量 ===================== */
#define FI_SCREEN_SIZE         480
#define FI_SCREEN_CENTER       240

#define FI_UPDATE_PERIOD_US    100000    /* 10Hz (100ms) — 降低 PSRAM 带宽争抢 */
#define FI_ENV_PERIOD_US       1000000   /* 1Hz */

#define FI_PITCH_SCALE         3         /* px/度 (canvas 内) */
#define FI_PITCH_MAX           45.0f     /* 俯仰钳制范围 (±45°) */
#define FI_ROLL_MAX            60.0f     /* 横滚钳制范围 (±60°) */

#define FI_HORIZON_SIZE        520
#define FI_CLIP_SIZE           400
#define FI_CANVAS_SIZE         200       /* 姿态画布尺寸 (200x200, 2x zoom 显示为 400x400) */

#define FI_ROLL_ARC_RADIUS     185       /* 横滚刻度标签半径 */

/* 颜色 */
#define COLOR_SKY        lv_color_hex(0x1A6FC4)
#define COLOR_GROUND     lv_color_hex(0x8B5E3C)
#define COLOR_AIRCRAFT   lv_color_hex(0xFFD700)
#define COLOR_NORTH      lv_color_hex(0xFF5050)
#define COLOR_GRAY       lv_color_hex(0xAAAAAA)
#define COLOR_ALT_TICK   lv_color_hex(0x00B4FF)
#define COLOR_BAR_TICK   lv_color_hex(0x00FF88)
#define COLOR_NEEDLE     lv_color_hex(0xFF5050)
#define COLOR_DIAL_BG    lv_color_hex(0x1A1A2E)

/* ===================== 模块状态 ===================== */
static lv_obj_t *s_overlay     = NULL;
static lv_obj_t *s_page_att    = NULL;
static lv_obj_t *s_page_detail = NULL;   /* 详细数值页 */
static bool      s_visible     = false;
static bool      s_detail_mode = false;   /* true=详细页, false=姿态页 */

/* 姿态页对象 */
static lv_obj_t   *s_canvas       = NULL;
static lv_color_t *s_canvas_buf   = NULL;
static lv_obj_t *s_hud_pitch     = NULL;
static lv_obj_t *s_hud_roll      = NULL;
static lv_obj_t *s_roll_pointer  = NULL;

/* 叠加仪表对象 */
static lv_obj_t              *s_compass_bg   = NULL;
static lv_obj_t              *s_compass_hdg  = NULL;
static lv_obj_t              *s_alt_bar      = NULL;   /* 海拔条形柱 */
static lv_obj_t              *s_alt_label    = NULL;
static lv_obj_t              *s_bar_bar      = NULL;   /* 气压条形柱 */
static lv_obj_t              *s_bar_label    = NULL;

/* 详细页标签 */
static lv_obj_t *s_det_labels[10] = {NULL};

/* 时间戳 */
static int64_t s_last_update_us     = 0;
static int64_t s_last_env_update_us = 0;

/* ===================== 前向声明 ===================== */
static void att_page_click_cb(lv_event_t *e);
static void att_page_longpress_cb(lv_event_t *e);
static void detail_back_cb(lv_event_t *e);

/* ===================== 辅助函数 ===================== */

/* 将横滚角度映射为弧上偏移量并定位标签 */
static void position_roll_tick(lv_obj_t *label, int16_t radius, int16_t roll_deg)
{
    float angle = (270 + roll_deg) * (float)M_PI / 180.0f;
    int16_t dx = (int16_t)(radius * cosf(angle));
    int16_t dy = (int16_t)(radius * sinf(angle));
    lv_obj_align(label, LV_ALIGN_CENTER, dx, dy);
}

/* 将罗盘方位角映射为表盘上偏移量并定位标签 */
static void position_compass_tick(lv_obj_t *label, int16_t radius, int16_t bearing)
{
    float rad = bearing * (float)M_PI / 180.0f;
    int16_t dx = (int16_t)(radius * sinf(rad));
    int16_t dy = (int16_t)(-radius * cosf(rad));
    lv_obj_align(label, LV_ALIGN_CENTER, dx, dy);
}

/* 创建胶囊式按钮 */
static lv_obj_t *create_capsule_btn(lv_obj_t *parent, const char *text,
                                     lv_event_cb_t cb, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x333344), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

/* ===================== 画布渲染：姿态指引仪 ===================== */

/* 像素级绘制天空/大地/地平线/俯仰刻度，绕过 LVGL transform_angle + clip_corner 缺陷 */
static void render_horizon_canvas(float pitch, float roll)
{
    if (!s_canvas_buf) return;

    float roll_rad = roll * (float)M_PI / 180.0f;
    float cos_r = cosf(roll_rad);
    float sin_r = sinf(roll_rad);

    const int size = FI_CANVAS_SIZE;
    const int half = size / 2;             /* 200 */
    const int radius_sq = half * half;
    const float pitch_off = pitch * FI_PITCH_SCALE;
    const float cy = (float)half + pitch_off;

    /* 俯仰刻度 sd 值: ±30=±10°, ±60=±20°, ±90=±30° (基于 FI_PITCH_SCALE=3) */
    static const int tick_sd[] = {30, -30, 60, -60, 90, -90};
    const int tick_half_w = 22;            /* 刻度线长度一半 (canvas 坐标) */

    lv_color_t *buf = s_canvas_buf;

    for (int y = 0; y < size; y++) {
        int dy_c = y - half;
        float dy_h = (float)y - cy;

        for (int x = 0; x < size; x++) {
            int dx = x - half;
            int idx = y * size + x;

            /* 圆外像素 → 黑色 */
            if (dx * dx + dy_c * dy_c > radius_sq) {
                buf[idx] = lv_color_black();
                continue;
            }

            /* 带符号距离: sd>0 → 天空, sd<0 → 大地 */
            float sd = dx * sin_r - dy_h * cos_r;

            /* 地平线 (~2px 宽, 全宽) */
            if (fabsf(sd) <= 1.0f) {
                buf[idx] = lv_color_white();
                continue;
            }

            /* 俯仰刻度线 (±10°/±20°/±30°, 仅在 tick_half_w 范围内) */
            bool is_tick = false;
            float td = dx * cos_r + dy_h * sin_r;
            if (fabsf(td) <= tick_half_w) {
                for (int t = 0; t < 6; t++) {
                    if (fabsf(sd - (float)tick_sd[t]) <= 1.0f) {
                        is_tick = true;
                        break;
                    }
                }
            }

            if (is_tick) {
                buf[idx] = lv_color_white();
            } else if (sd > 0) {
                buf[idx] = COLOR_SKY;
            } else {
                buf[idx] = COLOR_GROUND;
            }
        }
    }
    lv_obj_invalidate(s_canvas);
}

/* ===================== 第1页：姿态指引仪 ===================== */

static void create_attitude_page(lv_obj_t *parent)
{
    /* 页面容器 */
    s_page_att = lv_obj_create(parent);
    lv_obj_set_size(s_page_att, FI_SCREEN_SIZE, FI_SCREEN_SIZE);
    lv_obj_center(s_page_att);
    lv_obj_clear_flag(s_page_att, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_page_att, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_page_att, 0, 0);
    lv_obj_set_style_pad_all(s_page_att, 0, 0);

    /* ---- 姿态画布 (替代 clip+transform_angle, 像素级渲染) ---- */
    s_canvas = lv_canvas_create(s_page_att);
    lv_obj_center(s_canvas);
    /* 分配 PSRAM 缓冲区: 200x200 RGB565 = 80KB */
    if (!s_canvas_buf) {
        s_canvas_buf = heap_caps_malloc(FI_CANVAS_SIZE * FI_CANVAS_SIZE * sizeof(lv_color_t),
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_canvas_buf) {
            ESP_LOGE(TAG, "canvas buffer alloc failed");
        }
    }
    lv_canvas_set_buffer(s_canvas, s_canvas_buf, FI_CANVAS_SIZE, FI_CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);
    lv_img_set_zoom(s_canvas, 512);  /* 2x zoom: 200→400 显示尺寸 */
    lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    render_horizon_canvas(0.0f, 0.0f);  /* 初始渲染 */

    /* ---- 飞机符号 (固定，不旋转) ---- */
    /* 左翼 */
    static lv_point_t lw_pts[] = {{0, 0}, {28, 0}};
    lv_obj_t *lw = lv_line_create(s_page_att);
    lv_line_set_points(lw, lw_pts, 2);
    lv_obj_set_style_line_width(lw, 3, 0);
    lv_obj_set_style_line_color(lw, COLOR_AIRCRAFT, 0);
    lv_obj_align(lw, LV_ALIGN_CENTER, -26, 0);

    /* 右翼 */
    static lv_point_t rw_pts[] = {{0, 0}, {28, 0}};
    lv_obj_t *rw = lv_line_create(s_page_att);
    lv_line_set_points(rw, rw_pts, 2);
    lv_obj_set_style_line_width(rw, 3, 0);
    lv_obj_set_style_line_color(rw, COLOR_AIRCRAFT, 0);
    lv_obj_align(rw, LV_ALIGN_CENTER, 26, 0);

    /* 中心圆 */
    lv_obj_t *dot = lv_obj_create(s_page_att);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_align(dot, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(dot, COLOR_AIRCRAFT, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);

    /* ---- 横滚刻度弧 ---- */
    lv_obj_t *arc = lv_arc_create(s_page_att);
    lv_obj_set_size(arc, FI_CLIP_SIZE, FI_CLIP_SIZE);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 0);
    lv_arc_set_bg_angles(arc, 210, 330);     /* ±60° 范围，顶部 */
    lv_arc_set_range(arc, 0, 120);
    lv_arc_set_value(arc, 0);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 0, LV_PART_INDICATOR);

    /* 横滚刻度标签: 0°/±10°/±20°/±30°/±45°/±60° */
    static const int roll_ticks[] = {0, 10, 20, 30, 45, 60, -10, -20, -30, -45, -60};
    for (int i = 0; i < (int)(sizeof(roll_ticks) / sizeof(roll_ticks[0])); i++) {
        int r = roll_ticks[i];
        char buf[16];
        if (r == 0)
            snprintf(buf, sizeof(buf), "0");
        else if (r > 0)
            snprintf(buf, sizeof(buf), "+%d", r);
        else
            snprintf(buf, sizeof(buf), "%d", r);

        lv_obj_t *tick = lv_label_create(s_page_att);
        lv_label_set_text(tick, buf);
        lv_obj_set_style_text_color(tick, lv_color_white(), 0);
        lv_obj_set_style_text_font(tick, &lv_font_montserrat_14, 0);
        position_roll_tick(tick, FI_ROLL_ARC_RADIUS, (int16_t)r);
    }

    /* ---- 横滚指针 (随 roll 旋转) ---- */
    s_roll_pointer = lv_obj_create(s_page_att);
    lv_obj_set_size(s_roll_pointer, 4, 14);
    lv_obj_align(s_roll_pointer, LV_ALIGN_CENTER, 0, -FI_ROLL_ARC_RADIUS);
    lv_obj_clear_flag(s_roll_pointer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_roll_pointer, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_roll_pointer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_roll_pointer, 0, 0);
    lv_obj_set_style_radius(s_roll_pointer, 0, 0);
    /* 旋转轴心设为屏幕中心 */
    lv_obj_set_style_transform_pivot_x(s_roll_pointer, 2, 0);
    lv_obj_set_style_transform_pivot_y(s_roll_pointer,
        FI_ROLL_ARC_RADIUS + 7, 0);  /* 7 = height/2 */

    /* ---- HUD 标签 ---- */
    s_hud_pitch = lv_label_create(s_page_att);
    lv_obj_set_style_text_font(s_hud_pitch, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_hud_pitch, lv_color_white(), 0);
    lv_label_set_text(s_hud_pitch, "P:+0.0");
    lv_obj_align(s_hud_pitch, LV_ALIGN_BOTTOM_LEFT, 40, -12);

    s_hud_roll = lv_label_create(s_page_att);
    lv_obj_set_style_text_font(s_hud_roll, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_hud_roll, lv_color_white(), 0);
    lv_label_set_text(s_hud_roll, "R:+0.0");
    lv_obj_align(s_hud_roll, LV_ALIGN_BOTTOM_RIGHT, -40, -12);

    /* ---- 叠加仪表：指南针 / 海拔条 / 气压圆 ---- */

    /* 指南针 (底部中央, 80x80 小圆) */
    s_compass_bg = lv_obj_create(s_page_att);
    lv_obj_set_size(s_compass_bg, 80, 80);
    lv_obj_align(s_compass_bg, LV_ALIGN_CENTER, 0, 150);
    lv_obj_clear_flag(s_compass_bg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_compass_bg, lv_color_hex(0x111122), 0);
    lv_obj_set_style_bg_opa(s_compass_bg, LV_OPA_80, 0);
    lv_obj_set_style_border_color(s_compass_bg, lv_color_hex(0x444466), 0);
    lv_obj_set_style_border_width(s_compass_bg, 1, 0);
    lv_obj_set_style_radius(s_compass_bg, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(s_compass_bg, 0, 0);

    lv_obj_t *lbl_n = lv_label_create(s_compass_bg);
    lv_label_set_text(lbl_n, "N");
    lv_obj_set_style_text_color(lbl_n, COLOR_NORTH, 0);
    lv_obj_set_style_text_font(lbl_n, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_n, LV_ALIGN_TOP_MID, 0, 4);

    s_compass_hdg = lv_label_create(s_compass_bg);
    lv_obj_set_style_text_font(s_compass_hdg, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_compass_hdg, lv_color_white(), 0);
    lv_label_set_text(s_compass_hdg, "---");
    lv_obj_align(s_compass_hdg, LV_ALIGN_CENTER, 0, 8);

    /* 海拔条形柱 (右侧垂直, 16x120) */
    s_alt_bar = lv_bar_create(s_page_att);
    lv_obj_set_size(s_alt_bar, 16, 120);
    lv_obj_align(s_alt_bar, LV_ALIGN_CENTER, 155, 0);
    lv_bar_set_range(s_alt_bar, 0, 5000);
    lv_bar_set_value(s_alt_bar, 0, LV_ANIM_OFF);
    lv_obj_clear_flag(s_alt_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_alt_bar, lv_color_hex(0x111122), 0);
    lv_obj_set_style_bg_opa(s_alt_bar, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_alt_bar, 1, 0);
    lv_obj_set_style_border_color(s_alt_bar, lv_color_hex(0x333344), 0);
    lv_obj_set_style_radius(s_alt_bar, 3, 0);
    lv_obj_set_style_bg_color(s_alt_bar, COLOR_ALT_TICK, LV_PART_INDICATOR);

    s_alt_label = lv_label_create(s_page_att);
    lv_obj_set_style_text_font(s_alt_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_alt_label, COLOR_ALT_TICK, 0);
    lv_label_set_text(s_alt_label, "--m");
    lv_obj_align(s_alt_label, LV_ALIGN_CENTER, 155, 72);

    /* 气压条形柱 (左侧垂直, 与海拔条对称, 16x120) */
    s_bar_bar = lv_bar_create(s_page_att);
    lv_obj_set_size(s_bar_bar, 16, 120);
    lv_obj_align(s_bar_bar, LV_ALIGN_CENTER, -155, 0);
    lv_bar_set_range(s_bar_bar, 960, 1060);
    lv_bar_set_value(s_bar_bar, 1013, LV_ANIM_OFF);
    lv_obj_clear_flag(s_bar_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_bar_bar, lv_color_hex(0x111122), 0);
    lv_obj_set_style_bg_opa(s_bar_bar, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_bar_bar, 1, 0);
    lv_obj_set_style_border_color(s_bar_bar, lv_color_hex(0x333344), 0);
    lv_obj_set_style_radius(s_bar_bar, 3, 0);
    lv_obj_set_style_bg_color(s_bar_bar, COLOR_BAR_TICK, LV_PART_INDICATOR);

    s_bar_label = lv_label_create(s_page_att);
    lv_obj_set_style_text_font(s_bar_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_bar_label, COLOR_BAR_TICK, 0);
    lv_label_set_text(s_bar_label, "--");
    lv_obj_align(s_bar_label, LV_ALIGN_CENTER, -155, 72);

    /* 点击进入详情页 + 长按退出回表情页 */
    lv_obj_add_flag(s_page_att, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_page_att, att_page_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_page_att, att_page_longpress_cb, LV_EVENT_LONG_PRESSED, NULL);

    /* 底部提示标签 */
    lv_obj_t *hint = lv_label_create(s_page_att);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666688), 0);
    lv_label_set_text(hint, "tap: detail  ·  hold: exit");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -18);

    /* 默认可见 (第1页) */
    lv_obj_clear_flag(s_page_att, LV_OBJ_FLAG_HIDDEN);
}

/* ===================== 第2页：详细数值页 ===================== */

static void create_detail_page(lv_obj_t *parent)
{
    s_page_detail = lv_obj_create(parent);
    lv_obj_set_size(s_page_detail, FI_SCREEN_SIZE, FI_SCREEN_SIZE);
    lv_obj_center(s_page_detail);
    lv_obj_clear_flag(s_page_detail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_page_detail, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_page_detail, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_page_detail, 0, 0);
    lv_obj_set_style_pad_all(s_page_detail, 0, 0);
    lv_obj_set_style_radius(s_page_detail, 0, 0);

    /* 标题 */
    lv_obj_t *title = lv_label_create(s_page_detail);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "Sensor Data");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    /* 数据行: 两列布局 */
    const char *names[] = {
        "Pitch",   "Roll",
        "Heading", "Altitude",
        "Pressure","Temp",
        "Humidity","UV Index",
        "UV Volt", ""
    };
    const lv_coord_t y_start = 80;
    const lv_coord_t row_h   = 38;
    for (int i = 0; i < 9; i++) {
        int col = i % 2;
        int row = i / 2;
        lv_coord_t x = (col == 0) ? -110 : 60;
        lv_coord_t y = y_start + row * row_h;

        /* 标签名 */
        lv_obj_t *n = lv_label_create(s_page_detail);
        lv_obj_set_style_text_font(n, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(n, lv_color_hex(0x8b949e), 0);
        lv_label_set_text(n, names[i]);
        lv_obj_align(n, LV_ALIGN_CENTER, x, y - 8);

        /* 数值 */
        s_det_labels[i] = lv_label_create(s_page_detail);
        lv_obj_set_style_text_font(s_det_labels[i], &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(s_det_labels[i], lv_color_white(), 0);
        lv_label_set_text(s_det_labels[i], "--");
        lv_obj_align(s_det_labels[i], LV_ALIGN_CENTER, x, y + 10);
    }

    /* 返回按钮 */
    create_capsule_btn(s_page_detail, "< Back", detail_back_cb, 90, 34);
    lv_obj_align(lv_obj_get_child(s_page_detail, -1), LV_ALIGN_BOTTOM_MID, 0, -18);

    /* 默认隐藏 */
    lv_obj_add_flag(s_page_detail, LV_OBJ_FLAG_HIDDEN);
}

/* ===================== 事件回调 ===================== */

/* 姿态页点击：进入详情页 */
static void att_page_click_cb(lv_event_t *e)
{
    (void)e;
    s_detail_mode = true;
    lv_obj_add_flag(s_page_att, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_page_detail, LV_OBJ_FLAG_HIDDEN);
}

/* 姿态页长按：退出回表情页 */
static void att_page_longpress_cb(lv_event_t *e)
{
    (void)e;
    flight_instruments_hide();
}

/* 详情页返回：回到姿态页 */
static void detail_back_cb(lv_event_t *e)
{
    (void)e;
    s_detail_mode = false;
    lv_obj_add_flag(s_page_detail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_page_att, LV_OBJ_FLAG_HIDDEN);
}

/* ===================== 公开 API ===================== */

void flight_instruments_create(lv_obj_t *parent)
{
    /* 全屏覆盖容器 (默认隐藏) */
    s_overlay = lv_obj_create(parent);
    lv_obj_set_size(s_overlay, FI_SCREEN_SIZE, FI_SCREEN_SIZE);
    lv_obj_center(s_overlay);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);

    /* 创建姿态页 */
    create_attitude_page(s_overlay);

    /* 创建详情页 */
    create_detail_page(s_overlay);

    /* 默认隐藏 */
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    s_visible = false;

    ESP_LOGI(TAG, "飞行仪表盘覆盖层创建完成");
}

void flight_instruments_show(void)
{
    if (!s_overlay) return;
    s_visible = true;
    lv_obj_clear_flag(s_page_att, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

void flight_instruments_hide(void)
{
    if (!s_overlay) return;
    s_visible = false;
    s_detail_mode = false;
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_att, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_detail, LV_OBJ_FLAG_HIDDEN);
}

bool flight_instruments_is_visible(void)
{
    return s_visible;
}

void flight_instruments_update(void)
{
    if (!s_visible) return;

    int64_t now = esp_timer_get_time();
    if (now - s_last_update_us < FI_UPDATE_PERIOD_US) return;
    s_last_update_us = now;

    /* ---- 读取传感器数据 ---- */
    float pitch = 0, roll = 0;
    motion_engine_get_angles(&pitch, &roll);

    char buf[32];

    /* ---- 详情页模式：只更新数值 ---- */
    if (s_detail_mode) {
        snprintf(buf, sizeof(buf), "%+.1f\xc2\xb0", pitch);
        lv_label_set_text(s_det_labels[0], buf);
        snprintf(buf, sizeof(buf), "%+.1f\xc2\xb0", roll);
        lv_label_set_text(s_det_labels[1], buf);
        /* Heading 暂无数据源 */
        lv_label_set_text(s_det_labels[2], "---");

        env_snapshot_t env;
        if (sensor_manager_get_env(&env) == ESP_OK) {
            snprintf(buf, sizeof(buf), "%.1fm", env.bmp280.altitude);
            lv_label_set_text(s_det_labels[3], buf);

            float hpa = env.bmp280.pressure / 100.0f;
            snprintf(buf, sizeof(buf), "%.1fhPa", hpa);
            lv_label_set_text(s_det_labels[4], buf);

            snprintf(buf, sizeof(buf), "%.1f\xc2\xb0" "C", env.bmp280.temperature);
            lv_label_set_text(s_det_labels[5], buf);

            snprintf(buf, sizeof(buf), "%.1f%%", env.aht20.humidity);
            lv_label_set_text(s_det_labels[6], buf);

            snprintf(buf, sizeof(buf), "%.1f", env.uv.uv_index);
            lv_label_set_text(s_det_labels[7], buf);

            snprintf(buf, sizeof(buf), "%.3fV", env.uv.voltage);
            lv_label_set_text(s_det_labels[8], buf);
        }
        return;
    }

    /* ---- 姿态页模式：渲染画布 + 更新仪表 ---- */
    if (!s_canvas_buf) return;

    /* 钳制 pitch/roll 范围 */
    float p_clamped = pitch, r_clamped = roll;
    if (p_clamped > FI_PITCH_MAX) p_clamped = FI_PITCH_MAX;
    if (p_clamped < -FI_PITCH_MAX) p_clamped = -FI_PITCH_MAX;
    if (r_clamped > FI_ROLL_MAX) r_clamped = FI_ROLL_MAX;
    if (r_clamped < -FI_ROLL_MAX) r_clamped = -FI_ROLL_MAX;

    render_horizon_canvas(p_clamped, r_clamped);

    /* 横滚指针旋转 */
    int16_t roll_tenth = (int16_t)(r_clamped * 10);
    lv_obj_set_style_transform_angle(s_roll_pointer, roll_tenth, 0);

    /* HUD 标签 */
    snprintf(buf, sizeof(buf), "P:%+.1f", pitch);
    lv_label_set_text(s_hud_pitch, buf);
    snprintf(buf, sizeof(buf), "R:%+.1f", roll);
    lv_label_set_text(s_hud_roll, buf);

    /* ---- 环境数据 @1Hz ---- */
    if (now - s_last_env_update_us >= FI_ENV_PERIOD_US) {
        s_last_env_update_us = now;

        env_snapshot_t env;
        if (sensor_manager_get_env(&env) == ESP_OK) {
            /* 海拔条形柱 */
            float alt = env.bmp280.altitude;
            if (alt < 0) alt = 0;
            if (alt > 5000) alt = 5000;
            lv_bar_set_value(s_alt_bar, (int16_t)alt, LV_ANIM_OFF);
            snprintf(buf, sizeof(buf), "%.0fm", env.bmp280.altitude);
            lv_label_set_text(s_alt_label, buf);

            /* 气压条形柱 */
            float hpa = env.bmp280.pressure / 100.0f;
            if (hpa < 960) hpa = 960;
            if (hpa > 1060) hpa = 1060;
            lv_bar_set_value(s_bar_bar, (int16_t)hpa, LV_ANIM_OFF);
            snprintf(buf, sizeof(buf), "%.0f", hpa);
            lv_label_set_text(s_bar_label, buf);
        }
    }
}
