/**
 * @file obd_dashboard.c
 * @brief OBD 车辆仪表盘页面实现
 *
 * 渲染策略（PSRAM 带宽敏感）：
 *   - 禁用 canvas，转速表用 lv_arc 静态刻度 + transform_angle 指针（局部重绘）
 *   - 三级节流：指针/RPM 50ms、车速/节气门 200ms、水温/电压/进气温 1000ms
 *   - 数值无变化时跳过 lv_label_set_text，减少脏区
 */
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "lvgl.h"
#include "obd_manager.h"
#include "emote_engine.h"
#include "font_hzk_16.h"
#include "obd_dashboard.h"

static const char *TAG = "obd_dash";

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ===================== 常量 ===================== */
#define OD_SCREEN_SIZE       480

/* 三级节流周期 */
#define OD_FAST_PERIOD_US    50000     /* 20Hz — 指针 + RPM 数字 */
#define OD_MID_PERIOD_US     200000    /* 5Hz  — 车速 / 节气门 */
#define OD_SLOW_PERIOD_US    1000000   /* 1Hz  — 水温 / 电压 / 进气温 / 在线状态 */

/* 转速表几何：240° 量程，起始 150°（LVGL 角度系，0°=3点钟方向顺时针） */
#define OD_RPM_MAX           8000
#define OD_RPM_REDLINE       6500
#define OD_ARC_SIZE          360       /* 刻度弧直径 */
#define OD_ARC_START         150
#define OD_ARC_SWEEP         240
#define OD_TICK_RADIUS       152       /* 刻度数字标签半径 */
#define OD_NEEDLE_LEN        120       /* 指针长度 (px) */
#define OD_NEEDLE_W          6

/* 水温条量程 */
#define OD_COOLANT_MIN       60
#define OD_COOLANT_MAX       130
#define OD_COOLANT_WARN      105       /* 超过此值变红 */

/* 颜色（与 flight_instruments 同调色板） */
#define COLOR_RPM_ARC        lv_color_hex(0x333344)
#define COLOR_RPM_RED        lv_color_hex(0xFF5050)
#define COLOR_RPM_NEEDLE     lv_color_hex(0xFF5050)
#define COLOR_SPEED          lv_color_hex(0x00B4FF)
#define COLOR_COOLANT_OK     lv_color_hex(0x00FF88)
#define COLOR_TEXT_DIM       lv_color_hex(0x666688)

/* ===================== 模块状态 ===================== */
static lv_obj_t *s_overlay      = NULL;
static bool      s_visible      = false;

/* 转速表对象 */
static lv_obj_t *s_rpm_needle   = NULL;
static lv_obj_t *s_rpm_label    = NULL;   /* 中央大数字 */

/* 周边仪表对象 */
static lv_obj_t *s_speed_label  = NULL;   /* 顶部车速大字 */
static lv_obj_t *s_coolant_bar  = NULL;   /* 底部水温条 */
static lv_obj_t *s_coolant_lbl  = NULL;
static lv_obj_t *s_batt_lbl     = NULL;
static lv_obj_t *s_throttle_lbl = NULL;
static lv_obj_t *s_intake_lbl   = NULL;

/* 离线提示 */
static lv_obj_t *s_offline_lbl  = NULL;
static bool      s_shown_online = true;   /* 当前 UI 呈现的在线状态 */

/* 节流时间戳与上次呈现值（无变化跳过重绘） */
static int64_t s_last_fast_us   = 0;
static int64_t s_last_mid_us    = 0;
static int64_t s_last_slow_us   = 0;
static int     s_last_rpm       = -1;
static int     s_last_speed     = -1;
static int     s_last_throttle  = -1;
static int     s_last_coolant   = -1000;
static float   s_last_batt      = -1.0f;
static int     s_last_intake    = -1000;

/* ===================== 内部函数 ===================== */

/* 页面点击：退出回表情页 */
static void obd_page_click_cb(lv_event_t *e)
{
    (void)e;
    obd_dashboard_hide();
}

/* RPM → 指针旋转角 (0.1° 单位)：0 RPM 指向弧起点 150°，满量程 390°
 * 指针默认竖直向上 (270°)，故旋转量 = 弧角 - 270° */
static int16_t rpm_to_needle_angle(float rpm)
{
    if (rpm < 0) rpm = 0;
    if (rpm > OD_RPM_MAX) rpm = OD_RPM_MAX;
    float arc_deg = OD_ARC_START + OD_ARC_SWEEP * rpm / OD_RPM_MAX;
    int angle_tenth = (int)((arc_deg - 270.0f) * 10.0f);
    if (angle_tenth < 0) angle_tenth += 3600;
    return (int16_t)angle_tenth;
}

/* 创建转速表静态刻度（弧 + 红区 + 数字标签，创建时一次性布置） */
static void create_tachometer(lv_obj_t *parent)
{
    /* 主刻度弧（静态灰色底弧） */
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, OD_ARC_SIZE, OD_ARC_SIZE);
    lv_obj_center(arc);
    lv_arc_set_bg_angles(arc, OD_ARC_START, (OD_ARC_START + OD_ARC_SWEEP) % 360);
    lv_arc_set_value(arc, 0);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, COLOR_RPM_ARC, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);

    /* 红区弧（6500~8000 RPM 段，静态） */
    lv_obj_t *red = lv_arc_create(parent);
    lv_obj_set_size(red, OD_ARC_SIZE, OD_ARC_SIZE);
    lv_obj_center(red);
    uint16_t red_start = (OD_ARC_START + OD_ARC_SWEEP * OD_RPM_REDLINE / OD_RPM_MAX) % 360;
    lv_arc_set_bg_angles(red, red_start, (OD_ARC_START + OD_ARC_SWEEP) % 360);
    lv_obj_clear_flag(red, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(red, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(red, COLOR_RPM_RED, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(red, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(red, LV_OPA_TRANSP, LV_PART_KNOB);

    /* 刻度数字 0~8 (x1000 RPM)，沿弧一次性布置 */
    for (int i = 0; i <= OD_RPM_MAX / 1000; i++) {
        float deg = OD_ARC_START + (float)OD_ARC_SWEEP * i * 1000 / OD_RPM_MAX;
        float rad = deg * (float)M_PI / 180.0f;
        int16_t dx = (int16_t)(OD_TICK_RADIUS * cosf(rad));
        int16_t dy = (int16_t)(OD_TICK_RADIUS * sinf(rad));
        lv_obj_t *tick = lv_label_create(parent);
        lv_obj_set_style_text_font(tick, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(tick, (i * 1000 >= OD_RPM_REDLINE) ?
                                    COLOR_RPM_RED : lv_color_white(), 0);
        lv_label_set_text_fmt(tick, "%d", i);
        lv_obj_align(tick, LV_ALIGN_CENTER, dx, dy);
    }

    /* 指针（复用横滚指针 transform_angle 模式，轴心=屏幕中心） */
    s_rpm_needle = lv_obj_create(parent);
    lv_obj_set_size(s_rpm_needle, OD_NEEDLE_W, OD_NEEDLE_LEN);
    lv_obj_align(s_rpm_needle, LV_ALIGN_CENTER, 0, -(OD_NEEDLE_LEN / 2));
    lv_obj_clear_flag(s_rpm_needle, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_rpm_needle, COLOR_RPM_NEEDLE, 0);
    lv_obj_set_style_bg_opa(s_rpm_needle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_rpm_needle, 0, 0);
    lv_obj_set_style_radius(s_rpm_needle, 3, 0);
    lv_obj_set_style_transform_pivot_x(s_rpm_needle, OD_NEEDLE_W / 2, 0);
    lv_obj_set_style_transform_pivot_y(s_rpm_needle, OD_NEEDLE_LEN, 0);
    lv_obj_set_style_transform_angle(s_rpm_needle, rpm_to_needle_angle(0), 0);

    /* 中央轴心圆盖 */
    lv_obj_t *hub = lv_obj_create(parent);
    lv_obj_set_size(hub, 26, 26);
    lv_obj_center(hub);
    lv_obj_clear_flag(hub, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(hub, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hub, 2, 0);
    lv_obj_set_style_border_color(hub, COLOR_RPM_ARC, 0);
    lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);

    /* 中央 RPM 大数字（轴心下方） */
    s_rpm_label = lv_label_create(parent);
    lv_obj_set_style_text_font(s_rpm_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_rpm_label, lv_color_white(), 0);
    lv_label_set_text(s_rpm_label, "----");
    lv_obj_align(s_rpm_label, LV_ALIGN_CENTER, 0, 46);

    lv_obj_t *rpm_unit = lv_label_create(parent);
    lv_obj_set_style_text_font(rpm_unit, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(rpm_unit, COLOR_TEXT_DIM, 0);
    lv_label_set_text(rpm_unit, "RPM");
    lv_obj_align(rpm_unit, LV_ALIGN_CENTER, 0, 70);
}

/* ===================== 公开 API ===================== */

void obd_dashboard_create(lv_obj_t *parent)
{
    /* 全屏覆盖容器 (默认隐藏)，结构与 flight_instruments 一致 */
    s_overlay = lv_obj_create(parent);
    lv_obj_set_size(s_overlay, OD_SCREEN_SIZE, OD_SCREEN_SIZE);
    lv_obj_center(s_overlay);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);

    /* 中央转速表 */
    create_tachometer(s_overlay);

    /* 顶部车速大字 (km/h) */
    s_speed_label = lv_label_create(s_overlay);
    lv_obj_set_style_text_font(s_speed_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_speed_label, COLOR_SPEED, 0);
    lv_label_set_text(s_speed_label, "-- km/h");
    lv_obj_align(s_speed_label, LV_ALIGN_TOP_MID, 0, 42);

    /* 底部水温条 (60~130°C，>105°C 变红) */
    s_coolant_bar = lv_bar_create(s_overlay);
    lv_obj_set_size(s_coolant_bar, 160, 14);
    lv_obj_align(s_coolant_bar, LV_ALIGN_BOTTOM_MID, 0, -92);
    lv_bar_set_range(s_coolant_bar, OD_COOLANT_MIN, OD_COOLANT_MAX);
    lv_bar_set_value(s_coolant_bar, OD_COOLANT_MIN, LV_ANIM_OFF);
    lv_obj_clear_flag(s_coolant_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_coolant_bar, lv_color_hex(0x111122), 0);
    lv_obj_set_style_bg_opa(s_coolant_bar, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s_coolant_bar, 1, 0);
    lv_obj_set_style_border_color(s_coolant_bar, lv_color_hex(0x333344), 0);
    lv_obj_set_style_radius(s_coolant_bar, 3, 0);
    lv_obj_set_style_bg_color(s_coolant_bar, COLOR_COOLANT_OK, LV_PART_INDICATOR);

    s_coolant_lbl = lv_label_create(s_overlay);
    lv_obj_set_style_text_font(s_coolant_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_coolant_lbl, COLOR_COOLANT_OK, 0);
    lv_label_set_text(s_coolant_lbl, "--C");
    lv_obj_align(s_coolant_lbl, LV_ALIGN_BOTTOM_MID, 0, -68);

    /* 底部小字：电压 / 节气门 / 进气温 */
    s_batt_lbl = lv_label_create(s_overlay);
    lv_obj_set_style_text_font(s_batt_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_batt_lbl, COLOR_TEXT_DIM, 0);
    lv_label_set_text(s_batt_lbl, "--.-V");
    lv_obj_align(s_batt_lbl, LV_ALIGN_BOTTOM_MID, -80, -44);

    s_throttle_lbl = lv_label_create(s_overlay);
    lv_obj_set_style_text_font(s_throttle_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_throttle_lbl, COLOR_TEXT_DIM, 0);
    lv_label_set_text(s_throttle_lbl, "TPS --%");
    lv_obj_align(s_throttle_lbl, LV_ALIGN_BOTTOM_MID, 0, -44);

    s_intake_lbl = lv_label_create(s_overlay);
    lv_obj_set_style_text_font(s_intake_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_intake_lbl, COLOR_TEXT_DIM, 0);
    lv_label_set_text(s_intake_lbl, "IAT --C");
    lv_obj_align(s_intake_lbl, LV_ALIGN_BOTTOM_MID, 80, -44);

    /* 离线提示（默认隐藏，离线时叠加在转速表上方） */
    s_offline_lbl = lv_label_create(s_overlay);
    lv_obj_set_style_text_font(s_offline_lbl, &font_hzk_16, 0);
    lv_obj_set_style_text_color(s_offline_lbl, lv_color_white(), 0);
    lv_obj_set_style_bg_color(s_offline_lbl, lv_color_hex(0x222233), 0);
    lv_obj_set_style_bg_opa(s_offline_lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_offline_lbl, 10, 0);
    lv_obj_set_style_radius(s_offline_lbl, 8, 0);
    lv_label_set_text(s_offline_lbl, "OBD 未连接");
    lv_obj_align(s_offline_lbl, LV_ALIGN_CENTER, 0, -46);
    lv_obj_add_flag(s_offline_lbl, LV_OBJ_FLAG_HIDDEN);

    /* 点击退出回表情页 */
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_overlay, obd_page_click_cb, LV_EVENT_CLICKED, NULL);

    /* 底部提示标签 */
    lv_obj_t *hint = lv_label_create(s_overlay);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, COLOR_TEXT_DIM, 0);
    lv_label_set_text(hint, "tap: exit");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -18);

    /* 默认隐藏 */
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    s_visible = false;

    ESP_LOGI(TAG, "OBD 仪表盘覆盖层创建完成");
}

void obd_dashboard_show(void)
{
    if (!s_overlay) return;
    s_visible = true;
    /* 暂停后台 EAF 表情动画（与 flight_instruments 同策略） */
    emote_engine_pause();
    /* 强制下轮 update 全量刷新（哨兵用 INT_MIN，与 update 中“无效值”-1/-1000 区分，
       避免数据失效后重入页面时陈旧数值被跳过） */
    s_last_rpm = INT_MIN;
    s_last_speed = INT_MIN;
    s_last_throttle = INT_MIN;
    s_last_coolant = INT_MIN;
    s_last_batt = -1e9f;
    s_last_intake = INT_MIN;
    s_last_fast_us = s_last_mid_us = s_last_slow_us = 0;
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

void obd_dashboard_hide(void)
{
    if (!s_overlay) return;
    s_visible = false;
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    /* 恢复 EAF 表情动画播放 */
    emote_engine_resume();
}

bool obd_dashboard_is_visible(void)
{
    return s_visible;
}

void obd_dashboard_update(void)
{
    if (!s_visible) return;

    int64_t now = esp_timer_get_time();
    if (now - s_last_fast_us < OD_FAST_PERIOD_US) return;
    s_last_fast_us = now;

    obd_snapshot_t snap;
    if (obd_manager_get(&snap) != ESP_OK) return;

    char buf[32];

    /* ---- 快速层 @20Hz：指针 + RPM 数字 ---- */
    int rpm = (snap.valid_bits & OBD_VALID_RPM) ? (int)snap.rpm : 0;
    if (rpm != s_last_rpm) {
        s_last_rpm = rpm;
        lv_obj_set_style_transform_angle(s_rpm_needle,
                                         rpm_to_needle_angle((float)rpm), 0);
        if (snap.valid_bits & OBD_VALID_RPM) {
            snprintf(buf, sizeof(buf), "%d", rpm);
        } else {
            snprintf(buf, sizeof(buf), "----");
        }
        lv_label_set_text(s_rpm_label, buf);
    }

    /* ---- 中速层 @5Hz：车速 / 节气门 ---- */
    if (now - s_last_mid_us >= OD_MID_PERIOD_US) {
        s_last_mid_us = now;

        int speed = (snap.valid_bits & OBD_VALID_SPEED) ? (int)snap.speed_kph : -1;
        if (speed != s_last_speed) {
            s_last_speed = speed;
            if (speed >= 0) snprintf(buf, sizeof(buf), "%d km/h", speed);
            else            snprintf(buf, sizeof(buf), "-- km/h");
            lv_label_set_text(s_speed_label, buf);
        }

        int throttle = (snap.valid_bits & OBD_VALID_THROTTLE) ? (int)snap.throttle_pct : -1;
        if (throttle != s_last_throttle) {
            s_last_throttle = throttle;
            if (throttle >= 0) snprintf(buf, sizeof(buf), "TPS %d%%", throttle);
            else               snprintf(buf, sizeof(buf), "TPS --%%");
            lv_label_set_text(s_throttle_lbl, buf);
        }
    }

    /* ---- 慢速层 @1Hz：水温 / 电压 / 进气温 / 在线状态 ---- */
    if (now - s_last_slow_us >= OD_SLOW_PERIOD_US) {
        s_last_slow_us = now;

        int coolant = (snap.valid_bits & OBD_VALID_COOLANT) ? (int)snap.coolant_c : -1000;
        if (coolant != s_last_coolant) {
            s_last_coolant = coolant;
            if (coolant > -1000) {
                int bar_val = coolant;
                if (bar_val < OD_COOLANT_MIN) bar_val = OD_COOLANT_MIN;
                if (bar_val > OD_COOLANT_MAX) bar_val = OD_COOLANT_MAX;
                lv_bar_set_value(s_coolant_bar, bar_val, LV_ANIM_OFF);
                snprintf(buf, sizeof(buf), "%dC", coolant);
                /* 高温告警变红 */
                lv_color_t c = (coolant > OD_COOLANT_WARN) ?
                               lv_palette_main(LV_PALETTE_RED) : COLOR_COOLANT_OK;
                lv_obj_set_style_bg_color(s_coolant_bar, c, LV_PART_INDICATOR);
                lv_obj_set_style_text_color(s_coolant_lbl, c, 0);
            } else {
                lv_bar_set_value(s_coolant_bar, OD_COOLANT_MIN, LV_ANIM_OFF);
                snprintf(buf, sizeof(buf), "--C");
            }
            lv_label_set_text(s_coolant_lbl, buf);
        }

        float batt = (snap.valid_bits & OBD_VALID_BATT) ? snap.batt_v : -1.0f;
        if (fabsf(batt - s_last_batt) >= 0.05f) {
            s_last_batt = batt;
            if (batt >= 0) snprintf(buf, sizeof(buf), "%.1fV", batt);
            else           snprintf(buf, sizeof(buf), "--.-V");
            lv_label_set_text(s_batt_lbl, buf);
        }

        int intake = (snap.valid_bits & OBD_VALID_INTAKE) ? (int)snap.intake_c : -1000;
        if (intake != s_last_intake) {
            s_last_intake = intake;
            if (intake > -1000) snprintf(buf, sizeof(buf), "IAT %dC", intake);
            else                snprintf(buf, sizeof(buf), "IAT --C");
            lv_label_set_text(s_intake_lbl, buf);
        }

        /* 在线状态切换：离线时显示提示标签
           （根覆盖始终 OPA_COVER 不透明——LVGL 8 的 opa 不递归子对象，
            改 opa 会让底下暂停的表情帧透出） */
        if (snap.connected != s_shown_online) {
            s_shown_online = snap.connected;
            if (snap.connected) {
                lv_obj_add_flag(s_offline_lbl, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(s_offline_lbl, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}
