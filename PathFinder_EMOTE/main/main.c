/**
 * @file main.c
 * @brief PathFinder EMOTE — 480×480 圆形屏 车载智能表情终端
 *
 * 硬件：TK021F2699 (ST7701S RGB LCD + CST3530 触摸 + ESP32-S3)
 * 传感器：AHT20(温湿度) + BMP280(气压) + MPU-9250/6500(姿态) + GUVA-S12SD(UV)
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
#include "nvs_flash.h"
#include "lvgl.h"
#include "LCD.h"
#include "app_emote_assets.h"
#include "lv_eaf.h"
#include "sensor_manager.h"
#include "motion_engine.h"
#include "emote_engine.h"
#include "ble_gatt_server.h"
#include "wifi_config_manager.h"
#include "web_portal.h"
#include "provision_screen.h"
#include "flight_instruments.h"
#include "cJSON.h"
#include "mesh_node.h"
#include "mesh_espnow.h"
#include "mesh_protocol.h"

static const char *TAG = "emote";

/* ===================== Mesh 追踪数据存储 ===================== */
static float    s_tracker_angle = 0.0f;     /* B板声源追踪角度 */
static uint8_t  s_tracker_valid = 0;        /* 角度有效标志 */
static uint8_t  s_tracker_state = 0;        /* B板状态机状态 */
static uint8_t  s_child_mac[6] = {0};       /* B板 MAC 地址 */
static int64_t  s_last_heartbeat_us = 0;    /* 上次心跳时间 */
static bool     s_mesh_root_started = false;

/* ===================== LCD 引脚配置 ===================== */
#define LCD_H_RES              480
#define LCD_V_RES              480
#define LCD_PCLK_HZ            (10 * 1000 * 1000)  /* PathFinder_EMOTE 安全基线：多外设并发下 PSRAM 带宽紧张，≥14MHz 会 FIFO under-run 致黑屏 */

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

/* ===================== 传感器 I2C-1 =====================
 * 历史问题：原使用 GPIO12(SCL)/GPIO14(SDA) 但接入传感器后 LCD 黑屏
 * 原因：TK021F2699 板子上 GPIO12/14 被 LCD 信号间接占用（实际验证）
 * 修复：传感器共享触摸屏 I2C-0 总线（GPIO13/20），地址不冲突
 *   - CST3530 触摸  @0x58
 *   - MPU9250         @0x68
 *   - HMC5883L        @0x1E
 *   - AHT20           @0x38
 *   - BMP280          @0x76 / 0x77
 */
#define SENSOR_I2C_NUM        (TOUCH_I2C_NUM)
#define SENSOR_I2C_SCL        (TOUCH_I2C_SCL)
#define SENSOR_I2C_SDA        (TOUCH_I2C_SDA)
#define SENSOR_I2C_CLK_HZ     (100000)   /* 降到 100kHz 提升兼容性（上拉较弱时 400kHz 易 NACK） */

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
#define CAPSULE_ENV_W            240    /* 顶部胶囊宽度 (圆形屏安全区内) */
#define CAPSULE_ENV_H            46     /* 顶部胶囊高度 (适配 montserrat_18) */
#define CAPSULE_ENV_Y            58     /* 顶部胶囊 Y 坐标 */
#define CAPSULE_MOTION_W         290    /* 底部胶囊宽度 (圆形屏安全区内) */
#define CAPSULE_MOTION_H         46     /* 底部胶囊高度 */
#define CAPSULE_MOTION_Y         376    /* 底部胶囊 Y 坐标 */
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

/* ===================== 明细页布局参数 ===================== */
#define DETAIL_ROW_H            38     /* 数据行间距 */
#define DETAIL_ROW_START_Y      108    /* 首行 Y 坐标 */
#define DETAIL_TITLE_Y          58     /* 标题 Y 坐标 */
#define DETAIL_MAX_ROWS         8      /* 最大数据行数 */
#define DETAIL_NAME_X           85     /* 名称列左边距 */
#define DETAIL_VALUE_X          85     /* 数值列右边距 */

typedef enum {
    DETAIL_NONE = 0,      /* 未打开明细页 */
    DETAIL_ENV,           /* 环境传感器明细 */
    DETAIL_MOTION,        /* 运动传感器明细 */
} detail_page_t;

/* ===================== 全局变量 ===================== */
static SemaphoreHandle_t s_lvgl_mux = NULL;

/* VSYNC 帧同步（防撕裂核心） */
static TaskHandle_t      s_lvgl_task_handle = NULL;
static volatile bool     s_flush_pending    = false;
static lv_disp_drv_t    *s_active_disp_drv  = NULL;
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

/* 明细页状态 */
static detail_page_t s_detail_mode  = DETAIL_NONE;
static lv_obj_t     *s_detail_scr   = NULL;   /* 全屏覆盖容器 */
static lv_obj_t     *s_detail_title = NULL;
static lv_obj_t     *s_detail_names[DETAIL_MAX_ROWS];
static lv_obj_t     *s_detail_values[DETAIL_MAX_ROWS];
static lv_obj_t     *s_detail_back  = NULL;

/* 校准模式状态 */
static bool             s_calib_mode = false;
static float            s_calib_altitude = 0;    /* 用户调节的已知海拔 */
static lv_obj_t        *s_calib_lbl_alt   = NULL; /* 海拔显示 */
static lv_obj_t        *s_calib_lbl_p0    = NULL; /* 反推 P0 显示 */
static lv_obj_t        *s_calib_btn_minus = NULL;
static lv_obj_t        *s_calib_btn_plus  = NULL;
static lv_obj_t        *s_calib_btn_ok    = NULL;
static lv_obj_t        *s_calib_btn_reset = NULL;
static lv_obj_t        *s_calib_hint      = NULL;
static lv_obj_t        *s_calib_hint_lbl   = NULL; /* CAL 胶囊按钮内部标签 */

/* 校准回调前向声明 */
static void calib_minus_cb(lv_event_t *e);
static void calib_plus_cb(lv_event_t *e);
static void calib_ok_cb(lv_event_t *e);
static void calib_reset_cb(lv_event_t *e);
static void calib_enter_cb(lv_event_t *e);

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

/* ===================== LCD VSYNC 回调 (ISR 上下文) ===================== */
static bool IRAM_ATTR on_vsync(esp_lcd_panel_handle_t panel,
    const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx)
{
    BaseType_t need_yield = pdFALSE;
    if (s_flush_pending) {
        s_flush_pending = false;
        xTaskNotifyFromISR(s_lvgl_task_handle, 1, eSetBits, &need_yield);
    }
    return (need_yield == pdTRUE);
}

/* ===================== LVGL 刷新回调 ===================== */
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = drv->user_data;
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, color_map);
    /* 延迟 flush_ready 到 VSYNC 信号到来，确保帧缓冲在垂直消隐期切换，杜绝撕裂 */
    s_active_disp_drv = drv;
    s_flush_pending = true;
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

/* ===================== 传感器 I2C 总线初始化 =====================
 * 说明：传感器与触摸屏共享 I2C-0 总线（GPIO13/20）
 * 该总线由 touch_init() 先创建，sensor_i2c_init() 仅复用句柄 */
static esp_err_t sensor_i2c_init(void)
{
    /* 复用触摸 I2C-0 总线句柄（地址不冲突，见 SENSOR_I2C_xxx 注释） */
    s_sensor_i2c_bus = s_touch_i2c_bus;
    ESP_LOGI(TAG, "传感器 I2C 总线复用触摸 I2C-0 (SCL=%d SDA=%d)", SENSOR_I2C_SCL, SENSOR_I2C_SDA);
    return ESP_OK;
}

/* ===================== 胶囊创建 ===================== */

/* 设置胶囊正常态样式 (青色/绿色) */
static void capsule_set_normal_style(lv_obj_t *capsule, lv_obj_t *label, bool is_env)
{
    if (is_env) {
        /* 顶部胶囊 — 青色系 (环境数据) */
        lv_obj_set_style_bg_color(capsule, lv_color_hex(0x00B4FF), 0);
        lv_obj_set_style_bg_opa(capsule, 51, 0);   /* ~20%，提升胶囊辨识度 */
        /* 无边框：去除矩形框，仅靠背景色+阴影呈现胶囊形态 */
        lv_obj_set_style_border_width(capsule, 0, 0);
        /* 纯白粗体感文字：100%不透明最大化对比度 */
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_color(capsule, lv_color_hex(0x0088CC), 0);
    } else {
        /* 底部胶囊 — 绿色系 (运动数据) */
        lv_obj_set_style_bg_color(capsule, lv_color_hex(0x00FF88), 0);
        lv_obj_set_style_bg_opa(capsule, 41, 0);   /* ~16% */
        lv_obj_set_style_border_width(capsule, 0, 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_color(capsule, lv_color_hex(0x00CC66), 0);
    }
    lv_obj_set_style_shadow_width(capsule, 12, 0);
    lv_obj_set_style_shadow_ofs_y(capsule, 2, 0);
    lv_obj_set_style_shadow_spread(capsule, 0, 0);
    lv_obj_set_style_shadow_opa(capsule, LV_OPA_30, 0);
    lv_obj_set_style_radius(capsule, LV_RADIUS_CIRCLE, 0);  /* 全圆角 */
}

/* 设置胶囊异常高亮样式 (红色/黄色) */
static void capsule_set_alert_style(lv_obj_t *capsule, lv_obj_t *label, alert_level_t level)
{
    if (level == ALERT_URGENT) {
        /* 红色紧急 */
        lv_obj_set_style_bg_color(capsule, lv_color_hex(0xFF5050), 0);
        lv_obj_set_style_bg_opa(capsule, LV_OPA_30, 0);
        lv_obj_set_style_border_color(capsule, lv_color_hex(0xFF5050), 0);
        lv_obj_set_style_border_opa(capsule, LV_OPA_70, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFF8888), 0);
        lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_color(capsule, lv_color_hex(0xCC2020), 0);
    } else {
        /* 黄色警告 */
        lv_obj_set_style_bg_color(capsule, lv_color_hex(0xFFB400), 0);
        lv_obj_set_style_bg_opa(capsule, LV_OPA_30, 0);
        lv_obj_set_style_border_color(capsule, lv_color_hex(0xFFB400), 0);
        lv_obj_set_style_border_opa(capsule, LV_OPA_70, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFCC66), 0);
        lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_color(capsule, lv_color_hex(0xCC8800), 0);
    }
    lv_obj_set_style_border_width(capsule, 2, 0);
    lv_obj_set_style_shadow_width(capsule, 14, 0);
    lv_obj_set_style_shadow_ofs_y(capsule, 2, 0);
    lv_obj_set_style_shadow_spread(capsule, 0, 0);
    lv_obj_set_style_shadow_opa(capsule, LV_OPA_40, 0);
}

/* 创建一个胶囊 (矩形 + 全圆角 + 内边距 + 阴影 + 居中标签) */
static lv_obj_t *create_capsule(lv_obj_t *parent, lv_coord_t w, lv_coord_t h,
                                 lv_coord_t y_pos, lv_obj_t **out_label)
{
    lv_obj_t *cap = lv_obj_create(parent);
    lv_obj_set_size(cap, w, h);
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, y_pos);
    lv_obj_clear_flag(cap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(cap, LV_OBJ_FLAG_CLICKABLE);

    /* 水平内边距：让文字不贴圆角边缘，避免裁切色彩断层 */
    lv_obj_set_style_pad_left(cap, 14, 0);
    lv_obj_set_style_pad_right(cap, 14, 0);
    lv_obj_set_style_pad_top(cap, 0, 0);
    lv_obj_set_style_pad_bottom(cap, 0, 0);
    lv_obj_set_style_border_width(cap, 0, 0);  /* 无边框 */
    lv_obj_set_style_outline_width(cap, 0, 0); /* 无外描边 */
    lv_obj_set_style_radius(cap, LV_RADIUS_CIRCLE, 0);  /* 全圆角 */

    /* 边缘阴影：与圆形黑底自然融合，消除生硬切割感 */
    lv_obj_set_style_shadow_width(cap, 12, 0);
    lv_obj_set_style_shadow_ofs_y(cap, 2, 0);
    lv_obj_set_style_shadow_spread(cap, 0, 0);
    lv_obj_set_style_shadow_color(cap, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(cap, LV_OPA_30, 0);

    /* 默认隐藏 */
    lv_obj_set_style_opa(cap, LV_OPA_0, 0);

    /* 居中标签 — montserrat_18 适配 480×480 圆形屏 PPI */
    lv_obj_t *lbl = lv_label_create(cap);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
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

/* ===================== 明细数据页 ===================== */

static int64_t s_last_detail_update_us = 0;

/* 返回回调：隐藏明细页，恢复胶囊显示计时 */
static void detail_back_cb(lv_event_t *e)
{
    (void)e;
    s_detail_mode = DETAIL_NONE;
    lv_obj_add_flag(s_detail_scr, LV_OBJ_FLAG_HIDDEN);
    /* 重置胶囊计时器，返回后给用户 3 秒过渡 */
    if (s_overlay_state == OVERLAY_SHOWN) {
        s_overlay_shown_at_us = esp_timer_get_time();
    }
}

/* 创建明细页覆盖层（在 ui_create 中调用一次） */
static void detail_page_create(lv_obj_t *parent)
{
    /* ---- 全屏覆盖容器 ---- */
    s_detail_scr = lv_obj_create(parent);
    lv_obj_set_size(s_detail_scr, LCD_H_RES, LCD_V_RES);
    lv_obj_center(s_detail_scr);
    lv_obj_clear_flag(s_detail_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_detail_scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_detail_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_detail_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_detail_scr, 0, 0);
    lv_obj_set_style_pad_all(s_detail_scr, 0, 0);
    lv_obj_set_style_radius(s_detail_scr, 0, 0);
    lv_obj_add_event_cb(s_detail_scr, detail_back_cb, LV_EVENT_CLICKED, NULL);

    /* ---- 标题 ---- */
    s_detail_title = lv_label_create(s_detail_scr);
    lv_obj_set_style_text_font(s_detail_title, &lv_font_montserrat_20, 0);
    lv_obj_align(s_detail_title, LV_ALIGN_TOP_MID, 0, DETAIL_TITLE_Y);

    /* ---- 分隔线 ---- */
    lv_obj_t *sep = lv_obj_create(s_detail_scr);
    lv_obj_set_size(sep, 260, 2);
    lv_obj_align(sep, LV_ALIGN_TOP_MID, 0, DETAIL_TITLE_Y + 28);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x333344), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_set_style_pad_all(sep, 0, 0);

    /* ---- 数据行（左名称 + 右数值） ---- */
    /* 名称：montserrat_14 小号灰色；数值：montserrat_24 大号纯白 */
    for (int i = 0; i < DETAIL_MAX_ROWS; i++) {
        s_detail_names[i] = lv_label_create(s_detail_scr);
        lv_obj_set_style_text_font(s_detail_names[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_detail_names[i], lv_color_hex(0x888899), 0);
        lv_obj_align(s_detail_names[i], LV_ALIGN_TOP_LEFT, DETAIL_NAME_X,
                      DETAIL_ROW_START_Y + i * DETAIL_ROW_H + 6);

        s_detail_values[i] = lv_label_create(s_detail_scr);
        lv_obj_set_style_text_font(s_detail_values[i], &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(s_detail_values[i], lv_color_white(), 0);
        lv_obj_align(s_detail_values[i], LV_ALIGN_TOP_RIGHT, -DETAIL_VALUE_X,
                      DETAIL_ROW_START_Y + i * DETAIL_ROW_H);
    }

    /* ---- 返回提示 ---- */
    s_detail_back = lv_label_create(s_detail_scr);
    lv_obj_set_style_text_font(s_detail_back, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_detail_back, lv_color_hex(0x555566), 0);
    lv_label_set_text(s_detail_back, "< tap to back");
    lv_obj_align(s_detail_back, LV_ALIGN_BOTTOM_MID, 0, -25);

    /* ---- 校准 UI 控件 (默认隐藏) ---- */

    /* 校准 CAL 胶囊按钮 (EMA 蓝色胶囊风格) */
    s_calib_hint = lv_obj_create(s_detail_scr);
    lv_obj_set_size(s_calib_hint, 90, 34);
    lv_obj_align(s_calib_hint, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_clear_flag(s_calib_hint, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_calib_hint, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_calib_hint, LV_OBJ_FLAG_HIDDEN);
    /* EMA 胶囊样式：蓝色背景 + 全圆角 + 无边框 + 阴影 */
    lv_obj_set_style_bg_color(s_calib_hint, lv_color_hex(0x00B4FF), 0);
    lv_obj_set_style_bg_opa(s_calib_hint, 51, 0);
    lv_obj_set_style_border_width(s_calib_hint, 0, 0);
    lv_obj_set_style_radius(s_calib_hint, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(s_calib_hint, 0, 0);
    lv_obj_set_style_shadow_color(s_calib_hint, lv_color_hex(0x0088CC), 0);
    lv_obj_set_style_shadow_width(s_calib_hint, 10, 0);
    lv_obj_set_style_shadow_ofs_y(s_calib_hint, 2, 0);
    lv_obj_set_style_shadow_opa(s_calib_hint, LV_OPA_30, 0);
    lv_obj_add_event_cb(s_calib_hint, calib_enter_cb, LV_EVENT_CLICKED, NULL);
    /* 胶囊内部标签 */
    s_calib_hint_lbl = lv_label_create(s_calib_hint);
    lv_obj_set_style_text_font(s_calib_hint_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_calib_hint_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_opa(s_calib_hint_lbl, LV_OPA_COVER, 0);
    lv_label_set_text(s_calib_hint_lbl, "CAL");
    lv_obj_center(s_calib_hint_lbl);

    /* 海拔显示 */
    s_calib_lbl_alt = lv_label_create(s_detail_scr);
    lv_obj_set_style_text_font(s_calib_lbl_alt, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_calib_lbl_alt, lv_color_hex(0x00B4FF), 0);
    lv_obj_align(s_calib_lbl_alt, LV_ALIGN_CENTER, 0, -30);
    lv_obj_add_flag(s_calib_lbl_alt, LV_OBJ_FLAG_HIDDEN);

    /* P0 反推显示 */
    s_calib_lbl_p0 = lv_label_create(s_detail_scr);
    lv_obj_set_style_text_font(s_calib_lbl_p0, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_calib_lbl_p0, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(s_calib_lbl_p0, LV_ALIGN_CENTER, 0, 10);
    lv_obj_add_flag(s_calib_lbl_p0, LV_OBJ_FLAG_HIDDEN);

    /* -10m 按钮 */
    s_calib_btn_minus = lv_obj_create(s_detail_scr);
    lv_obj_set_size(s_calib_btn_minus, 60, 40);
    lv_obj_align(s_calib_btn_minus, LV_ALIGN_CENTER, -80, 60);
    lv_obj_set_style_bg_color(s_calib_btn_minus, lv_color_hex(0x333344), 0);
    lv_obj_set_style_bg_opa(s_calib_btn_minus, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_calib_btn_minus, 8, 0);
    lv_obj_set_style_border_width(s_calib_btn_minus, 0, 0);
    lv_obj_t *lbl_minus = lv_label_create(s_calib_btn_minus);
    lv_label_set_text(lbl_minus, "-10m");
    lv_obj_center(lbl_minus);
    lv_obj_add_event_cb(s_calib_btn_minus, calib_minus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_calib_btn_minus, LV_OBJ_FLAG_HIDDEN);

    /* +10m 按钮 */
    s_calib_btn_plus = lv_obj_create(s_detail_scr);
    lv_obj_set_size(s_calib_btn_plus, 60, 40);
    lv_obj_align(s_calib_btn_plus, LV_ALIGN_CENTER, 80, 60);
    lv_obj_set_style_bg_color(s_calib_btn_plus, lv_color_hex(0x333344), 0);
    lv_obj_set_style_bg_opa(s_calib_btn_plus, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_calib_btn_plus, 8, 0);
    lv_obj_set_style_border_width(s_calib_btn_plus, 0, 0);
    lv_obj_t *lbl_plus = lv_label_create(s_calib_btn_plus);
    lv_label_set_text(lbl_plus, "+10m");
    lv_obj_center(lbl_plus);
    lv_obj_add_event_cb(s_calib_btn_plus, calib_plus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_calib_btn_plus, LV_OBJ_FLAG_HIDDEN);

    /* OK 按钮 */
    s_calib_btn_ok = lv_obj_create(s_detail_scr);
    lv_obj_set_size(s_calib_btn_ok, 70, 36);
    lv_obj_align(s_calib_btn_ok, LV_ALIGN_CENTER, -40, 110);
    lv_obj_set_style_bg_color(s_calib_btn_ok, lv_color_hex(0x00B4FF), 0);
    lv_obj_set_style_bg_opa(s_calib_btn_ok, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_calib_btn_ok, 18, 0);
    lv_obj_set_style_border_width(s_calib_btn_ok, 0, 0);
    lv_obj_t *lbl_ok = lv_label_create(s_calib_btn_ok);
    lv_label_set_text(lbl_ok, "OK");
    lv_obj_set_style_text_color(lbl_ok, lv_color_black(), 0);
    lv_obj_center(lbl_ok);
    lv_obj_add_event_cb(s_calib_btn_ok, calib_ok_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_calib_btn_ok, LV_OBJ_FLAG_HIDDEN);

    /* RESET 按钮 */
    s_calib_btn_reset = lv_obj_create(s_detail_scr);
    lv_obj_set_size(s_calib_btn_reset, 70, 36);
    lv_obj_align(s_calib_btn_reset, LV_ALIGN_CENTER, 40, 110);
    lv_obj_set_style_bg_color(s_calib_btn_reset, lv_color_hex(0x555566), 0);
    lv_obj_set_style_bg_opa(s_calib_btn_reset, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_calib_btn_reset, 18, 0);
    lv_obj_set_style_border_width(s_calib_btn_reset, 0, 0);
    lv_obj_t *lbl_reset = lv_label_create(s_calib_btn_reset);
    lv_label_set_text(lbl_reset, "RESET");
    lv_obj_center(lbl_reset);
    lv_obj_add_event_cb(s_calib_btn_reset, calib_reset_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_calib_btn_reset, LV_OBJ_FLAG_HIDDEN);

    /* 默认隐藏 */
    lv_obj_add_flag(s_detail_scr, LV_OBJ_FLAG_HIDDEN);
}

/* 打开环境传感器明细页 */
static void detail_show_env(void)
{
    s_detail_mode = DETAIL_ENV;
    s_calib_mode = false;

    lv_label_set_text(s_detail_title, "ENVIRONMENT");
    lv_obj_set_style_text_color(s_detail_title, lv_color_hex(0x00B4FF), 0);

    static const char *names[] = {"Temperature", "Humidity", "Pressure", "Altitude", "UV Index", "Sea Level P0"};
    for (int i = 0; i < 6; i++) {
        lv_label_set_text(s_detail_names[i], names[i]);
        lv_obj_clear_flag(s_detail_names[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_detail_values[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_detail_values[i], "--");
    }
    for (int i = 6; i < DETAIL_MAX_ROWS; i++) {
        lv_obj_add_flag(s_detail_names[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_detail_values[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* 隐藏校准控件，显示 CAL 提示 */
    lv_obj_add_flag(s_calib_btn_minus, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_calib_btn_plus, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_calib_btn_ok, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_calib_btn_reset, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_calib_hint, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_calib_hint_lbl, "CAL");
    lv_obj_set_style_text_color(s_calib_hint_lbl, lv_color_white(), 0);

    /* 隐藏校准相关标签 */
    lv_obj_add_flag(s_calib_lbl_alt, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_calib_lbl_p0, LV_OBJ_FLAG_HIDDEN);

    lv_obj_clear_flag(s_detail_scr, LV_OBJ_FLAG_HIDDEN);
}

/* 打开运动传感器明细页 */
static void detail_show_motion(void)
{
    s_detail_mode = DETAIL_MOTION;

    lv_label_set_text(s_detail_title, "MOTION");
    lv_obj_set_style_text_color(s_detail_title, lv_color_hex(0x00FF88), 0);

    static const char *names[] = {"Pitch", "Roll", "Accel X", "Accel Y", "Accel Z",
                                   "Gyro X", "Gyro Y", "Gyro Z"};
    for (int i = 0; i < DETAIL_MAX_ROWS; i++) {
        lv_label_set_text(s_detail_names[i], names[i]);
        lv_obj_clear_flag(s_detail_names[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_detail_values[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_detail_values[i], "--");
    }

    lv_obj_clear_flag(s_detail_scr, LV_OBJ_FLAG_HIDDEN);
}

/* 明细页数据实时更新 (@5Hz) */
static void detail_update(void)
{
    if (s_detail_mode == DETAIL_NONE) return;

    int64_t now = esp_timer_get_time();
    if (now - s_last_detail_update_us < 200000) return;  /* 5Hz 限速 */
    s_last_detail_update_us = now;

    char buf[32];

    if (s_detail_mode == DETAIL_ENV) {
        env_snapshot_t env;
        if (sensor_manager_get_env(&env) == ESP_OK) {
            int hpa = (int)(env.bmp280.pressure / 100.0f);

            snprintf(buf, sizeof(buf), "%.1f °C", env.aht20.temperature);
            lv_label_set_text(s_detail_values[0], buf);

            snprintf(buf, sizeof(buf), "%.0f %%", env.aht20.humidity);
            lv_label_set_text(s_detail_values[1], buf);

            snprintf(buf, sizeof(buf), "%d hPa", hpa);
            lv_label_set_text(s_detail_values[2], buf);

            snprintf(buf, sizeof(buf), "%.0f m", env.bmp280.altitude);
            lv_label_set_text(s_detail_values[3], buf);

            snprintf(buf, sizeof(buf), "%.1f", env.uv.uv_index);
            lv_label_set_text(s_detail_values[4], buf);

            /* Sea Level P0 */
            bmp280_calib_config_t calib = sensor_manager_get_calib();
            snprintf(buf, sizeof(buf), "%.1f hPa", calib.sea_level_pa / 100.0f);
            lv_label_set_text(s_detail_values[5], buf);
        }
    } else if (s_detail_mode == DETAIL_MOTION) {
        float pitch = 0, roll = 0;
        motion_engine_get_angles(&pitch, &roll);

        snprintf(buf, sizeof(buf), "%+.1f °", pitch);
        lv_label_set_text(s_detail_values[0], buf);

        snprintf(buf, sizeof(buf), "%+.1f °", roll);
        lv_label_set_text(s_detail_values[1], buf);

        imu_snapshot_t imu;
        if (sensor_manager_get_imu(&imu) == ESP_OK) {
            snprintf(buf, sizeof(buf), "%+.3f g", imu.imu.accel[0]);
            lv_label_set_text(s_detail_values[2], buf);

            snprintf(buf, sizeof(buf), "%+.3f g", imu.imu.accel[1]);
            lv_label_set_text(s_detail_values[3], buf);

            snprintf(buf, sizeof(buf), "%+.3f g", imu.imu.accel[2]);
            lv_label_set_text(s_detail_values[4], buf);

            snprintf(buf, sizeof(buf), "%+.1f °/s", imu.imu.gyro[0]);
            lv_label_set_text(s_detail_values[5], buf);

            snprintf(buf, sizeof(buf), "%+.1f °/s", imu.imu.gyro[1]);
            lv_label_set_text(s_detail_values[6], buf);

            snprintf(buf, sizeof(buf), "%+.1f °/s", imu.imu.gyro[2]);
            lv_label_set_text(s_detail_values[7], buf);
        }
    }
}

/* ===================== 气压计校准 UI ===================== */

/* 更新校准显示 */
static void calib_update_display(void)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "%.0f m", s_calib_altitude);
    lv_label_set_text(s_calib_lbl_alt, buf);

    /* 实时反推 P0 */
    env_snapshot_t env;
    if (sensor_manager_get_env(&env) == ESP_OK && env.bmp280.pressure > 0) {
        float p0 = drv_bmp280_calc_sea_level(s_calib_altitude, env.bmp280.pressure);
        if (p0 > 0) {
            snprintf(buf, sizeof(buf), "P0 = %.1f hPa", p0 / 100.0f);
            lv_label_set_text(s_calib_lbl_p0, buf);
        }
    }
}

/* -10m 按钮回调 */
static void calib_minus_cb(lv_event_t *e)
{
    (void)e;
    s_calib_altitude -= 10.0f;
    if (s_calib_altitude < -500) s_calib_altitude = -500;
    calib_update_display();
}

/* +10m 按钮回调 */
static void calib_plus_cb(lv_event_t *e)
{
    (void)e;
    s_calib_altitude += 10.0f;
    if (s_calib_altitude > 9000) s_calib_altitude = 9000;
    calib_update_display();
}

/* 确认校准回调 */
static void calib_ok_cb(lv_event_t *e)
{
    (void)e;
    esp_err_t ret = sensor_manager_calib_altitude(s_calib_altitude);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "海拔校准成功: %.0f m", s_calib_altitude);
        lv_label_set_text(s_calib_hint_lbl, "Done!");
        lv_obj_set_style_text_color(s_calib_hint_lbl, lv_color_hex(0x00FF88), 0);
    } else {
        lv_label_set_text(s_calib_hint_lbl, "Err!");
        lv_obj_set_style_text_color(s_calib_hint_lbl, lv_color_hex(0xFF5050), 0);
    }
    /* 退出校准模式 */
    s_calib_mode = false;
    lv_obj_add_flag(s_calib_btn_minus, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_calib_btn_plus, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_calib_btn_ok, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_calib_btn_reset, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_calib_hint_lbl, "CAL");
}

/* 重置校准回调 */
static void calib_reset_cb(lv_event_t *e)
{
    (void)e;
    sensor_manager_calib_reset();
    lv_label_set_text(s_calib_hint_lbl, "CAL");
    lv_obj_set_style_text_color(s_calib_hint_lbl, lv_color_hex(0xFFB400), 0);
    s_calib_mode = false;
    lv_obj_add_flag(s_calib_btn_minus, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_calib_btn_plus, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_calib_btn_ok, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_calib_btn_reset, LV_OBJ_FLAG_HIDDEN);
}

/* CAL 按钮回调：进入校准模式 */
static void calib_enter_cb(lv_event_t *e)
{
    (void)e;
    s_calib_mode = true;

    /* 初始化已知海拔为当前计算值 */
    env_snapshot_t env;
    if (sensor_manager_get_env(&env) == ESP_OK) {
        s_calib_altitude = env.bmp280.altitude;
    } else {
        s_calib_altitude = 0;
    }

    /* 显示校准控件 */
    lv_obj_clear_flag(s_calib_btn_minus, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_calib_btn_plus, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_calib_btn_ok, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_calib_btn_reset, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_calib_lbl_alt, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_calib_lbl_p0, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_calib_hint_lbl, "Adjust");
    lv_obj_set_style_text_color(s_calib_hint_lbl, lv_color_hex(0x00B4FF), 0);
    calib_update_display();
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

    /* 状态机：SHOWN 状态 3 秒后自动淡出 (明细页/仪表页打开时暂停计时) */
    if (s_overlay_state == OVERLAY_SHOWN && s_detail_mode == DETAIL_NONE &&
        !flight_instruments_is_visible()) {
        if (now - s_overlay_shown_at_us >= (int64_t)CAPSULE_DISPLAY_MS * 1000) {
            s_overlay_state = OVERLAY_HIDDEN;
            capsule_fade_out(s_capsule_env);
            capsule_fade_out(s_capsule_motion);
        }
    }

    /* 明细页数据实时更新 */
    detail_update();
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

/* ===================== 长按回调 ===================== */
static void long_press_cb(lv_event_t *e)
{
    (void)e;
    /* 长按：手动切换表情 + 唤出胶囊 */
    emote_engine_manual_next();
    overlay_show_temporary();
}

/* ===================== 点击回调 ===================== */
static void click_cb(lv_event_t *e)
{
    lv_point_t pt;
    lv_indev_get_point(lv_indev_get_act(), &pt);

    /* 胶囊可见时，检测点击是否落在胶囊区域 → 进入明细页 */
    if (s_overlay_state != OVERLAY_HIDDEN) {
        lv_area_t a;

        /* 顶部胶囊 → 环境明细 */
        lv_obj_get_coords(s_capsule_env, &a);
        if (pt.x >= a.x1 && pt.x <= a.x2 && pt.y >= a.y1 && pt.y <= a.y2) {
            detail_show_env();
            return;
        }

        /* 底部胶囊 → 运动明细 */
        lv_obj_get_coords(s_capsule_motion, &a);
        if (pt.x >= a.x1 && pt.x <= a.x2 && pt.y >= a.y1 && pt.y <= a.y2) {
            detail_show_motion();
            return;
        }
    }

    /* 默认行为：进入飞行仪表页 */
    flight_instruments_show();
}

/* ===================== ESP-NOW / Mesh 接收回调 ===================== */

/* 处理收到的追踪消息 (来自 B板) */
static void handle_tracker_msg(const uint8_t *src_mac, const uint8_t *raw, int raw_len)
{
    mesh_msg_t msg;
    if (!mesh_msg_parse(raw, raw_len, &msg)) {
        return;  /* CRC 失败 */
    }

    /* 记录来源 MAC */
    if (src_mac) {
        memcpy(s_child_mac, src_mac, 6);
    }

    switch (msg.msg_type) {
    case MSG_ANGLE_DATA:
        if (msg.payload_len >= 3) {
            uint16_t angle_fixed = (uint16_t)(msg.payload[0] | (msg.payload[1] << 8));
            s_tracker_angle = angle_fixed / 10.0f;
            s_tracker_valid = msg.payload[2];
        }
        break;

    case MSG_TRACK_STATE:
        if (msg.payload_len >= 1) {
            s_tracker_state = msg.payload[0];
        }
        break;

    case MSG_FACE_INFO:
        /* 人脸信息暂时仅记录,后续可更新 UI */
        break;

    case MSG_HEARTBEAT:
        s_last_heartbeat_us = esp_timer_get_time();
        break;

    case MSG_MESH_READY:
        ESP_LOGI(TAG, "B板 Mesh Ready from %02x:%02x:%02x:%02x:%02x:%02x",
                 src_mac[0], src_mac[1], src_mac[2],
                 src_mac[3], src_mac[4], src_mac[5]);
        s_last_heartbeat_us = esp_timer_get_time();
        break;

    default:
        break;
    }
}

/* ESP-NOW 接收回调 (在 WiFi 任务上下文中运行) */
static void on_espnow_rx(const uint8_t *src_mac, const uint8_t *data, int data_len)
{
    handle_tracker_msg(src_mac, data, data_len);
}

/* Mesh P2P 接收回调 (在 mesh_rx_task 中运行) */
static void on_mesh_rx(const uint8_t *from_mac, const uint8_t *data, int data_len)
{
    handle_tracker_msg(from_mac, data, data_len);
}

/* ===================== Wi-Fi 配网状态同步 ===================== */
static void on_wifi_prov_state(wifi_prov_state_t state, const char *ssid, const char *detail)
{
    if (!lvgl_lock(100)) return;

    switch (state) {
    case WIFI_PROV_STATE_PROVISIONING:
        if (!provision_screen_is_visible()) {
            provision_screen_create(lv_disp_get_scr_act(lv_disp_get_default()));
        }
        provision_screen_set_state(PROV_SCREEN_WAITING, NULL, NULL);
        break;

    case WIFI_PROV_STATE_CONNECTING:
        provision_screen_set_state(PROV_SCREEN_CONNECTING, ssid, detail);
        break;

    case WIFI_PROV_STATE_CONNECTED:
        provision_screen_set_state(PROV_SCREEN_CONNECTED, ssid, detail);
        /* 2 秒后销毁配网 UI */
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (lvgl_lock(100)) {
            provision_screen_destroy();
            lvgl_unlock();
        }

        /* 配网成功 → 启动 Mesh ROOT 节点 */
        if (!s_mesh_root_started) {
            const char *r_ssid = wifi_config_manager_get_router_ssid();
            const char *r_pass = wifi_config_manager_get_router_pass();

            ESP_LOGI(TAG, "WiFi 已连接，启动 Mesh ROOT (router='%s')", r_ssid);

            esp_err_t mesh_ret = mesh_node_start_root_after_wifi(r_ssid, r_pass);
            if (mesh_ret != ESP_OK) {
                ESP_LOGE(TAG, "Mesh ROOT 启动失败: %s", esp_err_to_name(mesh_ret));
            } else {
                s_mesh_root_started = true;
            }

            /* ESP-NOW 初始化 (在 Mesh/WiFi 启动后) */
            mesh_espnow_init();
            mesh_espnow_register_rx_cb(on_espnow_rx);

            /* 注册 Mesh P2P 接收回调 */
            mesh_node_register_rx_cb(on_mesh_rx);
        }
        break;

    case WIFI_PROV_STATE_FAILED:
        provision_screen_set_state(PROV_SCREEN_FAILED, ssid, detail);
        break;

    default:
        break;
    }

    lvgl_unlock();

    /* 通过 BLE C5 Notify 通知 App */
    cJSON *json = cJSON_CreateObject();
    const char *state_str = "idle";
    switch (state) {
        case WIFI_PROV_STATE_PROVISIONING: state_str = "provisioning"; break;
        case WIFI_PROV_STATE_CONNECTING:   state_str = "connecting"; break;
        case WIFI_PROV_STATE_CONNECTED:    state_str = "connected"; break;
        case WIFI_PROV_STATE_FAILED:       state_str = "failed"; break;
        default: break;
    }
    cJSON_AddStringToObject(json, "status", state_str);
    if (ssid && strlen(ssid) > 0) cJSON_AddStringToObject(json, "ssid", ssid);
    if (detail && strlen(detail) > 0) {
        if (state == WIFI_PROV_STATE_CONNECTED) {
            cJSON_AddStringToObject(json, "ip", detail);
        } else {
            cJSON_AddStringToObject(json, "error", detail);
        }
    }
    char *json_str = cJSON_PrintUnformatted(json);
    if (json_str) {
        ble_gatt_notify_wifi_status(json_str);
        cJSON_free(json_str);
    }
    cJSON_Delete(json);
}

/* ===================== Skip 配网回调 ===================== */
static void on_prov_skip(void)
{
    ESP_LOGI(TAG, "用户跳过 Wi-Fi 配网, 进入 EAF 表情页");
    wifi_config_manager_skip_provisioning();
}

/* ===================== BLE C5 Write 回调 ===================== */
static void on_ble_wifi_write(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return;

    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    if (!cmd || !cJSON_IsString(cmd)) {
        cJSON_Delete(root);
        return;
    }

    if (strcmp(cmd->valuestring, "set_wifi") == 0) {
        cJSON *ssid_item = cJSON_GetObjectItem(root, "ssid");
        cJSON *pass_item = cJSON_GetObjectItem(root, "pass");
        if (ssid_item && cJSON_IsString(ssid_item)) {
            const char *ssid = ssid_item->valuestring;
            const char *pass = (pass_item && cJSON_IsString(pass_item)) ? pass_item->valuestring : "";
            wifi_config_manager_set_credentials(ssid, pass);
        }
    } else if (strcmp(cmd->valuestring, "get_status") == 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"status\":\"%s\"}",
                 wifi_config_manager_is_connected() ? "connected" : "provisioning");
        ble_gatt_notify_wifi_status(buf);
    } else if (strcmp(cmd->valuestring, "reset_wifi") == 0) {
        wifi_config_manager_reset();
    }

    cJSON_Delete(root);
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
    lv_obj_add_event_cb(s_eaf_obj, long_press_cb, LV_EVENT_LONG_PRESSED, NULL);

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

    /* ---- 飞行仪表盘覆盖层 ---- */
    flight_instruments_create(scr);

    /* ---- 明细页覆盖层（最后创建 → 位于最顶层）---- */
    detail_page_create(scr);
}

/* ===================== LVGL 主任务 (VSYNC 同步) ===================== */
static void lvgl_task(void *arg)
{
    s_lvgl_task_handle = xTaskGetCurrentTaskHandle();
    ESP_LOGI(TAG, "LVGL 任务启动 (VSYNC 帧同步)");
    uint32_t delay = LVGL_TASK_MAX_DELAY;
    uint32_t notify_val = 0;
    while (1) {
        /* flush 等待 VSYNC 时，超时设为 2 帧周期防止死锁 */
        uint32_t wait_ms = s_flush_pending ? 50 : (delay > 0 ? delay : 1);
        if (wait_ms > LVGL_TASK_MAX_DELAY) wait_ms = LVGL_TASK_MAX_DELAY;

        xTaskNotifyWait(0, 1, &notify_val, pdMS_TO_TICKS(wait_ms));

        if (lvgl_lock(-1)) {
            /* VSYNC 到来后通知 LVGL 缓冲已释放，DMA 在消隐期完成了帧切换 */
            if (notify_val & 1) {
                if (s_active_disp_drv) {
                    lv_disp_flush_ready(s_active_disp_drv);
                    s_active_disp_drv = NULL;
                }
            }
            delay = lv_timer_handler();
            overlay_update();
            emote_engine_tick_locked();
            flight_instruments_update();
            lvgl_unlock();
        }

        if (delay > LVGL_TASK_MAX_DELAY) delay = LVGL_TASK_MAX_DELAY;
        else if (delay < LVGL_TASK_MIN_DELAY) delay = LVGL_TASK_MIN_DELAY;
    }
}

/* ===================== BLE 数据推送任务 ===================== */
static void ble_notify_task(void *arg)
{
    ESP_LOGI(TAG, "BLE notify 任务启动");
    vTaskDelay(pdMS_TO_TICKS(3000));  /* 等待传感器稳定 */

    char last_emote_name[32] = {0};
    int  emote_check_counter = 0;

    while (1) {
        /* ── 环境数据 @1Hz ── */
        env_snapshot_t env;
        if (sensor_manager_get_env(&env) == ESP_OK) {
            ble_gatt_notify_env(
                (int16_t)(env.aht20.temperature * 100),
                (uint16_t)(env.aht20.humidity * 100),
                (uint32_t)env.bmp280.pressure,
                (int16_t)(env.bmp280.altitude * 10),
                (uint16_t)(env.uv.uv_index * 100)
            );
        }

        /* ── 运动数据 @10Hz (每 100ms) ── */
        for (int i = 0; i < 10; i++) {
            float pitch = 0, roll = 0;
            motion_engine_get_angles(&pitch, &roll);
            motion_event_t evt = motion_engine_get_event();

            /* 从 IMU 原始数据计算合加速度偏差 */
            imu_snapshot_t imu;
            float accel_mag = 0.0f;
            if (sensor_manager_get_imu(&imu) == ESP_OK) {
                float ax = imu.imu.accel[0];
                float ay = imu.imu.accel[1];
                float az = imu.imu.accel[2];
                float mag = sqrtf(ax * ax + ay * ay + az * az);
                accel_mag = fabsf(mag - 1.0f);  /* 偏离 1g 的量 */
            }

            ble_gatt_notify_motion(
                (int16_t)(pitch * 100),
                (int16_t)(roll * 100),
                (uint16_t)(accel_mag * 1000),
                (uint8_t)evt,
                95  /* confidence placeholder */
            );

            /* ── 表情状态 @2Hz (每 500ms 检查一次) ── */
            if (++emote_check_counter >= 5) {
                emote_check_counter = 0;
                const char *name = emote_engine_get_current_name();
                if (name && strcmp(name, last_emote_name) != 0) {
                    strncpy(last_emote_name, name, sizeof(last_emote_name) - 1);
                    /* 计算 trigger code: 简单映射 */
                    uint8_t trigger = 9;  /* normal */
                    ble_gatt_notify_emote(0, name, trigger);
                }
            }

            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

/* ===================== 应用入口 ===================== */
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  PathFinder EMOTE — Sensor + Emote");
    ESP_LOGI(TAG, "  TK021F2699 + ESP32-S3");
    ESP_LOGI(TAG, "========================================");

    /* ---- NVS 初始化 (必须在 LCD DMA 启动前完成) ---- */
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 需要擦除重建");
        nvs_flash_erase();
        nvs_ret = nvs_flash_init();
    }
    ESP_LOGI(TAG, "NVS 初始化完成");

    /* ---- 提前加载传感器校准参数 (LCD DMA 启动后 spi_flash_read 会 cache error) ---- */
    sensor_manager_preload_calib();

    /* ---- LCD 硬件初始化 (ST7701S 软件 SPI) ---- */
    ESP_LOGI(TAG, "初始化 LCD 硬件");
    Lcd_Initialize();

    /* ---- RGB 面板配置 ---- */
    ESP_LOGI(TAG, "安装 RGB LCD 面板");
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_rgb_panel_config_t panel_cfg = {
        .data_width = 16,
        .num_fbs = 2,
        .bounce_buffer_size_px = LCD_H_RES * 40,  /* 从 80 行减至 40 行，释放 ~110KB 内部 DMA RAM 给 Wi-Fi */
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

    /* ---- BLE GATT Server ---- */
    ESP_LOGI(TAG, "初始化 BLE GATT Server");
    ble_gatt_server_init();

    /* ---- Wi-Fi 配置管理器 ---- */
    ESP_LOGI(TAG, "初始化 Wi-Fi 配置管理器");
    wifi_config_manager_register_cb(on_wifi_prov_state);
    provision_screen_register_skip_cb(on_prov_skip);
    ble_gatt_register_wifi_write_cb(on_ble_wifi_write);
    wifi_config_manager_init();

    /* ---- BLE 数据推送任务 ---- */
    xTaskCreate(ble_notify_task, "BLE_NOTIFY", 6 * 1024, NULL, 2, NULL);

    ESP_LOGI(TAG, "PathFinder EMOTE 初始化完成!");
}
