/****************************************Copyright (c)****************************************************
** 
**                                      
**
**--------------File Info---------------------------------------------------------------------------------
** File name:			main.c
** modified Date:  		2019-8-12
** Last Version:		V0.1
** Descriptions:		  main 函数调用
** Author : xiao chen
** Historical Version :
** 好钜润科技，芯片事业部----深圳龙华应用分部
*********************************************************************************************************/
#include "exit.h"
#include "main.h"
#include <stdint.h> 

#include  "touch_RTP.h"
#include "SPI.h"
#include "UART.h"
#include "MM_T035.h"
#include "lvgl.h"
#include "LCD.h"
#include "lv_conf.h"
#include "lv_port_disp.h"  ///
#include "lv_port_indev.h" ///


/********************************************************************************************************
**函数信息 ：int main (void)                          
**功能描述 ：无
**输入参数 ：无
**输出参数 ：无
********************************************************************************************************/

int main(void)
{	
	volatile u32 i;	
	AI_Responder_enable();
  RemapVtorTable();
	SystemClk_HSEInit(RCC_PLLMul_20);//启动PLL时钟，12MHz*20=240MHz，25超频慎用
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);//2：2，全局性函数，仅需设置一次
	UartInit(UART1,460800);      //配置串口1，波特率为460800
	printf(" Welcome to use HJR TK499! \r\n");
     
//	TIM8_Config(2400,200);          //配置定时器8，在定时器中断里打印

	LCD_Initial();               //LCD初始化函数
  I2CInitMasterMode(I2C1);
	//EXIT_KEY_Init();	//外部模拟LCD中断按键初始化
	/************/
	Lcd_ColorBox(0,0,XSIZE_PHYS,YSIZE_PHYS,Blue);//用蓝色清屏
	Lcd_ColorBox(300,100,20,20,Red);
	TIM3_Config(3000,240);//3mS
	
//	LCD_PutString(120,40,"好钜润科技物联网部",Red,Yellow,1);

	LCD_PutString(90,60,"Welcome to use HJR TK499 and RGB LCD!",Red,Yellow,1);
//	LCD_PutString(10,80,"深圳市好钜润科技有限公司",Red,Yellow,1);
//	LCD_PutString(10,100,"电话：0755-21006150",Red,Yellow,1);
//	LCD_PutString(10,120," 2.1寸 TK021F2699 液晶屏 ",Red,Yellow,1);
	LCD_Fill_Pic(80,200,320,200,(u32*)gImage_MM_T035);
//	LCD_alpha_Pic(200,180,100,150,Yellow,75);//75透明度， 0~255
		/***********************************************************************************************************************/
		


		lv_init();
    lv_port_disp_init();
	  lv_port_indev_init();
		
//		lv_example_keyboard_1();

//		lv_demo_keypad_encoder();
		lv_demo_widgets();
//		lv_demo_printer();
//	  lv_demo_benchmark();
//    lv_demo_stress();

	while(true)
	{
		lv_timer_handler();
	}

}
