#ifndef _TOUCHPAD_H
#define _TOUCHPAD_H
#include "HAL_conf.h"
#include "stdio.h"
#if USE_RTP

#define  USE_RTP 0
#define TOUCH_X_MIN 450
#define TOUCH_X_MAX 3900
#define TOUCH_Y_MIN 600
#define TOUCH_Y_MAX 3750
void touchpad_Config(void);
extern volatile unsigned char touchInfo_flag;  //GUI用，1有触摸按下
#endif
#endif
