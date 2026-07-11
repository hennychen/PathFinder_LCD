#include "lvgl.h"

lv_obj_t * screen;

LV_IMG_DECLARE(loading);
LV_IMG_DECLARE(loading_bar);
LV_IMG_DECLARE(minions_0);
LV_IMG_DECLARE(minions_2);
LV_IMG_DECLARE(minions_5);
LV_IMG_DECLARE(crayon_xiao_xin);

void ui_gif_demo(void)
{
    screen = lv_obj_create(lv_scr_act());
    lv_obj_set_width(screen, 480);
    lv_obj_set_height(screen, 480);
    lv_obj_set_align(screen, LV_ALIGN_CENTER);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(screen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // lv_obj_t *loading_gif = lv_gif_create(screen);
    // lv_gif_set_src(loading_gif, &loading);
    // lv_obj_align(loading_gif, LV_ALIGN_CENTER, 0, 0);

    // lv_obj_t *loading_bar_gif = lv_gif_create(screen);
    // lv_gif_set_src(loading_bar_gif, &loading_bar);
    // lv_obj_align(loading_bar_gif, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *minions_2_gif = lv_gif_create(screen);
    lv_gif_set_src(minions_2_gif, &minions_2);
    lv_obj_align(minions_2_gif, LV_ALIGN_CENTER, 80, 0);

    lv_obj_t *minions_5_gif = lv_gif_create(screen);
    lv_gif_set_src(minions_5_gif, &minions_5);
    lv_obj_align(minions_5_gif, LV_ALIGN_CENTER, -80, 0);

    // lv_obj_t *xiao_xin_gif = lv_gif_create(screen);
    // lv_gif_set_src(xiao_xin_gif, &crayon_xiao_xin);
    // lv_obj_align(xiao_xin_gif, LV_ALIGN_CENTER, -60, 60);

    // lv_obj_t *minions_0_gif = lv_gif_create(screen);
    // lv_gif_set_src(minions_0_gif, &minions_0);
    // lv_obj_align(minions_0_gif, LV_ALIGN_CENTER, 60, 60);
}