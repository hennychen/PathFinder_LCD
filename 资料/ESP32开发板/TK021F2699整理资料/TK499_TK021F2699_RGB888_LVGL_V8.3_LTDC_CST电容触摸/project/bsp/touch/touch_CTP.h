
#ifndef  __TOUCH_CTP_H__
#define  __TOUCH_CTP_H__
#include "tk499.h"
#include "HAL_conf.h"
#include "TK499_I2C.h"
#include "stdio.h"

#define  USE_CTP 1
#define CPT_addr		0xB1
//#define CPT_addr		0x70		//地址为0x38要移一位 
//#define CPT_addr		0x94		//地址为 0x4a<<1, 要移一位 

extern volatile unsigned char touchInfo_flag;  //GUI用，1有触摸按下
int GUI_TOUCH_Measure(void); 


void Touch_Test(void);

#endif                                     

