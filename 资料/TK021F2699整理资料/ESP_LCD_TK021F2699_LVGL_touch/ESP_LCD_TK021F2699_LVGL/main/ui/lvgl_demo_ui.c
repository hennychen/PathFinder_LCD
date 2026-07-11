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
#include "esp32wifi.h"
#include "led_ws2812.h"

static const char *TAG = "ui";

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
LV_IMG_DECLARE(ui_img_1370113610);    // assets/手电筒 (2).png

LV_FONT_DECLARE(ui_font_Alibaba_PuHuiTi_Font_14);
LV_FONT_DECLARE(ui_font_Alibaba_PuHuiTi_Font_20);
LV_FONT_DECLARE(ui_font_Alibaba_PuHuiTi_Font_32);
LV_FONT_DECLARE(ui_font_Alibaba_PuHuiTi_Font_48);

LV_IMG_DECLARE(crayon_xiao_xin);
LV_IMG_DECLARE(minions_0);
LV_IMG_DECLARE(minions_2);
LV_IMG_DECLARE(minions_5);

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

/* 添加一个标志位来跟踪动画状态 */
static bool is_animating = false;

static lv_obj_t * Panel;
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
static lv_obj_t *signal_text;
static lv_obj_t *signal_label;
static lv_obj_t *menu_itmes[MENU_ITEM_COUNT];  // 菜单对象数组

lv_timer_t *led_timer = NULL;
extern ws2812_strip_handle_t ws2812_handle;

static int current_index = 0;           // 当前选中的菜单索引
static bool wifi_flag = true;
static bool ws2812_flag = true;

static int led_index = 0;

static void led_ws2812_task(void *arg)
{
    while (1)
    {
        if (ws2812_flag)        // 亮灯
        {
            //红色跑马灯
            for(led_index = 0; led_index < WS2812_LED_NUM; led_index++)
            {
                uint32_t r = 230,g = 20,b = 20;
                ws2812_write(ws2812_handle, led_index,r,g,b);
                vTaskDelay(pdMS_TO_TICKS(80));
            }
            //绿色跑马灯
            for(led_index = 0; led_index < WS2812_LED_NUM; led_index++)
            {
                uint32_t r = 20,g = 230,b = 20;
                ws2812_write(ws2812_handle, led_index,r,g,b);
                vTaskDelay(pdMS_TO_TICKS(80));
            }
            //蓝色跑马灯
            for(led_index = 0;led_index < WS2812_LED_NUM; led_index++)
            {
                uint32_t r = 20,g = 20,b = 230;
                ws2812_write(ws2812_handle, led_index,r,g,b);
                vTaskDelay(pdMS_TO_TICKS(80));
            }
        }
        else    // 
        {
            for(led_index = 0;led_index < WS2812_LED_NUM; led_index++)
            {
                uint32_t r = 0,g = 0,b = 0;
                ws2812_write(ws2812_handle, led_index,r,g,b);
                vTaskDelay(pdMS_TO_TICKS(80));
            }
            break;
        }
    }
    vTaskDelete(NULL);
}

static void led_ws2812_cb(lv_timer_t *timer)
{
        //红色跑马灯
        for(led_index = 0; led_index < WS2812_LED_NUM; led_index++)
        {
            uint32_t r = 230,g = 20,b = 20;
            ws2812_write(ws2812_handle, led_index,r,g,b);
            vTaskDelay(pdMS_TO_TICKS(80));
        }
        //绿色跑马灯
        for(led_index = 0; led_index < WS2812_LED_NUM; led_index++)
        {
            uint32_t r = 20,g = 230,b = 20;
            ws2812_write(ws2812_handle, led_index,r,g,b);
            vTaskDelay(pdMS_TO_TICKS(80));
        }
        //蓝色跑马灯
        for(led_index = 0;led_index < WS2812_LED_NUM; led_index++)
        {
            uint32_t r = 20,g = 20,b = 230;
            ws2812_write(ws2812_handle, led_index,r,g,b);
            vTaskDelay(pdMS_TO_TICKS(80));
        }
}

/* 功能选项 */
static void menu_state(int value)
{
    switch (value)
    {
    case 0: 
        lv_label_set_text(ui_state_label, "Wi-Fi"); 
        wifi_flag = true;
        break;
    case 1: 
        lv_label_set_text(ui_state_label, "设置"); 
        wifi_flag = false;
        break;
    case 2: lv_label_set_text(ui_state_label, "音乐"); break;
    case 3: 
        lv_label_set_text(ui_state_label, "亮度"); 
        ws2812_flag = false;
        break;
    case 4: 
        lv_label_set_text(ui_state_label, "手电筒"); 
        ws2812_flag = true;
        xTaskCreate(led_ws2812_task, "led_ws2812_task", 1024 * 4, NULL, 10, NULL);
        break;
    case 5: 
        lv_label_set_text(ui_state_label, "天气"); 
        break;
    case 6: lv_label_set_text(ui_state_label, "闹钟"); break;
    case 7: lv_label_set_text(ui_state_label, "地图"); break;
    case 8: lv_label_set_text(ui_state_label, "通知"); break;
    case 9: 
        lv_label_set_text(ui_state_label, "电池"); 
        wifi_flag = false;
        break;
    default: break;
    }
}

/* 禁用或启用所有图标按钮点击的函数 */
static void set_menu_buttons_enabled(bool enabled)
{
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        if (enabled) {
            lv_obj_add_flag(menu_itmes[i], LV_OBJ_FLAG_CLICKABLE);
            
        } else {
            lv_obj_clear_flag(menu_itmes[i], LV_OBJ_FLAG_CLICKABLE); 
        }
    }
}

/* 检测图标是否落在目标位置 */
static void check_target_pos(lv_anim_t *anim)
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
        }
            count = 0;
            is_animating = false;
            set_menu_buttons_enabled(true);      
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
    if (is_animating) return;
    is_animating = true;
    set_menu_buttons_enabled(false);  // 禁用按钮

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

static void rotste_menu_n(bool clockwise, uint8_t turns)
{
    if (turns == 0 || is_animating) return;  // 如果正在动画，直接返回

    is_animating = true;
    set_menu_buttons_enabled(false);  // 禁用按钮
    
    // 保存最终的索引
    int direction = clockwise ? 1 : -1;
    int final_index = (current_index + (direction * turns) + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
    
    // 计算每个菜单项需要旋转的总角度
    int total_rotation = direction * (360 / MENU_ITEM_COUNT) * turns;
    
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        // 获取菜单项当前的坐标
        lv_coord_t current_x = lv_obj_get_x(menu_itmes[i]);
        lv_coord_t current_y = lv_obj_get_y(menu_itmes[i]);
        
        // 计算当前位置相对于圆心的角度
        float dx = current_x - CENTER_X;
        float dy = current_y - CENTER_Y;
        float current_radian = atan2f(dy, dx);
        int current_angle = (int)(current_radian * 180.0 / 3.1415926);
        
        // 确保角度在0-360范围内
        if (current_angle < 0) current_angle += 360;
        
        // 起始角度使用当前位置
        int start_angle = current_angle;
        int end_angle = current_angle + total_rotation;
        
        // 创建动画
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, menu_itmes[i]);
        lv_anim_set_values(&anim, start_angle, end_angle);
        lv_anim_set_time(&anim, ANIM_TIME * turns);
        lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)anim_cb);
        lv_anim_set_ready_cb(&anim, check_target_pos);
        lv_anim_start(&anim);
    }
    
    // 动画完成后更新索引
    current_index = final_index;
}


/* 将指定图标旋转到顶部（12点钟方向） */
static void rotate_to_top(lv_obj_t *selected_btn)
{
    if (is_animating) return;  // 如果正在动画，直接返回
    is_animating = true;
    set_menu_buttons_enabled(false);  // 禁用按钮

    // 找到点击的按钮索引
    int selected_index = -1;
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        if (menu_itmes[i] == selected_btn) {
            selected_index = i;
            break;
        }
    }
    if (selected_index == -1) return;
    
    // 计算当前点击图标应该位于的顶部位置角度（270度）
    int target_angle = 270;  // 12点钟方向
    
    // 获取当前点击图标的位置
    lv_coord_t current_x = lv_obj_get_x(selected_btn);
    lv_coord_t current_y = lv_obj_get_y(selected_btn);
    
    // 计算当前点击图标的角度
    float dx = current_x - CENTER_X;
    float dy = current_y - CENTER_Y;
    float current_radian = atan2f(dy, dx);
    int current_angle = (int)(current_radian * 180.0 / 3.1415926);
    if (current_angle < 0) current_angle += 360;
    
    // 计算需要旋转的角度差
    int angle_diff = target_angle - current_angle;
    
    // 确保角度差在最优范围内（-180到180度之间）
    if (angle_diff > 180) angle_diff -= 360;
    else if (angle_diff < -180) angle_diff += 360;
    
    // 计算需要转动的步数（每个图标间隔36度）
    int steps = (int)(angle_diff / (360.0 / MENU_ITEM_COUNT));
    
    // 四舍五入到最近的整数步
    float remainder = fabsf(angle_diff - steps * (360.0 / MENU_ITEM_COUNT));
    if (remainder > (360.0 / MENU_ITEM_COUNT / 2)) {
        if (angle_diff > 0) steps++;
        else steps--;
    }
    
    // 如果步数不为0，执行旋转
    if (steps != 0) {
        bool clockwise = (steps > 0);
        int abs_steps = abs(steps);
        
        for (int i = 0; i < MENU_ITEM_COUNT; i++) {
            // 获取每个菜单项当前的位置和角度
            lv_coord_t btn_x = lv_obj_get_x(menu_itmes[i]);
            lv_coord_t btn_y = lv_obj_get_y(menu_itmes[i]);
            
            float btn_dx = btn_x - CENTER_X;
            float btn_dy = btn_y - CENTER_Y;
            float btn_radian = atan2f(btn_dy, btn_dx);
            int btn_angle = (int)(btn_radian * 180.0 / 3.1415926);
            if (btn_angle < 0) btn_angle += 360;
            
            // 计算动画的起始和结束角度
            int start_angle = btn_angle;
            int rotation_per_step = 360 / MENU_ITEM_COUNT;
            int total_rotation = clockwise ? rotation_per_step * abs_steps : -rotation_per_step * abs_steps;
            int end_angle = btn_angle + total_rotation;
            
            // 创建动画
            lv_anim_t anim;
            lv_anim_init(&anim);
            lv_anim_set_var(&anim, menu_itmes[i]);
            lv_anim_set_values(&anim, start_angle, end_angle);
            lv_anim_set_time(&anim, ANIM_TIME * abs_steps);
            lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)anim_cb);
            lv_anim_set_ready_cb(&anim, check_target_pos);
            lv_anim_start(&anim);
        }
        
        // 更新当前索引
        if (clockwise) {
            current_index = (current_index + abs_steps) % MENU_ITEM_COUNT;
        } else {
            current_index = (current_index - abs_steps + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
        }
    }
    
    // 立即更新功能显示
    menu_state(selected_index);
}

lv_obj_t * screen;
lv_timer_t* timer;
static void screen_event_cb(lv_event_t * e);

static void event_up(lv_timer_t * timer)
{
    lv_timer_t* user_data = (lv_timer_t *)timer->user_data;

    lv_obj_add_event_cb(screen, screen_event_cb, LV_EVENT_ALL, NULL);

    if(timer) lv_timer_del(timer);
}

//触摸屏滑动回调
static void screen_event_cb(lv_event_t * e)
{
    if (is_animating) {
        return;  // 如果正在动画，忽略触摸事件
    }

    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t * indev = lv_indev_get_act();
    

    static lv_point_t press_point;
    static int x_0,y_0,x_max=0, y_max=0;
    static int num=0, flag=0;
    if(code == 44)
    {
        lv_indev_get_point(indev, &press_point);
        //ESP_LOGE(TAG, "%d: (%d, %d)\n",(int)code, press_point.x, press_point.y); 
        if(press_point.x ==0 && press_point.y==0)
        {
            num++;
            if(num>2) 
            {
                 if(x_max-x_0 > 10  ) flag=1; //&& (x_0 < 170 || x_0 > 320)
                 else if(y_max-y_0 > 10 ) flag=1; //&& (y_0 < 170 || y_0 > 320)

                //if(x_0 > 170 && x_0 < 320 && y_0 > 170 && y_0 < 320) flag=1;

                if(flag) 
                {
                    lv_obj_remove_event_cb(screen, screen_event_cb);
                    rotste_menu(false);
                    timer = lv_timer_create(event_up, 400, timer);
                }

                num = 0;
                x_max = 0;
                y_max = 0;
                x_0 = 0;
                y_0 = 0;
                flag = 0;
            }
        }
        else
        {
            if(num==1) 
            {
               //num=1; 
               x_0 = press_point.x;
               y_0 = press_point.y;
            }
            if(press_point.x > x_max) x_max = press_point.x;
            if(press_point.y > y_max) y_max = press_point.y;
        }
         
    }

}

/* 颜色设置 */
static lv_obj_t *ui_Container2;
static void btn_event(lv_event_t * e)
{
    //lv_timer_t* user_data = (lv_timer_t *)timer->user_data;

    lv_event_code_t event_code = lv_event_get_code(e);
    lv_obj_t * target = lv_event_get_target(e);
    if(event_code == LV_EVENT_CLICKED)
    {
        if (is_animating) {
            return;  // 如果正在动画，忽略点击
        }

        lv_color_t color = lv_obj_get_style_bg_color(target, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(ui_Container2, color, LV_PART_MAIN | LV_STATE_DEFAULT);

        rotate_to_top(target);
        //gpio_set_level(GPIO_NUM_38 , ~gpio_get_level(GPIO_NUM_38));
    }
    
}

/* 更新 Wi-Fi 信号强度回调函数 */
static void wifi_signal_cb(lv_timer_t *timer)
{
    if (wifi_flag) {
        lv_obj_clear_flag(signal_text, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(signal_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(signal_text, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(signal_label, LV_OBJ_FLAG_HIDDEN);
    }
    int data = get_wifi_signal_strength();
    lv_label_set_text_fmt(signal_label, "%d dbm", data);
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
            if (!is_animating) {
                rotste_menu_n(true, 1);
            }
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
    Panel = lv_obj_create(scr);
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
    lv_obj_set_x(ui_state_label, 0);
    lv_obj_set_y(ui_state_label, 20);
    lv_label_set_text(ui_state_label, "Wi-Fi");
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
      
        // 初始位置设置
        int angle = START_ANGLE_OFFSET + (360 / MENU_ITEM_COUNT) * i;
        int x = CENTER_X + RADIUS * cosf(LV_DEG_TO_RAD(angle));
        int y = CENTER_Y + RADIUS * sinf(LV_DEG_TO_RAD(angle));

        lv_obj_set_pos(menu_itmes[i], x, y);

        lv_obj_add_event_cb(menu_itmes[i], btn_event, LV_EVENT_CLICKED, NULL);
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

    // 手电筒(摄像头)
    lv_obj_set_style_bg_color(menu_itmes[4], lv_color_hex(0x3333D7), LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_camera_img = lv_img_create(menu_itmes[4]);
    lv_img_set_src(ui_camera_img, &ui_img_1370113610);
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

    signal_text = lv_label_create(Panel);
    lv_obj_set_width(signal_text, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(signal_text, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(signal_text, 0);
    lv_obj_set_y(signal_text, 60);
    lv_obj_set_align(signal_text, LV_ALIGN_CENTER);
    lv_obj_add_flag(signal_text, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(signal_text, "Signal strength");
    lv_obj_set_style_text_color(signal_text, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(signal_text, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(signal_text, &ui_font_Alibaba_PuHuiTi_Font_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    signal_label = lv_label_create(Panel);
    lv_obj_set_width(signal_label, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(signal_label, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(signal_label, 0);
    lv_obj_set_y(signal_label, 80);
    lv_obj_set_align(signal_label, LV_ALIGN_CENTER);
    lv_obj_add_flag(signal_label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(signal_label, "00 dbm");
    lv_obj_set_style_text_color(signal_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(signal_label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(signal_label, &ui_font_Alibaba_PuHuiTi_Font_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *gif = lv_gif_create(Panel);
    lv_gif_set_src(gif, &minions_5);
    lv_obj_align(gif, LV_ALIGN_CENTER, 0, -57);

    button_init();
    lv_timer_t *timer = lv_timer_create(wifi_signal_cb, 1000, NULL);



    screen = lv_scr_act();
    //lv_obj_add_event_cb(screen, screen_event_cb, LV_EVENT_ALL, NULL);



    ui_Container2 = lv_obj_create(Panel);
    lv_obj_remove_style_all(ui_Container2);
    lv_obj_set_width(ui_Container2, 20);
    lv_obj_set_height(ui_Container2, 20);
    lv_obj_set_x(ui_Container2, 0);
    lv_obj_set_y(ui_Container2, 115);
    lv_obj_set_align(ui_Container2, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_Container2, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(ui_Container2, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_Container2, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Container2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
}
