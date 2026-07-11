/**
 * @file main.c
 * @brief PathFinder EMOTE — 480×480 圆形屏 车载智能表情终端
 *
 * 硬件：TK021F2699 (ST7701S RGB LCD + CST3530 触摸 + ESP32-S3)
 * 传感器：AHT20(温湿度) + BMP280(气压) + MPU6050(姿态) + GUVA-S12SD(UV)
 *
 * UI 布局：极简胶囊式（EAF 优先）
 *   - EAF 表情居中最大化 (330×330)，始终可见
 *   - 顶部/底部胶囊数据条：默认隐藏，点击唤出 3 秒淡出
 *   - 异常自动弹出：碰撞/急刹车→红色脉冲，高UV/大倾角→黄色警告
 *   - 基于传感器数据综合评估智能切换表情动画
 */
#include <stdio.h>
#include <math.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "LCD.h"
#include "app_emote_assets.h"
#include "lv_eaf.h"
#include "sensor_manager.h"
#include "motion_engine.h"
#include "emote_engine.h"

static const char *TAG = "emote";

/* ===================== LCD 引脚配置 ===================== */
#define LCD_H_RES              480
#define LCD_V_RES              480
#define LCD_PCLK_HZ            (10 * 1000 * 1000)  /* 10MHz */

#define PIN_HSYNC              41
#define PIN_VSYNC              46
#define PIN_DE                 42
#define PIN_PCLK               2
#define PIN_DATA0              4
#define PIN_DATA1              5
#define PIN_DATA2              6
#define PIN_DATA3              7
#define PIN_DATA4              15
#define PIN_DATA5              16
#define PIN_DATA6              17
#define PIN_DATA7              18
#define PIN_DATA8              9
#define PIN_DATA9              10
#define PIN_DATA10             11
#define PIN_DATA11             0
#define PIN_DATA12             45
#define PIN_DATA13             48
#define PIN_DATA14             47
#define PIN_DATA15             21

/* ===================== 触摸 CST3530 (I2C-0) ===================== */
#define TOUCH_I2C_NUM         (0)
#define TOUCH_I2C_CLK_HZ      (400000)
#define TOUCH_I2C_SCL         (GPIO_NUM_13)
#define TOUCH_I2C_SDA         (GPIO_NUM_20)
#define CPT_ADDR              0x58

/* ===================== 传感器 I2C-1 ===================== */
#define SENSOR_I2C_NUM        (1)
#define SENSOR_I2C_SCL        (GPIO_NUM_12)
#define SENSOR_I2C_SDA        (GPIO_NUM_14)
#define SENSOR_I2C_CLK_HZ     (400000)

/* ===================== LVGL 配置 ===================== */
#define LVGL_TICK_PERIOD_MS   2
#define LVGL_TASK_MAX_DELAY   500
#define LVGL_TASK_MIN_DELAY   1
#define LVGL_TASK_STACK       (12 * 1024)
#define LVGL_TASK_PRIORITY    1

/* ===================== 叠加层更新频率 ===================== */
#define OVERLAY_ENV_PERIOD_MS    1000   /* 环境数据 1Hz */
#define OVERLAY_TILT_PERIOD_MS   200    /* 倾角 5Hz (降低渲染压力) */
#define EMOTE_TICK_PERIOD_MS     200    /* 表情引擎 tick 5Hz (原 10Hz) */

/* ===================== 胶囊布局参数 ===================== */
#define CAPSULE_FADE_MS          200    /* 淡入/淡出动画时长 */
#define CAPSULE_DISPLAY_MS       3000   /* 点击唤出后显示时长 */
#define CAPSULE_ENV_W            200    /* 顶部胶囊宽度 */
#define CAPSULE_ENV_H            38     /* 顶部胶囊高度 */
#define CAPSULE_ENV_Y            68     /* 顶部胶囊 Y 坐标 */
#define CAPSULE_MOTION_W         250    /* 底部胶囊宽度 */
#define CAPSULE_MOTION_H         38     /* 底部胶囊高度 */
#define CAPSULE_MOTION_Y         378    /* 底部胶囊 Y 坐标 */
#define EAF_SIZE                 330    /* EAF 表情区域大小 */
#define NAME_LABEL_Y_OFFSET      -130   /* 名称标签距底部偏移 */

/* ===================== 异常检测阈值 ===================== */
#define ALERT_UV_WARNING         8.0f   /* UV 警告阈值 */
#define ALERT_TILT_WARNING       20.0f  /* 倾角警告阈值 (度) */

/* ===================== Overlay 状态机 ===================== */
typedef enum {
    OVERLAY_HIDDEN = 0,   /* 默认隐藏 */
    OVERLAY_SHOWN,        /* 点击唤出，3秒后淡出 */
    OVERLAY_ALERT,        /* 异常高亮，持续显示 */
} overlay_state_t;

typedef enum {
    ALERT_NONE = 0,       /* 无异常 */
    ALERT_WARNING,        /* 黄色警告 (UV/倾角/颠簸) */
    ALERT_URGENT,         /* 红色紧急 (碰撞/急刹车) */
} alert_level_t;

/* ===================== 全局变量 ===================== */
static SemaphoreHandle_t s_lvgl_mux = NULL;
static i2c_master_bus_handle_t s_touch_i2c_bus;
static i2c_master_dev_handle_t s_touch_i2c_dev;
static i2c_master_bus_handle_t s_sensor_i2c_bus;

static lv_obj_t *s_eaf_obj    = NULL;
static lv_obj_t *s_name_label = NULL;

/* 胶囊数据条 */
static lv_obj_t *s_capsule_env    = NULL;   /* 顶部胶囊 (环境) */
static lv_obj_t *s_capsule_motion = NULL;   /* 底部胶囊 (运动) */
static lv_obj_t *s_lbl_env        = NULL;
static lv_obj_t *s_lbl_motion     = NULL;

/* Overlay 状态 */
static overlay_state_t s_overlay_state = OVERLAY_HIDDEN;
static alert_level_t   s_alert_level   = ALERT_NONE;
static int64_t         s_overlay_shown_at_us = 0;
static lv_anim_t       s_pulse_anim;
static bool             s_pulse_active = false;

/* ===================== LVGL 互斥锁 ===================== */
static bool lvgl_lock(int timeout_ms)
{
    TickType_t ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_lvgl_mux, ticks) == pdTRUE;
}

static void lvgl_unlock(void)
{
    xSemaphoreGiveRecursive(s_lvgl_mux);
}

/* ===================== LCD VSYNC 回调 ===================== */
static bool IRAM_ATTR on_vsync(esp_lcd_panel_handle_t panel,
    const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx)
{
    return false;
}

/* ===================== LVGL 刷新回调 ===================== */
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = drv->user_data;
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, color_map);
    lv_disp_flush_ready(drv);
}

/* ===================== LVGL Tick ===================== */
static void lvgl_tick(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

/* ===================== 触摸初始化 ===================== */
static esp_err_t touch_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = TOUCH_I2C_NUM,
        .sda_io_num = TOUCH_I2C_SDA,
        .scl_io_num = TOUCH_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = 1,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_touch_i2c_bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CPT_ADDR,
        .scl_speed_hz = TOUCH_I2C_CLK_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_touch_i2c_bus, &dev_cfg, &s_touch_i2c_dev));

    ESP_LOGI(TAG, "CST3530 触摸初始化完成 (SCL=%d SDA=%d)", TOUCH_I2C_SCL, TOUCH_I2C_SDA);
    return ESP_OK;
}

/* ===================== 触摸读取回调 ===================== */
static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint8_t buf[10];

    uint8_t wr[4] = {0xD0, 0x07, 0x00, 0x00};
    if (i2c_master_transmit(s_touch_i2c_dev, wr, sizeof(wr), -1) != ESP_OK) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    if (i2c_master_receive(s_touch_i2c_dev, buf, sizeof(buf), -1) != ESP_OK) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    if (buf[8] == 32) {
        data->point.x = buf[4] | ((buf[7] & 0x0f) << 8);
        data->point.y = buf[5] | ((buf[7] & 0xF0) << 4);
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
    uint8_t end[4] = {0xD0, 0x00, 0x02, 0xAB};
    i2c_master_transmit(s_touch_i2c_dev, end, sizeof(end), -1);
}

/* ===================== 传感器 I2C-1 总线初始化 ===================== */
static esp_err_t sensor_i2c_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = SENSOR_I2C_NUM,
        .sda_io_num = SENSOR_I2C_SDA,
        .scl_io_num = SENSOR_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = 1,
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_sensor_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "传感器 I2C-1 总线创建失败: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "传感器 I2C-1 总线就绪 (SCL=%d SDA=%d)", SENSOR_I2C_SCL, SENSOR_I2C_SDA);
    return ESP_OK;
}

/* ===================== 胶囊创建 ===================== */

/* 设置胶囊正常态样式 (青色/绿色) */
static void capsule_set_normal_style(lv_obj_t *capsule, lv_obj_t *label, bool is_env)
{
    if (is_env) {
        /* 顶部胶囊 — 青色系 (环境数据) */
        lv_obj_set_style_bg_color(capsule, lv_color_hex(0x00B4FF), 0);
        lv_obj_set_style_bg_opa(capsule, 38, 0);  /* ~15% */
        lv_obj_set_style_border_color(capsule, lv_color_hex(0x00B4FF), 0);
        lv_obj_set_style_border_opa(capsule, LV_OPA_40, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x00C8FF), 0);
        lv_obj_set_style_text_opa(label, 217, 0);  /* ~85% */
    } else {
        /* 底部胶囊 — 绿色系 (运动数据) */
        lv_obj_set_style_bg_color(capsule, lv_color_hex(0x00FF88), 0);
        lv_obj_set_style_bg_opa(capsule, 31, 0);  /* ~12% */
        lv_obj_set_style_border_color(capsule, lv_color_hex(0x00FF88), 0);
        lv_obj_set_style_border_opa(capsule, LV_OPA_40, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x00FF88), 0);
        lv_obj_set_style_text_opa(label, 204, 0);  /* ~80% */
    }
    lv_obj_set_style_border_width(capsule, 1, 0);
    lv_obj_set_style_radius(capsule, CAPSULE_ENV_H / 2, 0);  /* 全圆角 */
}

/* 设置胶囊异常高亮样式 (红色/黄色) */
static void capsule_set_alert_style(lv_obj_t *capsule, lv_obj_t *label, alert_level_t level)
{
    if (level == ALERT_URGENT) {
        /* 红色紧急 */
        lv_obj_set_style_bg_color(capsule, lv_color_hex(0xFF5050), 0);
        lv_obj_set_style_bg_opa(capsule, LV_OPA_20, 0);
        lv_obj_set_style_border_color(capsule, lv_color_hex(0xFF5050), 0);
        lv_obj_set_style_border_opa(capsule, LV_OPA_70, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFF7878), 0);
        lv_obj_set_style_text_opa(label, 242, 0);  /* ~95% */
    } else {
        /* 黄色警告 */
        lv_obj_set_style_bg_color(capsule, lv_color_hex(0xFFB400), 0);
        lv_obj_set_style_bg_opa(capsule, LV_OPA_20, 0);
        lv_obj_set_style_border_color(capsule, lv_color_hex(0xFFB400), 0);
        lv_obj_set_style_border_opa(capsule, LV_OPA_70, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFC850), 0);
        lv_obj_set_style_text_opa(label, 242, 0);  /* ~95% */
    }
    lv_obj_set_style_border_width(capsule, 2, 0);
}

/* 创建一个胶囊 (矩形 + 全圆角 + 居中标签) */
static lv_obj_t *create_capsule(lv_obj_t *parent, lv_coord_t w, lv_coord_t h,
                                 lv_coord_t y_pos, lv_obj_t **out_label)
{
    lv_obj_t *cap = lv_obj_create(parent);
    lv_obj_set_size(cap, w, h);
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, y_pos);
    lv_obj_clear_flag(cap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(cap, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_style_pad_all(cap, 0, 0);
    lv_obj_set_style_border_width(cap, 1, 0);
    lv_obj_set_style_radius(cap, h / 2, 0);  /* 全圆角 = 高度/2 */

    /* 默认隐藏 */
    lv_obj_set_style_opa(cap, LV_OPA_0, 0);

    /* 居中标签 */
    lv_obj_t *lbl = lv_label_create(cap);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl);
    *out_label = lbl;

    return cap;
}

/* ===================== 胶囊淡入/淡出动画 ===================== */

static void capsule_fade_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, v, 0);
}

static void capsule_fade_in(lv_obj_t *capsule)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, capsule);
    lv_anim_set_values(&a, LV_OPA_0, LV_OPA_COVER);
    lv_anim_set_time(&a, CAPSULE_FADE_MS);
    lv_anim_set_exec_cb(&a, capsule_fade_cb);
    lv_anim_start(&a);
}

static void capsule_fade_out(lv_obj_t *capsule)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, capsule);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_0);
    lv_anim_set_time(&a, CAPSULE_FADE_MS);
    lv_anim_set_exec_cb(&a, capsule_fade_cb);
    lv_anim_start(&a);
}

/* 脉冲动画回调 (border_opa 往复) */
static void pulse_cb(void *obj, int32_t v)
{
    lv_obj_set_style_border_opa((lv_obj_t *)obj, v, 0);
}

static void pulse_start(lv_obj_t *capsule)
{
    if (s_pulse_active) return;
    lv_anim_init(&s_pulse_anim);
    lv_anim_set_var(&s_pulse_anim, capsule);
    lv_anim_set_values(&s_pulse_anim, LV_OPA_10, LV_OPA_70);
    lv_anim_set_time(&s_pulse_anim, 1000);
    lv_anim_set_playback_time(&s_pulse_anim, 1000);
    lv_anim_set_repeat_count(&s_pulse_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&s_pulse_anim, pulse_cb);
    lv_anim_start(&s_pulse_anim);
    s_pulse_active = true;
}

static void pulse_stop(void)
{
    if (!s_pulse_active) return;
    lv_anim_del(&s_pulse_anim.var, pulse_cb);
    s_pulse_active = false;
}

/* ===================== Overlay 状态控制 ===================== */

/* 点击唤出 — 3 秒后自动淡出 */
static void overlay_show_temporary(void)
{
    if (s_overlay_state == OVERLAY_ALERT) return;  /* 异常高亮时不覆盖 */

    s_overlay_state = OVERLAY_SHOWN;
    s_overlay_shown_at_us = esp_timer_get_time();
    capsule_fade_in(s_capsule_env);
    capsule_fade_in(s_capsule_motion);
}

/* 异常自动弹出 */
static void overlay_trigger_alert(alert_level_t level)
{
    if (level <= s_alert_level && s_overlay_state == OVERLAY_ALERT) return;

    s_alert_level = level;
    s_overlay_state = OVERLAY_ALERT;

    /* 设置异常样式 */
    capsule_set_alert_style(s_capsule_env, s_lbl_env, level);
    capsule_set_alert_style(s_capsule_motion, s_lbl_motion, level);

    /* 确保可见 */
    lv_obj_set_style_opa(s_capsule_env, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(s_capsule_motion, LV_OPA_COVER, 0);

    /* 启动脉冲动画 */
    pulse_start(s_capsule_motion);
}

/* 解除异常 → 回到正常样式 → 3 秒后淡出 */
static void overlay_clear_alert(void)
{
    if (s_overlay_state != OVERLAY_ALERT) return;

    s_alert_level = ALERT_NONE;
    pulse_stop();

    /* 恢复正常样式 */
    capsule_set_normal_style(s_capsule_env, s_lbl_env, true);
    capsule_set_normal_style(s_capsule_motion, s_lbl_motion, false);

    /* 设置为显示状态，3 秒后淡出 */
    s_overlay_state = OVERLAY_SHOWN;
    s_overlay_shown_at_us = esp_timer_get_time();
}

/* ===================== 叠加层更新 ===================== */
static int64_t s_last_env_update_us  = 0;
static int64_t s_last_tilt_update_us = 0;
static int64_t s_last_emote_tick_us  = 0;

/* 检测环境异常并触发/解除 overlay */
static void overlay_check_alerts(const env_snapshot_t *env, float pitch, float roll)
{
    float tilt_mag = sqrtf(pitch * pitch + roll * roll);

    /* 检查是否有异常条件 */
    bool uv_warning = (env && env->uv.uv_index >= ALERT_UV_WARNING);
    bool tilt_warning = (tilt_mag >= ALERT_TILT_WARNING);

    if (uv_warning || tilt_warning) {
        /* 有异常 → 触发警告级别 */
        if (s_overlay_state != OVERLAY_ALERT) {
            overlay_trigger_alert(ALERT_WARNING);
        }
    } else {
        /* 无异常 → 如果当前是异常状态，解除 */
        if (s_overlay_state == OVERLAY_ALERT && s_alert_level == ALERT_WARNING) {
            overlay_clear_alert();
        }
    }
}

static void overlay_update(void)
{
    int64_t now = esp_timer_get_time();
    float pitch = 0, roll = 0;
    motion_engine_get_angles(&pitch, &roll);

    /* 环境数据 @1Hz */
    if (now - s_last_env_update_us >= OVERLAY_ENV_PERIOD_MS * 1000) {
        s_last_env_update_us = now;

        env_snapshot_t env;
        bool has_env = (sensor_manager_get_env(&env) == ESP_OK);

        if (has_env) {
            char buf[64];

            /* 顶部胶囊：温/湿/气压 */
            if (s_lbl_env) {
                int hpa = (int)(env.bmp280.pressure / 100.0f);
                snprintf(buf, sizeof(buf), "%.1f°C · %.0f%% · %dhPa",
                         env.aht20.temperature, env.aht20.humidity, hpa);
                lv_label_set_text(s_lbl_env, buf);
            }

            /* 底部胶囊：UV/海拔/倾角 */
            if (s_lbl_motion) {
                snprintf(buf, sizeof(buf), "UV %.1f · %.0fm · P%+.0f R%+.0f",
                         env.uv.uv_index, env.bmp280.altitude, pitch, roll);
                lv_label_set_text(s_lbl_motion, buf);
            }
        }

        /* 检查异常 */
        overlay_check_alerts(has_env ? &env : NULL, pitch, roll);
    }

    /* 倾角 @5Hz (仅在显示状态时更新) */
    if (now - s_last_tilt_update_us >= OVERLAY_TILT_PERIOD_MS * 1000) {
        s_last_tilt_update_us = now;

        /* 仅在胶囊可见时更新底部胶囊的倾角数据 */
        if (s_lbl_motion && s_overlay_state != OVERLAY_HIDDEN) {
            env_snapshot_t env;
            bool has_env = (sensor_manager_get_env(&env) == ESP_OK);
            char buf[64];
            if (has_env) {
                snprintf(buf, sizeof(buf), "UV %.1f · %.0fm · P%+.0f R%+.0f",
                         env.uv.uv_index, env.bmp280.altitude, pitch, roll);
            } else {
                snprintf(buf, sizeof(buf), "P%+.0f R%+.0f", pitch, roll);
            }
            lv_label_set_text(s_lbl_motion, buf);
        }
    }

    /* 状态机：SHOWN 状态 3 秒后自动淡出 */
    if (s_overlay_state == OVERLAY_SHOWN) {
        if (now - s_overlay_shown_at_us >= (int64_t)CAPSULE_DISPLAY_MS * 1000) {
            s_overlay_state = OVERLAY_HIDDEN;
            capsule_fade_out(s_capsule_env);
            capsule_fade_out(s_capsule_motion);
        }
    }
}

/* ===================== 表情引擎 tick (独立调用) ===================== */
static void emote_engine_tick_locked(void)
{
    int64_t now = esp_timer_get_time();
    if (now - s_last_emote_tick_us >= EMOTE_TICK_PERIOD_MS * 1000) {
        s_last_emote_tick_us = now;
        emote_engine_tick();
    }
}

/* ===================== 点击回调 ===================== */
static void click_cb(lv_event_t *e)
{
    (void)e;
    /* 1. 切换表情（保留原有行为） */
    emote_engine_manual_next();
    /* 2. 唤出数据胶囊（如果当前不是异常高亮状态） */
    overlay_show_temporary();
}

/* ===================== UI 创建 ===================== */
static void ui_create(lv_disp_t *disp)
{
    lv_obj_t *scr = lv_disp_get_scr_act(disp);

    /* 屏幕：纯黑背景 */
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    if (!app_emote_assets_is_ready() || app_emote_assets_get_count() == 0) {
        lv_obj_t *fb = lv_label_create(scr);
        lv_label_set_text(fb, "EAF assets not found\n\nFlash emote-assets.bin\nto emote partition");
        lv_obj_set_style_text_font(fb, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(fb, lv_color_white(), 0);
        lv_obj_set_style_text_align(fb, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(fb);
        return;
    }

    /* ---- EAF widget：全屏，继承 lv_img 自动尺寸（数据加载后撞满 480×480）---- */
    s_eaf_obj = lv_eaf_create(scr);
    lv_obj_clear_flag(s_eaf_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_eaf_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_eaf_obj, click_cb, LV_EVENT_CLICKED, NULL);

    /* ---- 名称标签：底部居中 ---- */
    s_name_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_name_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_name_label, lv_color_white(), 0);
    lv_obj_align(s_name_label, LV_ALIGN_BOTTOM_MID, 0, NAME_LABEL_Y_OFFSET);

    /* ---- 顶部胶囊（环境数据）---- */
    s_capsule_env = create_capsule(scr, CAPSULE_ENV_W, CAPSULE_ENV_H, CAPSULE_ENV_Y, &s_lbl_env);
    capsule_set_normal_style(s_capsule_env, s_lbl_env, true);
    lv_label_set_text(s_lbl_env, "--°C · --% · ----hPa");

    /* ---- 底部胶囊（运动数据）---- */
    s_capsule_motion = create_capsule(scr, CAPSULE_MOTION_W, CAPSULE_MOTION_H,
                                       CAPSULE_MOTION_Y, &s_lbl_motion);
    capsule_set_normal_style(s_capsule_motion, s_lbl_motion, false);
    lv_label_set_text(s_lbl_motion, "UV -- · --m · P-- R--");
}

/* ===================== LVGL 主任务 ===================== */
static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL 任务启动");
    uint32_t delay = LVGL_TASK_MAX_DELAY;
    while (1) {
        if (lvgl_lock(-1)) {
            delay = lv_timer_handler();
            overlay_update();
            emote_engine_tick_locked();
            lvgl_unlock();
        }

        if (delay > LVGL_TASK_MAX_DELAY) delay = LVGL_TASK_MAX_DELAY;
        else if (delay < LVGL_TASK_MIN_DELAY) delay = LVGL_TASK_MIN_DELAY;
        vTaskDelay(pdMS_TO_TICKS(delay));
    }
}

/* ===================== 应用入口 ===================== */
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  PathFinder EMOTE — Sensor + Emote");
    ESP_LOGI(TAG, "  TK021F2699 + ESP32-S3");
    ESP_LOGI(TAG, "========================================");

    /* ---- LCD 硬件初始化 (ST7701S 软件 SPI) ---- */
    ESP_LOGI(TAG, "初始化 LCD 硬件");
    Lcd_Initialize();

    /* ---- RGB 面板配置 ---- */
    ESP_LOGI(TAG, "安装 RGB LCD 面板");
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_rgb_panel_config_t panel_cfg = {
        .data_width = 16,
        .num_fbs = 2,
        .bounce_buffer_size_px = LCD_H_RES * 40,
        .clk_src = LCD_CLK_SRC_PLL240M,
        .disp_gpio_num = -1,
        .pclk_gpio_num = PIN_PCLK,
        .vsync_gpio_num = PIN_VSYNC,
        .hsync_gpio_num = PIN_HSYNC,
        .de_gpio_num = PIN_DE,
        .data_gpio_nums = {
            PIN_DATA0, PIN_DATA1, PIN_DATA2, PIN_DATA3,
            PIN_DATA4, PIN_DATA5, PIN_DATA6, PIN_DATA7,
            PIN_DATA8, PIN_DATA9, PIN_DATA10, PIN_DATA11,
            PIN_DATA12, PIN_DATA13, PIN_DATA14, PIN_DATA15,
        },
        .timings = {
            .pclk_hz = LCD_PCLK_HZ,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_back_porch = 9,
            .hsync_front_porch = 4,
            .hsync_pulse_width = 2,
            .vsync_back_porch = 9,
            .vsync_front_porch = 4,
            .vsync_pulse_width = 2,
            .flags.pclk_active_neg = true,
        },
        .flags.fb_in_psram = true,
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_cfg, &panel));

    esp_lcd_rgb_panel_event_callbacks_t cbs = { .on_vsync = on_vsync };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, NULL));

    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_LOGI(TAG, "RGB LCD 面板初始化完成 (%dx%d)", LCD_H_RES, LCD_V_RES);

    /* ---- 初始化 LVGL ---- */
    lv_init();

    void *buf1, *buf2;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &buf1, &buf2));

    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, LCD_H_RES * LCD_V_RES);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel;
    disp_drv.full_refresh = true;
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    /* ---- LVGL Tick ---- */
    esp_timer_create_args_t tick_args = { .callback = lvgl_tick, .name = "lvgl_tick" };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    /* ---- 触摸 (I2C-0) ---- */
    ESP_LOGI(TAG, "初始化 CST3530 触摸");
    touch_init();

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    /* ---- 挂载 EAF 资源 ---- */
    ESP_LOGI(TAG, "挂载 EAF 表情资源");
    app_emote_assets_init();

    /* ---- 传感器 I2C-1 总线 ---- */
    ESP_LOGI(TAG, "初始化传感器 I2C-1 总线");
    sensor_i2c_init();

    /* ---- 运动分析引擎 ---- */
    ESP_LOGI(TAG, "初始化运动分析引擎");
    motion_engine_init();

    /* ---- 创建 UI (EAF + 叠加卡片) ---- */
    ESP_LOGI(TAG, "创建 EMOTE 界面");
    s_lvgl_mux = xSemaphoreCreateRecursiveMutex();
    assert(s_lvgl_mux);
    if (lvgl_lock(-1)) {
        ui_create(disp);
        lvgl_unlock();
    }

    /* ---- 传感器管理器 (初始化驱动 + 启动采样任务) ---- */
    ESP_LOGI(TAG, "初始化传感器管理器");
    sensor_manager_init(s_sensor_i2c_bus);

    /* ---- 表情联动引擎 ---- */
    ESP_LOGI(TAG, "初始化表情联动引擎");
    if (lvgl_lock(-1)) {
        emote_engine_init(s_eaf_obj, s_name_label);
        lvgl_unlock();
    }

    /* ---- 启动 LVGL 主任务 ---- */
    xTaskCreate(lvgl_task, "LVGL", LVGL_TASK_STACK, NULL, LVGL_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "PathFinder EMOTE 初始化完成!");
}
