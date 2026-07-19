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
#include <string.h>
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

#define FI_UPDATE_PERIOD_US    50000     /* 20Hz (50ms) */
#define FI_ENV_PERIOD_US       1000000   /* 1Hz */
#define FI_RENDER_THRESHOLD    0.3f      /* 角度变化阈值，低于此值跳过渲染 */

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
static bool      s_visible     = false;

/* 姿态页对象 */
static lv_obj_t   *s_canvas       = NULL;
static lv_color_t *s_canvas_buf   = NULL;
static lv_obj_t *s_hud_pitch     = NULL;
static lv_obj_t *s_hud_roll      = NULL;
static lv_obj_t *s_roll_pointer  = NULL;

/* 叠加仪表对象 */
static lv_obj_t              *s_compass_bg   = NULL;
static lv_obj_t              *s_compass_hdg  = NULL;
static lv_obj_t              *s_compass_face = NULL;  /* 表盘容器（作为可旋转整体） */
static lv_obj_t              *s_compass_lbl[8] = {NULL}; /* 8 个方位字母 N/NE/E/SE/S/SW/W/NW */
static lv_obj_t              *s_compass_tri  = NULL;  /* 顶部固定▼指针（表朝向标记）*/
static int16_t                s_last_heading  = -1;   /* 表盘增量重定位跟踪 (-1=首次) */
static lv_obj_t              *s_alt_bar      = NULL;   /* 海拔条形柱 */
static lv_obj_t              *s_alt_label    = NULL;
static lv_obj_t              *s_bar_bar      = NULL;   /* 气压条形柱 */
static lv_obj_t              *s_bar_label    = NULL;

/* 时间戳 */
static int64_t s_last_update_us     = 0;
static int64_t s_last_env_update_us = 0;

/* 增量渲染跟踪 */
static float s_last_render_pitch = 999.0f;
static float s_last_render_roll  = 999.0f;

/* 俯仰刻度标签 (6 条刻度线 × 左右两侧 = 12 个标签) */
static lv_obj_t *s_pitch_labels[6][2] = {NULL};

/* 校准 UI 状态 */
static lv_obj_t *s_calib_mbox     = NULL;   /* 模态确认对话框 */
static lv_obj_t *s_calib_overlay  = NULL;   /* 半透明遮罩 + 进度环容器 */
static lv_obj_t *s_calib_arc      = NULL;   /* 进度环 */
static lv_obj_t *s_calib_hint_lbl = NULL;   /* 状态提示标签 */
static int64_t   s_calib_done_at  = 0;      /* DONE/FAILED 时间戳 */

/* ===================== 前向声明 ===================== */
static void att_page_click_cb(lv_event_t *e);
static void compass_face_update(int16_t heading);
static void att_page_long_press_cb(lv_event_t *e);
static void calib_mbox_cb(lv_event_t *e);
static void create_calib_overlay(void);
static void destroy_calib_overlay(void);

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

/* ===================== 指南针表盘动态重定位 =====================
 * 设计思路（户外手表风格表盘旋转）：
 *   - 外壳固定（包含顶部▼指针、中央数字读数）
 *   - 表盘内 8 个方位字母随 heading 逆向旋转
 *   - heading=000°  → N 在顶部
 *   - heading=090°  → N 转到左侧 (用户朝东，所以北在左手边)
 *   - heading=180°  → N 转到底部
 *   - 超过±90°范围的字母变暗（表盘背后的方位）
 *
 * 8 个方位字母的固定方位角：
 *   N=0°  NE=45°  E=90°  SE=135°  S=180°  SW=225°  W=270°  NW=315°
 */
static const char *COMPASS_NAMES[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
static const int   COMPASS_ANGLES[8] = {0, 45, 90, 135, 180, 225, 270, 315};

#define COMPASS_FACE_RADIUS   30      /* 方位字母在表盘上的半径 */
#define COMPASS_HDG_THRESHOLD 2       /* heading 变化超过 2° 才重定位 */

static void compass_face_update(int16_t heading)
{
    if (!s_compass_face) return;

    /* 增量优化：heading 变化小于阈值不重定位 */
    if (s_last_heading >= 0 &&
        abs(heading - s_last_heading) < COMPASS_HDG_THRESHOLD) {
        return;
    }
    s_last_heading = heading;

    /* 逐个重定位 8 个方位字母 */
    for (int i = 0; i < 8; i++) {
        if (!s_compass_lbl[i]) continue;

        /* 字母在表盘上的显示角度 = 方位角 - heading
         * heading 是用户朝向（屏幕顶部），所以北的方位 = 0 - heading */
        int disp_angle = COMPASS_ANGLES[i] - heading;
        while (disp_angle < 0)   disp_angle += 360;
        while (disp_angle >= 360) disp_angle -= 360;

        /* 极坐标转屏幕坐标（北=顶部，顺时针） */
        int16_t dx = (int16_t)(COMPASS_FACE_RADIUS * sinf((float)disp_angle * (float)M_PI / 180.0f));
        int16_t dy = (int16_t)(-COMPASS_FACE_RADIUS * cosf((float)disp_angle * (float)M_PI / 180.0f));
        lv_obj_align(s_compass_lbl[i], LV_ALIGN_CENTER, dx, dy);

        /* 表盘背面（>±90°）的字母变暗（模拟 3D 表盘透视）*/
        lv_opa_t opa;
        if (disp_angle > 100 && disp_angle < 260) {
            opa = LV_OPA_30;          /* 背面字母变暗 */
        } else if (disp_angle > 80 && disp_angle <= 100) {
            opa = LV_OPA_60;          /* 边缘过渡区 */
        } else if (disp_angle >= 260 && disp_angle <= 280) {
            opa = LV_OPA_60;          /* 边缘过渡区 */
        } else {
            opa = LV_OPA_COVER;       /* 正面字母全亮 */
        }
        lv_obj_set_style_text_opa(s_compass_lbl[i], opa, 0);
    }
}

/* ===================== 画布渲染：姿态指引仪 ===================== */

/* 像素级绘制天空/大地/地平线/俯仰刻度，绕过 LVGL transform_angle + clip_corner 缺陷
 * 优化：memset预填充黑色 + 行裁剪跳过圆外像素 + 预计算 + 增量跳过 */
static void render_horizon_canvas(float pitch, float roll)
{
    if (!s_canvas_buf) return;

    /* 增量跳过：角度变化小于阈值时不重绘 */
    float dp = fabsf(pitch - s_last_render_pitch);
    float dr = fabsf(roll  - s_last_render_roll);
    if (dp < FI_RENDER_THRESHOLD && dr < FI_RENDER_THRESHOLD) {
        return;  /* 无显著变化，跳过整帧渲染 */
    }
    s_last_render_pitch = pitch;
    s_last_render_roll  = roll;

    float roll_rad = roll * (float)M_PI / 180.0f;
    float cos_r = cosf(roll_rad);
    float sin_r = sinf(roll_rad);

    const int size = FI_CANVAS_SIZE;
    const int half = size / 2;             /* 100 */
    const int radius   = half;
    const int radius_sq = radius * radius;
    const float pitch_off = pitch * FI_PITCH_SCALE;
    const float cy = (float)half + pitch_off;

    /* 俯仰刻度 sd 值 (canvas 像素，sd>0=天空/仰角) */
    static const int tick_sd[] = {30, -30, 60, -60, 90, -90};
    const int tick_half_w = 22;  /* 刻度线半宽 (canvas px) */

    lv_color_t *buf = s_canvas_buf;

    /* memset 预填充全黑 (RGB565 黑色 = 0x0000) */
    memset(buf, 0, size * size * sizeof(lv_color_t));

    /* 仅渲染圆内像素，逐行计算水平边界 */
    for (int y = 0; y < size; y++) {
        int dy_c = y - half;
        int dy_sq = dy_c * dy_c;
        if (dy_sq > radius_sq) continue;   /* 整行在圆外 */

        /* 计算该行圆内 x 范围 */
        int x_ext = (int)sqrtf((float)(radius_sq - dy_sq));
        int x_start = half - x_ext;
        int x_end   = half + x_ext;
        if (x_start < 0) x_start = 0;
        if (x_end >= size) x_end = size - 1;

        float dy_h = (float)y - cy;
        float cos_r_dy_h = cos_r * dy_h;   /* 预计算：内循环少一次乘法 */

        int row_base = y * size;
        for (int x = x_start; x <= x_end; x++) {
            int dx = x - half;
            int idx = row_base + x;

            /* 带符号距离: sd>0 → 天空, sd<0 → 大地 */
            float sd = (float)dx * sin_r - cos_r_dy_h;

            /* 地平线 (~2px 宽) */
            if (fabsf(sd) <= 1.0f) {
                buf[idx] = lv_color_white();
                continue;
            }

            /* 俯仰刻度线 */
            bool is_tick = false;
            float td = (float)dx * cos_r + dy_h * sin_r;
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

    /* ---- 俯仰刻度度数标签 (横线两端: 10/20/30) ---- */
    static const char *pitch_deg[] = {"10", "10", "20", "20", "30", "30"};
    for (int i = 0; i < 6; i++) {
        for (int side = 0; side < 2; side++) {
            s_pitch_labels[i][side] = lv_label_create(s_page_att);
            lv_obj_set_style_text_font(s_pitch_labels[i][side], &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(s_pitch_labels[i][side], lv_color_white(), 0);
            lv_label_set_text(s_pitch_labels[i][side], pitch_deg[i]);
            lv_obj_clear_flag(s_pitch_labels[i][side], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        }
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

    /* ===================== 指南针（表盘旋转风格）======================
     * 外壳 (100×100 圆形，底部居中)
     *   ├── 顶部固定▼指针（黄色三角形，指示当前朝向）
     *   ├── 表盘容器 s_compass_face (88×88 可旋转，8 个方位字母)
     *   │     ├── N (红色)、E/S/W (白色) — 主方位
     *   │     └── NE/NW/SE/SW (浅灰小字) — 次方位
     *   └── 中央数字读数 s_compass_hdg (0~360°)
     */
    s_compass_bg = lv_obj_create(s_page_att);
    lv_obj_set_size(s_compass_bg, 100, 100);
    lv_obj_align(s_compass_bg, LV_ALIGN_CENTER, 0, 150);
    lv_obj_clear_flag(s_compass_bg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_compass_bg, lv_color_hex(0x0A0E1A), 0);
    lv_obj_set_style_bg_opa(s_compass_bg, LV_OPA_90, 0);
    lv_obj_set_style_border_color(s_compass_bg, lv_color_hex(0x666688), 0);
    lv_obj_set_style_border_width(s_compass_bg, 2, 0);
    lv_obj_set_style_radius(s_compass_bg, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(s_compass_bg, 0, 0);

    /* 顶部固定▼指针（黄色三角形，指示当前朝向）*/
    s_compass_tri = lv_label_create(s_compass_bg);
    lv_obj_set_style_text_font(s_compass_tri, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_compass_tri, lv_color_hex(0xFFD700), 0);
    lv_label_set_text(s_compass_tri, "▼");
    lv_obj_align(s_compass_tri, LV_ALIGN_TOP_MID, 0, -2);

    /* 表盘容器（8 个方位字母的父对象，便于将来整体旋转）*/
    s_compass_face = lv_obj_create(s_compass_bg);
    lv_obj_set_size(s_compass_face, 88, 88);
    lv_obj_center(s_compass_face);
    lv_obj_clear_flag(s_compass_face, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(s_compass_face, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_compass_face, 0, 0);
    lv_obj_set_style_pad_all(s_compass_face, 0, 0);
    lv_obj_set_style_radius(s_compass_face, LV_RADIUS_CIRCLE, 0);

    /* 8 个方位字母 — 主方位 N/E/S/W 大字号，次方位 NE/NW/SE/SW 小字号 */
    for (int i = 0; i < 8; i++) {
        s_compass_lbl[i] = lv_label_create(s_compass_face);
        bool is_primary = (i % 2 == 0);  /* N(0)/E(2)/S(4)/W(6) */

        if (is_primary) {
            lv_obj_set_style_text_font(s_compass_lbl[i], &lv_font_montserrat_16, 0);
            if (i == 0) {
                /* N 用红色高亮（主方位北）*/
                lv_obj_set_style_text_color(s_compass_lbl[i], COLOR_NORTH, 0);
            } else {
                lv_obj_set_style_text_color(s_compass_lbl[i], lv_color_white(), 0);
            }
        } else {
            /* 次方位 NE/NW/SE/SW 同字号但深灰色 */
            lv_obj_set_style_text_font(s_compass_lbl[i], &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(s_compass_lbl[i], lv_color_hex(0x666677), 0);
        }
        lv_label_set_text(s_compass_lbl[i], COMPASS_NAMES[i]);
    }

    /* 中央数字读数 (不参与表盘旋转，独立显示) */
    s_compass_hdg = lv_label_create(s_compass_bg);
    lv_obj_set_style_text_font(s_compass_hdg, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_compass_hdg, lv_color_hex(0xFFD700), 0);
    lv_label_set_text(s_compass_hdg, "---");
    lv_obj_align(s_compass_hdg, LV_ALIGN_BOTTOM_MID, 0, -8);

    /* 初始定位 8 个方位字母 (heading=0 时 N 在顶部) */
    s_last_heading = -1;
    compass_face_update(0);

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

    /* 点击退出回表情页，长按触发陀螺仪校准 */
    lv_obj_add_flag(s_page_att, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_page_att, att_page_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_page_att, att_page_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);

    /* 底部提示标签 */
    lv_obj_t *hint = lv_label_create(s_page_att);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666688), 0);
    lv_label_set_text(hint, "tap: exit  |  hold: calib");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -18);

    /* 默认可见 (第1页) */
    lv_obj_clear_flag(s_page_att, LV_OBJ_FLAG_HIDDEN);
}

/* ===================== 事件回调 ===================== */

/* 姿态页点击：退出回表情页（校准遮罩打开时禁止退出） */
static void att_page_click_cb(lv_event_t *e)
{
    (void)e;
    if (s_calib_overlay || s_calib_mbox) return;
    flight_instruments_hide();
}

/* 姿态页长按：弹出校准确认对话框 */
static void att_page_long_press_cb(lv_event_t *e)
{
    (void)e;
    if (s_calib_mbox || s_calib_overlay) return;  /* 防重入 */

    static const char *btns[] = {"Start", "Cancel", ""};
    s_calib_mbox = lv_msgbox_create(NULL, "Gyro Calibration",
                                     "Keep device level & still\nfor 3 seconds.",
                                     btns, true);
    if (s_calib_mbox) {
        lv_obj_set_style_bg_color(s_calib_mbox, lv_color_hex(0x222233), 0);
        lv_obj_set_style_bg_opa(s_calib_mbox, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_calib_mbox, 2, 0);
        lv_obj_set_style_border_color(s_calib_mbox, lv_color_hex(0x00B4FF), 0);
        lv_obj_set_style_radius(s_calib_mbox, 12, 0);
        lv_obj_add_event_cb(s_calib_mbox, calib_mbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_center(s_calib_mbox);
    }
}

/* 校准对话框按钮回调 */
static void calib_mbox_cb(lv_event_t *e)
{
    lv_obj_t *mbox = lv_event_get_current_target(e);
    uint16_t idx = lv_msgbox_get_active_btn(mbox);

    /* 删除对话框 */
    if (s_calib_mbox) {
        lv_obj_del(s_calib_mbox);
        s_calib_mbox = NULL;
    }

    /* idx == 0: Start */
    if (idx == 0) {
        motion_engine_start_calibration(75);
        create_calib_overlay();
    }
}

/* 创建校准进度环遮罩 */
static void create_calib_overlay(void)
{
    s_calib_overlay = lv_obj_create(s_page_att);
    lv_obj_set_size(s_calib_overlay, 280, 280);
    lv_obj_center(s_calib_overlay);
    lv_obj_set_style_bg_opa(s_calib_overlay, LV_OPA_80, 0);
    lv_obj_set_style_bg_color(s_calib_overlay, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_calib_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_calib_overlay, 0, 0);
    lv_obj_set_style_radius(s_calib_overlay, 140, 0);
    lv_obj_clear_flag(s_calib_overlay, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* 进度环 */
    s_calib_arc = lv_arc_create(s_calib_overlay);
    lv_obj_set_size(s_calib_arc, 200, 200);
    lv_obj_center(s_calib_arc);
    lv_arc_set_range(s_calib_arc, 0, 100);
    lv_arc_set_value(s_calib_arc, 0);
    lv_obj_clear_flag(s_calib_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_calib_arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_calib_arc, lv_color_hex(0x00B4FF), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_calib_arc, lv_color_hex(0x333344), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_calib_arc, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_calib_arc, LV_OPA_TRANSP, LV_PART_KNOB);

    /* 中心标题标签 */
    lv_obj_t *title = lv_label_create(s_calib_overlay);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_label_set_text(title, "Calibrating");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -20);

    /* 底部状态提示 */
    s_calib_hint_lbl = lv_label_create(s_calib_overlay);
    lv_obj_set_style_text_color(s_calib_hint_lbl, lv_color_white(), 0);
    lv_label_set_text(s_calib_hint_lbl, "Keep still...");
    lv_obj_align(s_calib_hint_lbl, LV_ALIGN_CENTER, 0, 60);

    s_calib_done_at = 0;
}

/* 销毁校准遮罩 */
static void destroy_calib_overlay(void)
{
    if (s_calib_overlay) {
        lv_obj_del(s_calib_overlay);
        s_calib_overlay = NULL;
        s_calib_arc = NULL;
        s_calib_hint_lbl = NULL;
    }
    s_calib_done_at = 0;
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
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_att, LV_OBJ_FLAG_HIDDEN);
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

    /* ---- 姿态页模式：渲染画布 + 更新仪表 ---- */
    if (!s_canvas_buf) return;

    /* 钳制 pitch/roll 范围 */
    float p_clamped = pitch, r_clamped = roll;
    if (p_clamped > FI_PITCH_MAX) p_clamped = FI_PITCH_MAX;
    if (p_clamped < -FI_PITCH_MAX) p_clamped = -FI_PITCH_MAX;
    if (r_clamped > FI_ROLL_MAX) r_clamped = FI_ROLL_MAX;
    if (r_clamped < -FI_ROLL_MAX) r_clamped = -FI_ROLL_MAX;

    render_horizon_canvas(p_clamped, r_clamped);

    /* 俯仰刻度度数标签定位 (随 pitch/roll 旋转移动) */
    {
        float roll_rad = r_clamped * (float)M_PI / 180.0f;
        float cr = cosf(roll_rad);
        float sr = sinf(roll_rad);
        float pitch_off = p_clamped * FI_PITCH_SCALE;
        static const int tick_sd[] = {30, -30, 60, -60, 90, -90};
        const int label_td = 30;  /* 刻度线半宽 22 + 8px 间距 (canvas px) */

        for (int i = 0; i < 6; i++) {
            float sd = (float)tick_sd[i];
            for (int side = 0; side < 2; side++) {
                float td = (side == 0) ? -(float)label_td : (float)label_td;
                /* 从旋转坐标系转换到显示坐标 (2x zoom) */
                float dx = cr * td + sr * sd;
                float dy = pitch_off + sr * td - cr * sd;
                int16_t disp_x = (int16_t)(dx * 2);
                int16_t disp_y = (int16_t)(dy * 2);
                /* 超出圆形可视区则隐藏 */
                int32_t dist_sq = (int32_t)disp_x * disp_x + (int32_t)disp_y * disp_y;
                if (dist_sq > 185 * 185) {
                    lv_obj_add_flag(s_pitch_labels[i][side], LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_clear_flag(s_pitch_labels[i][side], LV_OBJ_FLAG_HIDDEN);
                    lv_obj_align(s_pitch_labels[i][side], LV_ALIGN_CENTER, disp_x, disp_y);
                }
            }
        }
    }

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

        /* 指南针方位角 (优先 HMC5883L，fallback MPU-9250 内置 AK8963)
         * sensor_manager 已在 imu_snapshot.compass 中完成统一封装
         * compass.valid 为 true 时，heading 字段可直接使用 */
        imu_snapshot_t imu;
        if (sensor_manager_get_imu(&imu) == ESP_OK && imu.compass.valid) {
            /* 更新中央数字读数 (0~360°) */
            snprintf(buf, sizeof(buf), "%03.0f°", imu.compass.heading);
            lv_label_set_text(s_compass_hdg, buf);
            /* 更新表盘上 8 个方位字母的位置（随 heading 逆向旋转）*/
            compass_face_update((int16_t)imu.compass.heading);
        } else {
            /* 所有磁力计均不可用 (HMC5883L 未挂 + MPU-6500 无 AK8963) */
            lv_label_set_text(s_compass_hdg, "N/A");
        }
    }

    /* ---- 校准 UI 状态机 ---- */
    if (s_calib_overlay) {
        motion_calib_state_t cs = motion_engine_get_calib_state();
        int pct = motion_engine_get_calib_progress();
        switch (cs) {
        case MOTION_CALIB_RUNNING:
            lv_arc_set_value(s_calib_arc, pct);
            break;
        case MOTION_CALIB_DONE:
            if (s_calib_done_at == 0) {
                s_calib_done_at = now;
                if (s_calib_hint_lbl) {
                    lv_label_set_text(s_calib_hint_lbl, "Done!");
                    lv_obj_set_style_text_color(s_calib_hint_lbl, lv_color_hex(0x00FF88), 0);
                }
                lv_arc_set_value(s_calib_arc, 100);
                lv_obj_set_style_arc_color(s_calib_arc, lv_color_hex(0x00FF88), LV_PART_INDICATOR);
            }
            if (now - s_calib_done_at > 1500000) {  /* 1.5s 后自动关闭 */
                destroy_calib_overlay();
            }
            break;
        case MOTION_CALIB_FAILED:
            if (s_calib_done_at == 0) {
                s_calib_done_at = now;
                if (s_calib_hint_lbl) {
                    lv_label_set_text(s_calib_hint_lbl, "Failed! Too much motion");
                    lv_obj_set_style_text_color(s_calib_hint_lbl, lv_color_hex(0xFF5050), 0);
                }
                lv_obj_set_style_arc_color(s_calib_arc, lv_color_hex(0xFF5050), LV_PART_INDICATOR);
            }
            if (now - s_calib_done_at > 2000000) {  /* 2s 后自动关闭 */
                destroy_calib_overlay();
            }
            break;
        default:
            break;
        }
    }
}
