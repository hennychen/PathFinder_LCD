/**
 * @file provision_screen.c
 * @brief LVGL 配网 UI 覆盖层实现
 *        复用 EMA 胶囊风格 (#00B4FF 蓝 / #00FF88 绿 / #FFB400 黄 / #FF5050 红)
 */
#include "provision_screen.h"
#include "esp_timer.h"

/* ── 颜色常量 (与 main.c EMA 胶囊色系一致) ── */
#define COLOR_BLUE    0x00B4FF
#define COLOR_GREEN   0x00FF88
#define COLOR_YELLOW  0xFFB400
#define COLOR_RED     0xFF5050
#define COLOR_WHITE   0xFFFFFF
#define COLOR_GREY    0x8b949e

static lv_obj_t *s_overlay = NULL;
static lv_obj_t *s_title_label = NULL;
static lv_obj_t *s_ssid_label = NULL;
static lv_obj_t *s_detail_label = NULL;
static lv_obj_t *s_hint_label = NULL;
static lv_obj_t *s_status_capsule = NULL;
static lv_obj_t *s_status_capsule_lbl = NULL;
static lv_obj_t *s_progress_bar = NULL;
static lv_obj_t *s_skip_btn = NULL;
static lv_obj_t *s_pulse_dots[3] = {NULL};
static provision_skip_cb_t s_skip_cb = NULL;

/* 脉冲动画 */
static lv_anim_t s_pulse_anim;
static bool s_pulse_active = false;

/* 前向声明 */
static void pulse_stop(void);

/* 跳过按钮回调: 先通知业务层停 AP，再销毁 UI */
static void skip_btn_cb(lv_event_t *e)
{
    (void)e;
    pulse_stop();
    if (s_skip_cb) {
        s_skip_cb();
    }
    if (s_overlay) {
        lv_obj_del(s_overlay);
        s_overlay = NULL;
        s_title_label = NULL;
        s_ssid_label = NULL;
        s_detail_label = NULL;
        s_hint_label = NULL;
        s_status_capsule = NULL;
        s_status_capsule_lbl = NULL;
        s_progress_bar = NULL;
        s_skip_btn = NULL;
        for (int i = 0; i < 3; i++) {
            s_pulse_dots[i] = NULL;
        }
    }
}

static void pulse_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, v, 0);
}

static void pulse_start(void)
{
    if (s_pulse_active) return;
    for (int i = 0; i < 3; i++) {
        if (s_pulse_dots[i]) {
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, s_pulse_dots[i]);
            lv_anim_set_values(&a, 100, 255);
            lv_anim_set_time(&a, 800);
            lv_anim_set_playback_time(&a, 800);
            lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
            lv_anim_set_exec_cb(&a, pulse_cb);
            lv_anim_set_delay(&a, i * 200);
            lv_anim_start(&a);
        }
    }
    s_pulse_active = true;
}

static void pulse_stop(void)
{
    if (!s_pulse_active) return;
    for (int i = 0; i < 3; i++) {
        if (s_pulse_dots[i]) {
            lv_anim_del(s_pulse_dots[i], pulse_cb);
        }
    }
    s_pulse_active = false;
}

/* 设置边框颜色 */
static void set_border_color(uint32_t color)
{
    lv_obj_set_style_border_color(s_overlay, lv_color_hex(color), 0);
    lv_obj_set_style_border_opa(s_overlay, LV_OPA_COVER, 0);
}

/* 设置标题 */
static void set_title(const char *text, uint32_t color)
{
    lv_label_set_text(s_title_label, text);
    lv_obj_set_style_text_color(s_title_label, lv_color_hex(color), 0);
}

/* 创建一个脉冲点 */
static lv_obj_t *create_pulse_dot(lv_obj_t *parent, lv_coord_t x_offset)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_radius(dot, 4, 0);
    lv_obj_set_style_pad_all(dot, 0, 0);
    lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, x_offset, -30);
    return dot;
}

void provision_screen_create(lv_obj_t *parent)
{
    if (s_overlay) return;

    /* 全屏覆盖容器 */
    s_overlay = lv_obj_create(parent);
    lv_obj_set_size(s_overlay, 480, 480);
    lv_obj_center(s_overlay);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_overlay, 3, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);

    /* 标题标签 */
    s_title_label = lv_label_create(s_overlay);
    lv_obj_set_style_text_font(s_title_label, &lv_font_montserrat_20, 0);
    lv_obj_align(s_title_label, LV_ALIGN_TOP_MID, 0, 100);

    /* SSID 标签 */
    s_ssid_label = lv_label_create(s_overlay);
    lv_obj_set_style_text_font(s_ssid_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ssid_label, lv_color_white(), 0);
    lv_obj_align(s_ssid_label, LV_ALIGN_CENTER, 0, -20);

    /* 详情标签 (IP 或错误) */
    s_detail_label = lv_label_create(s_overlay);
    lv_obj_set_style_text_font(s_detail_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_detail_label, lv_color_hex(COLOR_GREY), 0);
    lv_obj_align(s_detail_label, LV_ALIGN_CENTER, 0, 10);

    /* 状态胶囊标签 (BLE / Web) */
    s_status_capsule = lv_obj_create(s_overlay);
    lv_obj_set_size(s_status_capsule, 200, 32);
    lv_obj_align(s_status_capsule, LV_ALIGN_CENTER, 0, 60);
    lv_obj_clear_flag(s_status_capsule, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_status_capsule, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(s_status_capsule, 38, 0);
    lv_obj_set_style_radius(s_status_capsule, 16, 0);
    lv_obj_set_style_border_width(s_status_capsule, 1, 0);
    lv_obj_set_style_pad_all(s_status_capsule, 0, 0);

    s_status_capsule_lbl = lv_label_create(s_status_capsule);
    lv_obj_set_style_text_font(s_status_capsule_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(s_status_capsule_lbl);

    /* 提示标签 (底部) */
    s_hint_label = lv_label_create(s_overlay);
    lv_obj_set_style_text_font(s_hint_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_hint_label, lv_color_hex(COLOR_GREY), 0);
    lv_obj_align(s_hint_label, LV_ALIGN_BOTTOM_MID, 0, -60);

    /* 进度条 */
    s_progress_bar = lv_bar_create(s_overlay);
    lv_obj_set_size(s_progress_bar, 200, 6);
    lv_obj_align(s_progress_bar, LV_ALIGN_CENTER, 0, 40);
    lv_obj_add_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(0x30363d), 0);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(COLOR_BLUE), LV_PART_INDICATOR);

    /* 脉冲点 (底部) */
    s_pulse_dots[0] = create_pulse_dot(s_overlay, -12);
    s_pulse_dots[1] = create_pulse_dot(s_overlay, 0);
    s_pulse_dots[2] = create_pulse_dot(s_overlay, 12);

    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_bg_color(s_pulse_dots[i], lv_color_hex(COLOR_YELLOW), 0);
    }

    /* 跳过按钮 (右下角) */
    s_skip_btn = lv_obj_create(s_overlay);
    lv_obj_set_size(s_skip_btn, 80, 32);
    lv_obj_align(s_skip_btn, LV_ALIGN_BOTTOM_RIGHT, -16, -16);
    lv_obj_clear_flag(s_skip_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_skip_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(s_skip_btn, 38, 0);
    lv_obj_set_style_bg_color(s_skip_btn, lv_color_hex(COLOR_GREY), 0);
    lv_obj_set_style_radius(s_skip_btn, 16, 0);
    lv_obj_set_style_border_width(s_skip_btn, 1, 0);
    lv_obj_set_style_border_color(s_skip_btn, lv_color_hex(COLOR_GREY), 0);
    lv_obj_set_style_pad_all(s_skip_btn, 0, 0);

    lv_obj_t *skip_lbl = lv_label_create(s_skip_btn);
    lv_obj_set_style_text_font(skip_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(skip_lbl, lv_color_hex(COLOR_GREY), 0);
    lv_label_set_text(skip_lbl, "Skip");
    lv_obj_center(skip_lbl);

    lv_obj_add_event_cb(s_skip_btn, skip_btn_cb, LV_EVENT_CLICKED, NULL);

    /* 默认状态: WAITING */
    provision_screen_set_state(PROV_SCREEN_WAITING, NULL, NULL);
}

void provision_screen_set_state(prov_screen_state_t state, const char *ssid, const char *detail)
{
    if (!s_overlay) return;

    pulse_stop();

    /* 隐藏进度条 (仅 CONNECTING 显示) */
    lv_obj_add_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);

    switch (state) {
    case PROV_SCREEN_WAITING:
        set_border_color(COLOR_YELLOW);
        set_title("WiFi Setup", COLOR_YELLOW);

        lv_label_set_text(s_ssid_label, "PathFinder-EMOTE");
        lv_obj_set_style_text_color(s_ssid_label, lv_color_hex(COLOR_BLUE), 0);

        lv_label_set_text(s_detail_label, "Connect to this hotspot");
        lv_obj_set_style_text_color(s_detail_label, lv_color_hex(COLOR_GREY), 0);

        /* 胶囊: 显示 BLE + Web */
        lv_obj_set_style_bg_color(s_status_capsule, lv_color_hex(COLOR_BLUE), 0);
        lv_obj_set_style_bg_opa(s_status_capsule, 38, 0);
        lv_obj_set_style_border_color(s_status_capsule, lv_color_hex(COLOR_BLUE), 0);
        lv_obj_set_style_border_opa(s_status_capsule, LV_OPA_40, 0);
        lv_label_set_text(s_status_capsule_lbl, "BLE  +  Web  192.168.4.1");
        lv_obj_set_style_text_color(s_status_capsule_lbl, lv_color_hex(COLOR_BLUE), 0);

        lv_label_set_text(s_hint_label, "");

        for (int i = 0; i < 3; i++) {
            lv_obj_clear_flag(s_pulse_dots[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(s_pulse_dots[i], lv_color_hex(COLOR_YELLOW), 0);
        }
        pulse_start();
        break;

    case PROV_SCREEN_CONNECTING:
        set_border_color(COLOR_BLUE);
        set_title("Connecting...", COLOR_BLUE);

        lv_label_set_text(s_ssid_label, ssid ? ssid : "");
        lv_obj_set_style_text_color(s_ssid_label, lv_color_white(), 0);

        lv_label_set_text(s_detail_label, detail ? detail : "");

        lv_obj_set_style_border_color(s_status_capsule, lv_color_hex(COLOR_GREY), 0);
        lv_obj_set_style_border_opa(s_status_capsule, LV_OPA_30, 0);
        lv_label_set_text(s_status_capsule_lbl, "");
        lv_obj_set_style_text_color(s_status_capsule_lbl, lv_color_hex(COLOR_GREY), 0);

        lv_label_set_text(s_hint_label, "");

        /* 显示进度条 */
        lv_obj_clear_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_range(s_progress_bar, 0, 100);
        lv_bar_set_value(s_progress_bar, 50, LV_ANIM_ON);

        /* 隐藏脉冲点 */
        for (int i = 0; i < 3; i++) {
            lv_obj_add_flag(s_pulse_dots[i], LV_OBJ_FLAG_HIDDEN);
        }
        break;

    case PROV_SCREEN_CONNECTED:
        set_border_color(COLOR_GREEN);
        set_title("Connected!", COLOR_GREEN);

        lv_label_set_text(s_ssid_label, ssid ? ssid : "");
        lv_obj_set_style_text_color(s_ssid_label, lv_color_white(), 0);

        lv_label_set_text(s_detail_label, detail ? detail : "");
        lv_obj_set_style_text_color(s_detail_label, lv_color_hex(COLOR_GREEN), 0);

        lv_label_set_text(s_hint_label, "Starting dashboard...");

        for (int i = 0; i < 3; i++) {
            lv_obj_clear_flag(s_pulse_dots[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(s_pulse_dots[i], lv_color_hex(COLOR_GREEN), 0);
        }
        pulse_start();
        break;

    case PROV_SCREEN_FAILED:
        set_border_color(COLOR_RED);
        set_title("Failed", COLOR_RED);

        lv_label_set_text(s_ssid_label, ssid ? ssid : "");
        lv_obj_set_style_text_color(s_ssid_label, lv_color_white(), 0);

        lv_label_set_text(s_detail_label, detail ? detail : "Connection failed");
        lv_obj_set_style_text_color(s_detail_label, lv_color_hex(COLOR_RED), 0);

        lv_label_set_text(s_hint_label, "Returning to setup...");

        for (int i = 0; i < 3; i++) {
            lv_obj_clear_flag(s_pulse_dots[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(s_pulse_dots[i], lv_color_hex(COLOR_RED), 0);
        }
        pulse_start();
        break;
    }
}

void provision_screen_destroy(void)
{
    pulse_stop();
    if (s_overlay) {
        lv_obj_del(s_overlay);
        s_overlay = NULL;
        s_title_label = NULL;
        s_ssid_label = NULL;
        s_detail_label = NULL;
        s_hint_label = NULL;
        s_status_capsule = NULL;
        s_status_capsule_lbl = NULL;
        s_progress_bar = NULL;
        s_skip_btn = NULL;
        for (int i = 0; i < 3; i++) {
            s_pulse_dots[i] = NULL;
        }
    }
}

bool provision_screen_is_visible(void)
{
    return s_overlay != NULL;
}

void provision_screen_register_skip_cb(provision_skip_cb_t cb)
{
    s_skip_cb = cb;
}
