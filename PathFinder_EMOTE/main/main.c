/**
 * @file main.c
 * @brief PathFinder EMOTE — 480×480 圆形屏 EAF 表情动画播放器
 *
 * 硬件：TK021F2699 (ST7701S RGB LCD + CST3530 触摸 + ESP32-S3)
 * 功能：开机即全屏播放 EAF 动画，点击切换，自动轮播
 *
 * 这是独立项目，不含传感器、LED、多页面等无关功能。
 */
#include <stdio.h>
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

static const char *TAG = "emote";

/* ===================== LCD 引脚配置 ===================== */
#define LCD_H_RES              480
#define LCD_V_RES              480
#define LCD_PCLK_HZ            (10 * 1000 * 1000)  /* 10MHz — 降低 GDMA 读 PSRAM 频率 */

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

/* ===================== 触摸 CST3530 配置 ===================== */
#define TOUCH_I2C_NUM         (0)
#define TOUCH_I2C_CLK_HZ      (400000)
#define TOUCH_I2C_SCL         (GPIO_NUM_13)
#define TOUCH_I2C_SDA         (GPIO_NUM_20)
#define CPT_ADDR              0x58

/* ===================== LVGL 配置 ===================== */
#define LVGL_TICK_PERIOD_MS   2
#define LVGL_TASK_MAX_DELAY   500
#define LVGL_TASK_MIN_DELAY   1
#define LVGL_TASK_STACK       (8 * 1024)  /* 增大栈，EAF 解码需要 */
#define LVGL_TASK_PRIORITY    1           /* 降低优先级，避免饿死 IDLE */

/* ===================== EAF 动画参数 ===================== */
#define EAF_FRAME_DELAY_MS    200   /* 5fps，避免看门狗超时 */
#define EAF_ROTATE_PERIOD_MS  15000 /* 15 秒自动切换 */

/* ===================== 全局变量 ===================== */
static SemaphoreHandle_t s_lvgl_mux = NULL;
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_i2c_dev;

static lv_obj_t   *s_eaf_obj      = NULL;
static lv_obj_t   *s_name_label   = NULL;
static lv_timer_t *s_rotate_timer = NULL;
static int         s_current_idx  = 0;

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
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_i2c_bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CPT_ADDR,
        .scl_speed_hz = TOUCH_I2C_CLK_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_i2c_dev));

    ESP_LOGI(TAG, "CST3530 触摸初始化完成 (SCL=%d SDA=%d)", TOUCH_I2C_SCL, TOUCH_I2C_SDA);
    return ESP_OK;
}

/* ===================== 触摸读取回调 ===================== */
static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint8_t buf[10];

    uint8_t wr[4] = {0xD0, 0x07, 0x00, 0x00};
    if (i2c_master_transmit(s_i2c_dev, wr, sizeof(wr), -1) != ESP_OK) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    if (i2c_master_receive(s_i2c_dev, buf, sizeof(buf), -1) != ESP_OK) {
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
    i2c_master_transmit(s_i2c_dev, end, sizeof(end), -1);
}

/* ===================== EAF 动画切换 ===================== */
static void switch_animation(int idx)
{
    if (!s_eaf_obj) return;

    size_t count = app_emote_assets_get_count();
    if (count == 0) return;
    if (idx < 0) idx = (int)count - 1;
    if (idx >= (int)count) idx = 0;
    s_current_idx = idx;

    size_t sz = 0;
    const void *data = app_emote_assets_get_data((size_t)idx, &sz);
    if (!data || sz == 0) {
        ESP_LOGW(TAG, "动画 %d 数据为空", idx);
        return;
    }

    lv_eaf_set_src_data(s_eaf_obj, data, sz);
    lv_eaf_set_loop_count(s_eaf_obj, -1);
    lv_eaf_set_frame_delay(s_eaf_obj, EAF_FRAME_DELAY_MS);

    const char *name = app_emote_assets_get_name((size_t)idx);
    if (name && s_name_label)
        lv_label_set_text(s_name_label, name);

    /* 强制布局同步后打印精确坐标 */
    lv_obj_update_layout(s_eaf_obj);
    ESP_LOGI(TAG, "播放 [%d] %s (%zuB) eaf:(%d,%d %dx%d) lbl:(%d,%d %dx%d)",
             idx, name ? name : "?", sz,
             lv_obj_get_x(s_eaf_obj), lv_obj_get_y(s_eaf_obj),
             lv_obj_get_width(s_eaf_obj), lv_obj_get_height(s_eaf_obj),
             lv_obj_get_x(s_name_label), lv_obj_get_y(s_name_label),
             lv_obj_get_width(s_name_label), lv_obj_get_height(s_name_label));
}

/* ===================== 事件回调 ===================== */
static void click_cb(lv_event_t *e)
{
    (void)e;
    switch_animation(s_current_idx + 1);
    if (s_rotate_timer) lv_timer_reset(s_rotate_timer);
}

static void rotate_cb(lv_timer_t *t)
{
    (void)t;
    switch_animation(s_current_idx + 1);
}

/* ===================== UI 创建 ===================== */
static void ui_create(lv_disp_t *disp)
{
    lv_obj_t *scr = lv_disp_get_scr_act(disp);

    /* ---- 屏幕：最小化配置（对比原项目） ---- */
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

    /* ---- EAF widget：最小化配置（不设size，不设scroll，对比原项目） ---- */
    s_eaf_obj = lv_eaf_create(scr);
    lv_obj_clear_flag(s_eaf_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_eaf_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_eaf_obj, click_cb, LV_EVENT_CLICKED, NULL);

    /* ---- 名称标签：底部居中 ---- */
    s_name_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_name_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_name_label, lv_color_white(), 0);
    lv_obj_align(s_name_label, LV_ALIGN_BOTTOM_MID, 0, -30);

    /* 加载第一个动画 */
    s_current_idx = 0;
    switch_animation(0);

    /* 自动轮播 */
    s_rotate_timer = lv_timer_create(rotate_cb, EAF_ROTATE_PERIOD_MS, NULL);
}

/* ===================== LVGL 主任务 ===================== */
static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL 任务启动");
    uint32_t delay = LVGL_TASK_MAX_DELAY;
    while (1) {
        if (lvgl_lock(-1)) {
            delay = lv_timer_handler();
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
    ESP_LOGI(TAG, "  PathFinder EMOTE");
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
        .num_fbs = 2,                              /* 双帧缓冲防撕裂 */
        .bounce_buffer_size_px = LCD_H_RES * 40,    /* 40 行 Bounce Buffer 防 FIFO under-run */
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

    /* ---- 触摸 ---- */
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

    /* ---- 创建 UI ---- */
    ESP_LOGI(TAG, "创建 EMOTE 界面");
    s_lvgl_mux = xSemaphoreCreateRecursiveMutex();
    assert(s_lvgl_mux);
    if (lvgl_lock(-1)) {
        ui_create(disp);
        lvgl_unlock();
    }

    /* ---- 启动 LVGL 主任务 ---- */
    xTaskCreate(lvgl_task, "LVGL", LVGL_TASK_STACK, NULL, LVGL_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "PathFinder EMOTE 初始化完成!");
}
