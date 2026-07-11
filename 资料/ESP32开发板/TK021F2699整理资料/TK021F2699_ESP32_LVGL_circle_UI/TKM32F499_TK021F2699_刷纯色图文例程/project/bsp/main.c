/****************************************Copyright (c)****************************************************
** 
**                                      
**
**--------------File Info---------------------------------------------------------------------------------
** File name:			main.c
** Last Version:		V0.1
** Descriptions:		  main 函数调用
** Author : xiao chen
** Historical Version :
** 好钜润科技，芯片事业部----深圳龙华应用分部
*********************************************************************************************************/
#include "main.h"
#include "LCD.h"
#include "SPI.h"
#include "UART.h"
#include "MM_T035.h"
/********************************************************************************************************
**函数信息 ：int main (void)                          
**功能描述 ：无
**输入参数 ：无
**输出参数 ：无
********************************************************************************************************/

int main(void)
{	
  RemapVtorTable();
	SystemClk_HSEInit(RCC_PLLMul_20);//启动PLL时钟，12MHz*20=240MHz
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);//2：2，全局性函数，仅需设置一次
	UartInit(UART1,460800);      //配置串口1，波特率为460800
	printf(" Welcome to use HJR TK499! \r\n");
				
	TIM8_Config(24000,10000);          //配置定时器8，在定时器中断里打印

	LCD_Initial();               //LCD初始化函数
	
	Lcd_ColorBox(0,0,XSIZE_PHYS,YSIZE_PHYS,Blue);//用蓝色清屏

	printf(" Welcome to use HJR TK499! \r\n");
	
	LCD_PutString(90,60,"Welcome to use HJR TK499 and RGB LCD!",Red,Yellow,1);
	LCD_Fill_Pic(80,200,320,200,(u32*)gImage_MM_T035);
	while(1)//无限循环 
	{
		
	}
}

