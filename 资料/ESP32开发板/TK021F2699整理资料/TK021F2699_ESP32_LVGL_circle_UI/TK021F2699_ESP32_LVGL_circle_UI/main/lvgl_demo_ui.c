/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

// This demo UI is adapted from LVGL official example: https://docs.lvgl.io/master/examples.html#scatter-chart

#include <stdio.h>
#include <math.h>

#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

LV_IMG_DECLARE(ui_img_534919753);    // assets/wifi (2).png
LV_IMG_DECLARE(ui_img_1063244380);    // assets/设置3.png
LV_IMG_DECLARE(ui_img_1413029994);    // assets/音乐.png
LV_IMG_DECLARE(ui_img_1663005091);    // assets/灯光.png
LV_IMG_DECLARE(ui_img_2128317734);    // assets/摄像头.png
LV_IMG_DECLARE(ui_img_559735659);    // assets/天气 (2).png
LV_IMG_DECLARE(ui_img_608756609);    // assets/闹钟.png
LV_IMG_DECLARE(ui_img_1836530665);    // assets/地图 (1).png
LV_IMG_DECLARE(ui_img_1301454848);    // assets/通知.png
LV_IMG_DECLARE(ui_img_1152878764);    // assets/电池电量.png

LV_FONT_DECLARE(ui_font_Alibaba_PuHuiTi_Font_48);

/* 角度转弧度 */
#define LV_DEG_TO_RAD(deg)  ((deg) * 3.1415926 / 180.0)

/* 按键 GPIO 定义 */
#define BUTTON_GPIO     GPIO_NUM_0

/* 定义环形菜单的参数 */
#define RADIUS          180     // 半径
#define MENU_ITEM_COUNT 10      // 菜单项数量
#define ANIM_TIME       700    // 动画时间（毫秒）

/* 菜单中心 */
#define CENTER_X        185
#define CENTER_Y        185

#define START_ANGLE_OFFSET  270      // 起始角度偏移

static lv_obj_t * ui_wifi_img;
static lv_obj_t * ui_set_img;
static lv_obj_t * ui_music_img;
static lv_obj_t * ui_lamp_img;
static lv_obj_t * ui_camera_img;
static lv_obj_t * ui_weather_img;
static lv_obj_t * ui_clock_img;
static lv_obj_t * ui_map_img;
static lv_obj_t * ui_notif_img;
static lv_obj_t * ui_battery_img;

static lv_obj_t *ui_state_label;
static lv_obj_t *menu_itmes[MENU_ITEM_COUNT];  // 菜单对象数组

static int current_index = 0;           // 当前选中的菜单索引

/* 功能选项 */
static void menu_state(int value)
{
    switch (value)
    {
    case 0: lv_label_set_text(ui_state_label, "WiFi"); break;
    case 1: lv_label_set_text(ui_state_label, "设置"); break;
    case 2: lv_label_set_text(ui_state_label, "音乐"); break;
    case 3: lv_label_set_text(ui_state_label, "亮度"); break;
    case 4: lv_label_set_text(ui_state_label, "摄像头"); break;
    case 5: lv_label_set_text(ui_state_label, "天气"); break;
    case 6: lv_label_set_text(ui_state_label, "闹钟"); break;
    case 7: lv_label_set_text(ui_state_label, "地图"); break;
    case 8: lv_label_set_text(ui_state_label, "通知"); break;
    case 9: lv_label_set_text(ui_state_label, "电池"); break;
    default: break;
    }
}

/* 检测图标是否落在目标位置 */
static void check_target_pos(void)
{
    static int count = 0;
    count++;
    if (count >= MENU_ITEM_COUNT -1) {
            for (int i = 0; i < MENU_ITEM_COUNT; i++) {
            // 获取图标当前位置
            lv_coord_t x = lv_obj_get_x(menu_itmes[i]);
            lv_coord_t y = lv_obj_get_y(menu_itmes[i]);
          
            // 检测图标是否落在 (0,185) 位置附近
            if (160 < x && x < 185 && y > 0 && y < 10) {
                // 更新当前功能目标
                menu_state(i);
             
                // printf("menu_itmes[%d] x= %d, y= %d\n", i, x, y);
            }
            count = 0;
        }
    } 
}

/* 动画执行回调：更新菜单项的位置 */
static void anim_cb(void *obj, int32_t value)
{
    lv_obj_t *btn = (lv_obj_t *)obj;
   
    // 根据 value (角度) 计算新坐标
    int x = CENTER_X + RADIUS * cosf(LV_DEG_TO_RAD(value));
    int y = CENTER_Y + RADIUS * sinf(LV_DEG_TO_RAD(value));

    lv_obj_set_pos(btn, x, y);
} 

/* 旋转菜单动画函数 */
static void rotste_menu(bool clockwise)
{
    current_index = (current_index + (clockwise ? 1 : -1) + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;

    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        // 起始角度
        int start_angle = (START_ANGLE_OFFSET - 360 / MENU_ITEM_COUNT) + \
            (360 / MENU_ITEM_COUNT) * ((i - current_index + MENU_ITEM_COUNT) % MENU_ITEM_COUNT);

        // 目标角度
        int end_angle = start_angle + (clockwise ? -360 / MENU_ITEM_COUNT : 360 / MENU_ITEM_COUNT);

        // 创建动画
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, menu_itmes[i]);
        lv_anim_set_values(&anim, start_angle, end_angle);
        lv_anim_set_time(&anim, ANIM_TIME);
        lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)anim_cb);
        lv_anim_set_ready_cb(&anim, check_target_pos);  // 动画完成后检查位置
        lv_anim_start(&anim);
    }
}

/* 按键任务 */
static void button_task(void *arg)
{
    bool last_state = true;

    while (1)
    {
        bool current_state = gpio_get_level(BUTTON_GPIO);

        // 检测按钮按下（下降沿）
        if (!current_state && last_state) {
            rotste_menu(false);
        }

        last_state = current_state;
        vTaskDelay(pdMS_TO_TICKS(50));  // 按键扫描周期
    }
}

/* 按键初始化 */
void button_init(void)
{
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL<<BUTTON_GPIO), 
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = 0,
        .pull_up_en = 1,
    };
    gpio_config(&io_cfg);

    // 创建按键扫描任务
    xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);
}

void lvgl_demo_ui(lv_disp_t *disp)
{
    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    lv_obj_t *Panel = lv_obj_create(scr);
    lv_obj_set_width(Panel, 480);
    lv_obj_set_height(Panel, 480);
    lv_obj_set_align(Panel, LV_ALIGN_CENTER);
    lv_obj_clear_flag(Panel, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(Panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(Panel, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(Panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(Panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *ui_Panel3 = lv_obj_create(Panel);
    lv_obj_set_width(ui_Panel3, 100);
    lv_obj_set_height(ui_Panel3, 100);
    lv_obj_set_x(ui_Panel3, 0);
    lv_obj_set_y(ui_Panel3, -230);
    lv_obj_set_align(ui_Panel3, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_Panel3, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(ui_Panel3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_Panel3, lv_color_hex(0xA4A4A4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Panel3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Panel3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *ui_Panel2 = lv_obj_create(Panel);
    lv_obj_set_width(ui_Panel2, 100);
    lv_obj_set_height(ui_Panel2, 100);
    lv_obj_set_x(ui_Panel2, 0);
    lv_obj_set_y(ui_Panel2, -180);
    lv_obj_set_align(ui_Panel2, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_Panel2, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(ui_Panel2, 50, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_Panel2, lv_color_hex(0xA4A4A4), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Panel2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Panel2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_state_label = lv_label_create(Panel);
    lv_obj_set_width(ui_state_label, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_state_label, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_align(ui_state_label, LV_ALIGN_CENTER);
    lv_label_set_text(ui_state_label, "WiFi");
    lv_obj_set_style_text_color(ui_state_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_state_label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_state_label, &ui_font_Alibaba_PuHuiTi_Font_48, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 创建菜单项
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        menu_itmes[i] = lv_btn_create(Panel);
        lv_obj_set_size(menu_itmes[i], 80, 80);
        lv_obj_set_style_radius(menu_itmes[i], 40, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_ofs_x(menu_itmes[i], 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_ofs_y(menu_itmes[i], 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(menu_itmes[i], lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        
        // 添加标签
        // lv_obj_t *label = lv_label_create(menu_itmes[i]);
        // lv_obj_set_style_text_color(label, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        // lv_obj_set_style_text_font(label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

        // lv_label_set_text_fmt(label, "%d", i);
        // lv_obj_set_align(label, LV_ALIGN_CENTER);
      
        // 初始位置设置
        int angle = START_ANGLE_OFFSET + (360 / MENU_ITEM_COUNT) * i;
        int x = CENTER_X + RADIUS * cosf(LV_DEG_TO_RAD(angle));
        int y = CENTER_Y + RADIUS * sinf(LV_DEG_TO_RAD(angle));

        lv_obj_set_pos(menu_itmes[i], x, y);
    }

    // Wi-Fi
    lv_obj_set_style_bg_color(menu_itmes[0], lv_color_hex(0x46DB46), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *ui_wifi_img = lv_img_create(menu_itmes[0]);
    lv_img_set_src(ui_wifi_img, &ui_img_534919753);
    lv_obj_set_width(ui_wifi_img, LV_SIZE_CONTENT);   /// 40
    lv_obj_set_height(ui_wifi_img, LV_SIZE_CONTENT);    /// 32
    lv_obj_set_align(ui_wifi_img, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_wifi_img, LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_clear_flag(ui_wifi_img, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    // 设置
    lv_obj_set_style_bg_color(menu_itmes[1], lv_color_hex(0x313131), LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_set_img = lv_img_create(menu_itmes[1]);
    lv_img_set_src(ui_set_img, &ui_img_1063244380);
    lv_obj_set_width(ui_set_img, LV_SIZE_CONTENT);   /// 48
    lv_obj_set_height(ui_set_img, LV_SIZE_CONTENT);    /// 48
    lv_obj_set_align(ui_set_img, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_set_img, LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_clear_flag(ui_set_img, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    // 音乐
    lv_obj_set_style_bg_color(menu_itmes[2], lv_color_hex(0xFF2A2A), LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_music_img = lv_img_create(menu_itmes[2]);
    lv_img_set_src(ui_music_img, &ui_img_1413029994);
    lv_obj_set_width(ui_music_img, LV_SIZE_CONTENT);   /// 48
    lv_obj_set_height(ui_music_img, LV_SIZE_CONTENT);    /// 48
    lv_obj_set_x(ui_music_img, -3);
    lv_obj_set_y(ui_music_img, 0);
    lv_obj_set_align(ui_music_img, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_music_img, LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_clear_flag(ui_music_img, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    // 灯
    lv_obj_set_style_bg_color(menu_itmes[3], lv_color_hex(0xFFA500), LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_lamp_img = lv_img_create(menu_itmes[3]);
    lv_img_set_src(ui_lamp_img, &ui_img_1663005091);
    lv_obj_set_width(ui_lamp_img, LV_SIZE_CONTENT);   /// 48
    lv_obj_set_height(ui_lamp_img, LV_SIZE_CONTENT);    /// 48
    lv_obj_set_align(ui_lamp_img, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_lamp_img, LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_clear_flag(ui_lamp_img, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    // 摄像头
    lv_obj_set_style_bg_color(menu_itmes[4], lv_color_hex(0x3333D7), LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_camera_img = lv_img_create(menu_itmes[4]);
    lv_img_set_src(ui_camera_img, &ui_img_2128317734);
    lv_obj_set_width(ui_camera_img, LV_SIZE_CONTENT);   /// 48
    lv_obj_set_height(ui_camera_img, LV_SIZE_CONTENT);    /// 48
    lv_obj_set_x(ui_camera_img, 2);
    lv_obj_set_y(ui_camera_img, 2);
    lv_obj_set_align(ui_camera_img, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_camera_img, LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_clear_flag(ui_camera_img, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    // 天气
    lv_obj_set_style_bg_color(menu_itmes[5], lv_color_hex(0x00BFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_weather_img = lv_img_create(menu_itmes[5]);
    lv_img_set_src(ui_weather_img, &ui_img_559735659);
    lv_obj_set_width(ui_weather_img, LV_SIZE_CONTENT);   /// 49
    lv_obj_set_height(ui_weather_img, LV_SIZE_CONTENT);    /// 48
    lv_obj_set_align(ui_weather_img, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_weather_img, LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_clear_flag(ui_weather_img, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    // 闹钟
    lv_obj_set_style_bg_color(menu_itmes[6], lv_color_hex(0x707070), LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_clock_img = lv_img_create(menu_itmes[6]);
    lv_img_set_src(ui_clock_img, &ui_img_608756609);
    lv_obj_set_width(ui_clock_img, LV_SIZE_CONTENT);   /// 48
    lv_obj_set_height(ui_clock_img, LV_SIZE_CONTENT);    /// 48
    lv_obj_set_align(ui_clock_img, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_clock_img, LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_clear_flag(ui_clock_img, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    // 地图
    lv_obj_set_style_bg_color(menu_itmes[7], lv_color_hex(0x0047F9), LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_map_img = lv_img_create(menu_itmes[7]);
    lv_img_set_src(ui_map_img, &ui_img_1836530665);
    lv_obj_set_width(ui_map_img, LV_SIZE_CONTENT);   /// 48
    lv_obj_set_height(ui_map_img, LV_SIZE_CONTENT);    /// 48
    lv_obj_set_align(ui_map_img, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_map_img, LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_clear_flag(ui_map_img, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    // 通知
    lv_obj_set_style_bg_color(menu_itmes[8], lv_color_hex(0x77A7EA), LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_notif_img = lv_img_create(menu_itmes[8]);
    lv_img_set_src(ui_notif_img, &ui_img_1301454848);
    lv_obj_set_width(ui_notif_img, LV_SIZE_CONTENT);   /// 48
    lv_obj_set_height(ui_notif_img, LV_SIZE_CONTENT);    /// 48
    lv_obj_set_align(ui_notif_img, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_notif_img, LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_clear_flag(ui_notif_img, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    // 电池
    lv_obj_set_style_bg_color(menu_itmes[9], lv_color_hex(0x00D000), LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_battery_img = lv_img_create(menu_itmes[9]);
    lv_img_set_src(ui_battery_img, &ui_img_1152878764);
    lv_obj_set_width(ui_battery_img, LV_SIZE_CONTENT);   /// 48
    lv_obj_set_height(ui_battery_img, LV_SIZE_CONTENT);    /// 48
    lv_obj_set_align(ui_battery_img, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_battery_img, LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_clear_flag(ui_battery_img, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    button_init();
}
