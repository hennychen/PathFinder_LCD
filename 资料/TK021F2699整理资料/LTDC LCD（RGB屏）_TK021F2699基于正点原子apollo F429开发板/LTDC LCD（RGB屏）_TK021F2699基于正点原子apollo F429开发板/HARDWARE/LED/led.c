#include "led.h"
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//ALIENTEK STM32F429开发板
//LED驱动代码	   
//正点原子@ALIENTEK
//技术论坛:www.openedv.com
//创建日期:2016/1/5
//版本：V1.0
//版权所有，盗版必究。
//Copyright(C) 广州市星翼电子科技有限公司 2014-2024
//All rights reserved									  
////////////////////////////////////////////////////////////////////////////////// 	
#define LCD_SPI_CS(a)	\
						if (a)	\
						HAL_GPIO_WritePin(GPIOH,GPIO_PIN_7,GPIO_PIN_SET);	\
						else		\
						HAL_GPIO_WritePin(GPIOH,GPIO_PIN_7,GPIO_PIN_RESET);					
#define SPI_DCLK(a)	\
						if (a)	\
						HAL_GPIO_WritePin(GPIOH,GPIO_PIN_6,GPIO_PIN_SET);	\
						else		\
						HAL_GPIO_WritePin(GPIOH,GPIO_PIN_6,GPIO_PIN_RESET);	
#define SPI_SDA(a)	\
						if (a)	\
						HAL_GPIO_WritePin(GPIOI,GPIO_PIN_3,GPIO_PIN_SET);	\
						else		\
						HAL_GPIO_WritePin(GPIOI,GPIO_PIN_3,GPIO_PIN_RESET);	

#define LCD_RST(a)	\
						if (a)	\
							HAL_GPIO_WritePin(GPIOG,GPIO_PIN_3,GPIO_PIN_SET);	\
						else		\
						HAL_GPIO_WritePin(GPIOG,GPIO_PIN_3,GPIO_PIN_RESET);	
//初始化PB1为输出.并使能时钟	    
//LED IO初始化
void LED_Init(void)
{
    GPIO_InitTypeDef GPIO_Initure;
    __HAL_RCC_GPIOB_CLK_ENABLE();           //开启GPIOB时钟
		__HAL_RCC_GPIOH_CLK_ENABLE();           //开启GPIOH时钟
		__HAL_RCC_GPIOG_CLK_ENABLE();           //开启GPIOG时钟
		__HAL_RCC_GPIOI_CLK_ENABLE();           //开启GPIOI时钟
	
    GPIO_Initure.Pin=GPIO_PIN_0|GPIO_PIN_1; //PB1,0
    GPIO_Initure.Mode=GPIO_MODE_OUTPUT_PP;  //推挽输出
    GPIO_Initure.Pull=GPIO_PULLUP;          //上拉
    GPIO_Initure.Speed=GPIO_SPEED_HIGH;     //高速
    HAL_GPIO_Init(GPIOB,&GPIO_Initure);
	
		GPIO_Initure.Pin=GPIO_PIN_3 | GPIO_PIN_5; //LCD_RESET  LCD_BL
    GPIO_Initure.Mode=GPIO_MODE_OUTPUT_PP;  //推挽输出
    GPIO_Initure.Pull=GPIO_PULLUP;          //上拉
    GPIO_Initure.Speed=GPIO_SPEED_HIGH;     //高速
    HAL_GPIO_Init(GPIOG,&GPIO_Initure);
	
	
		GPIO_Initure.Pin=GPIO_PIN_3 | GPIO_PIN_8; //T_MOSI
    GPIO_Initure.Mode=GPIO_MODE_OUTPUT_PP;  //推挽输出
    GPIO_Initure.Pull=GPIO_PULLUP;          //上拉
    GPIO_Initure.Speed=GPIO_SPEED_HIGH;     //高速
    HAL_GPIO_Init(GPIOI,&GPIO_Initure);
	
		GPIO_Initure.Pin=GPIO_PIN_6 | GPIO_PIN_7; //T_SCK T_CS
    GPIO_Initure.Mode=GPIO_MODE_OUTPUT_PP;  //推挽输出
    GPIO_Initure.Pull=GPIO_PULLUP;          //上拉
    GPIO_Initure.Speed=GPIO_SPEED_HIGH;     //高速
    HAL_GPIO_Init(GPIOH,&GPIO_Initure);
	
    HAL_GPIO_WritePin(GPIOB,GPIO_PIN_0,GPIO_PIN_SET);	//PB0置1 
    HAL_GPIO_WritePin(GPIOB,GPIO_PIN_1,GPIO_PIN_SET);	//PB1置1  
}
volatile void LCD_delay(volatile int time)  //简单软件延时
{
	volatile u32 i;	
	while(time--)
	for(i=500;i>0;i--);
}

static void LCD_Reset(void)
{
	//注意，现在科学发达，有的屏不用复位也行 
		LCD_RST(0);
    LCD_delay(200);					   
    LCD_RST(1);		 	 
    LCD_delay(200);					   

}
void LCD_WriteByteSPI(unsigned char byte)
{
    unsigned char n;
   
    for(n=0; n<8; n++)			
    {  
        if(byte&0x80) SPI_SDA(1)
        else SPI_SDA(0)
        byte<<= 1;
        SPI_DCLK(0);
        SPI_DCLK(1);
    }
}
void SPI_WriteComm(u16 CMD)//3线8bit 串行接口
{			
	LCD_SPI_CS(0);
	SPI_SDA(0);
	SPI_DCLK(0);
	SPI_DCLK(1);
	LCD_WriteByteSPI(CMD);
	LCD_SPI_CS(1);
}
void SPI_WriteData(u16 tem_data)
{			
	LCD_SPI_CS(0);
	SPI_SDA(1);
	SPI_DCLK(0);
	SPI_DCLK(1);
	LCD_WriteByteSPI(tem_data);
	LCD_SPI_CS(1);
}
#define REGFLAG_DELAY         0XFE
#define REGFLAG_END_OF_TABLE  0xFD   /* END OF REGISTERS MARKER */

struct LCM_setting_table {
	u8 cmd;
	u32 count;
	u8 para_list[64];
};

static struct LCM_setting_table lcm_initialization_setting[] = {
	//{0x11, 1, {0x00} },
	//{REGFLAG_DELAY, 60, {0x00} },
	{0x3A, 1, {0x70} },
	{0xff, 5, {0x77, 0x01, 0x00, 0x00, 0x10} },
	{0xc0, 2, {0x3B, 0x00} },
	{0xc1, 2, {0x06, 0x05} },
	{0xc2, 2, {0x37, 0x02} },
	{0xc6, 1, {0x21} },
	{0xcc, 1, {0x30} },
	//{0xcd, 1, {0x08} },

	{0xb0, 16, {0xC0, 0x54, 0x5C, 0x0D, 0x51, 0x06, 0x09, 0x08, 0x07,
		   0x24, 0x03, 0x11, 0x0F, 0xAC, 0xB5, 0x7F} },

	{0xb1, 16, {0xC0,0x54 ,0x5C,0x0E ,0x11 ,0x07 ,0x0A ,0x09 ,0x08 ,0x24 ,0x04 ,0x51 ,0x10 ,0xAD ,0x75 ,0x7F} },

	{0xff, 5, {0x77, 0x01, 0x00, 0x00, 0x11} },

	{0xb0, 1, {0x7D} },
	{0xb1, 1, {0x3B} },
	{0xb2, 1, {0x07} },
	{0xb3, 1, {0x80} },
	{0xb5, 1, {0x45} },
	{0xb7, 1, {0x87} },
	{0xb8, 1, {0x33} },
	{0xb9, 1, {0x10} },
	{0xbb, 1, {0x03} },
	{0xc0, 1, {0x03} },
	{0xc1, 1, {0x70} },
	{0xc2, 1, {0x70} },
	{0xd0, 1, {0x88} },
	{REGFLAG_DELAY, 50, {0x00} },

	
	{0xe0, 5, {0x00,0x18,0x00,0x00,0x00,0x20} },
	{0xe1, 11, {0x02,0x00,0x04,0x00,0x01,0x00,0x03,0x00,0x00,0x22,0x22} },
	{0xe2, 13, {0x10,0x10,0x20,0x20,0xE7,0x00,0x00,0x00,0xE6,0x00,0x00,0x00,0x00} },
	{0xe3, 4, {0x00, 0x00, 0x11, 0x11} },
	{0xe4, 2, {0x44, 0x44} },
	{0xe5, 16, {0x03,0xE0,0x00,0xF5,0x05,0xE2,0x00,0xF5,0x07,0xE4,0x00,0xF5,0x09,0xE6,0x00,0xF5} },

	{0xe6, 4, {0x00, 0x00, 0x11, 0x11} },
	{0xe7, 2, {0x44, 0x44} },
	{0xe8, 16, {0x02,0xDF,0x00,0xF5,0x04,0xE1,0x00,0xF5,0x06,0xE3,0x00,0xF5,0x08,0xE5,0x00,0xF5} },
//	{0xEA, 16, {0x10, 0x00, 0x10, 0x00, 0x10, 0x00, 0x10, 0x00, 0x10, 0x00, 
//			0x10, 0x00, 0x10, 0x00, 0x10, 0x00} },
	{0xeb, 7, {0x00,0x02,0xE4,0xE4,0x88,0x00,0x10} },

	{0xec, 3, {0x3D, 0x02, 0x02} },
	{0xed, 16, {0x20,0x76,0x54,0x98,0xBA,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xAB,0x89,0x45,0x67,0x02} },
	{0xeF, 6, {0x00,0x00,0x04,0x00,0x3F,0x1F} },
	{0xff, 5, {0x77, 0x01, 0x00, 0x00, 0x13} },
	{0xE8, 2, {0x00, 0x0E} },
	{0xE8, 2, {0x00, 0x0C} },
	{0xE8, 2, {0x00, 0x00} },
	
	
	{0xff, 5, {0x77, 0x01, 0x00, 0x00, 0x00} },
	{0x11, 1, {0x00} },
	{REGFLAG_DELAY, 50, {0x00} },
	{0x20, 1, {0x00} },
	{0x36, 1, {0x00} },
	{0x29, 1, {0x00} },
	{REGFLAG_END_OF_TABLE, 0x00, {0x00} }
};


void Lcd_Initialize(void)
{
	u32 i = 0,j=0;
 LCD_SPI_CS(1);
    LCD_delay(20);
    LCD_SPI_CS(0);
    LCD_Reset();
	for (i = 0;; i++) {
		if (lcm_initialization_setting[i].cmd == REGFLAG_END_OF_TABLE)
			break;
		else if (lcm_initialization_setting[i].cmd == REGFLAG_DELAY)
			LCD_delay(lcm_initialization_setting[i].count);
		else {

			SPI_WriteComm(lcm_initialization_setting[i].cmd);
			for(j=0;j<lcm_initialization_setting[i].count;j++)
			SPI_WriteData(lcm_initialization_setting[i].para_list[j]);
		}
	}
}
//void Lcd_Initialize(void)	//LCD初始化函数
//{
//    LCD_SPI_CS(1);
//    LCD_delay(20);
//    LCD_SPI_CS(0);
//    LCD_Reset();

//		SPI_WriteComm(0x20);//exit_invert_mode
//		SPI_WriteComm(0x29);//set_display_on

//		SPI_WriteComm(0xB1);//RGB Interface Setting
//		SPI_WriteData(0x00);
//		SPI_WriteData(0x14);
//		SPI_WriteData(0x06);

//		SPI_WriteComm(0xB2);//Panel Characteristics Setting
//		SPI_WriteData(0x10);//480 pixels
//		SPI_WriteData(0xC8);//800 pixels

//		SPI_WriteComm(0xB3);//Panel Drive Setting    Set the inversion mode


//		SPI_WriteData(0x00);//1-dot inversion 0x01

//		SPI_WriteComm(0xB4);//Display Mode Control
//		SPI_WriteData(0x04);//Dither disable.

//		SPI_WriteComm(0xB5);//Display Mode and Frame Memory Write Mode Setting
//		SPI_WriteData(0x10);
//		SPI_WriteData(0x30);
//		SPI_WriteData(0x30);
//		SPI_WriteData(0x00);
//		SPI_WriteData(0x00);

//		SPI_WriteComm(0xB6);//Display Control 2 ( GIP Specific )
//		SPI_WriteData(0x01);
//		SPI_WriteData(0x18);
//		SPI_WriteData(0x02);
//		SPI_WriteData(0x40);
//		SPI_WriteData(0x10);
//		SPI_WriteData(0x00);

//		SPI_WriteComm(0xc0);
//		SPI_WriteData(0x01);
//		SPI_WriteData(0x18);


//		SPI_WriteComm(0xC3); 
//		SPI_WriteData(0x03);
//		SPI_WriteData(0x04);
//		SPI_WriteData(0x03);
//		SPI_WriteData(0x03);
//		SPI_WriteData(0x03);

//		LCD_delay(10);

//		SPI_WriteComm(0xC4);//VDD Regulator setting
//		SPI_WriteData(0x02);
//		SPI_WriteData(0x23);//GDC AP
//		SPI_WriteData(0x11);//VRH1  Vreg1out=1.533xVCI(10)
//		SPI_WriteData(0x12);//VRH2  Vreg2out=-1.533xVCI(10)
//		SPI_WriteData(0x02);//BT 5 VGH/VGL  6/-4
//		SPI_WriteData(0x77);//DDVDH 6C//0x56
//		LCD_delay(10);

//		SPI_WriteComm(0xC5);
//		SPI_WriteData(0x73);
//		LCD_delay(10);

//		SPI_WriteComm(0xC6);
//		SPI_WriteData(0x24);//VCI 23
//		SPI_WriteData(0x60);//RESET RCO 53
//		SPI_WriteData(0x00);//SBC GBC
//		LCD_delay(10);
//		//GAMMA SETTING
//		SPI_WriteComm(0xD0);
//		SPI_WriteData(0x14);
//		SPI_WriteData(0x01);
//		SPI_WriteData(0x53);
//		SPI_WriteData(0x25);
//		SPI_WriteData(0x02);
//		SPI_WriteData(0x02);
//		SPI_WriteData(0x66);
//		SPI_WriteData(0x14);
//		SPI_WriteData(0x03);

//		SPI_WriteComm(0xD1);
//		SPI_WriteData(0x14);
//		SPI_WriteData(0x01);
//		SPI_WriteData(0x53);
//		SPI_WriteData(0x07);
//		SPI_WriteData(0x02);
//		SPI_WriteData(0x02);
//		SPI_WriteData(0x66);
//		SPI_WriteData(0x14);
//		SPI_WriteData(0x03);



//		SPI_WriteComm(0xD2);
//		SPI_WriteData(0x14);
//		SPI_WriteData(0x01);
//		SPI_WriteData(0x53);
//		SPI_WriteData(0x25);
//		SPI_WriteData(0x02);
//		SPI_WriteData(0x02);
//		SPI_WriteData(0x66);
//		SPI_WriteData(0x14);
//		SPI_WriteData(0x03);

//		SPI_WriteComm(0xD3);
//		SPI_WriteData(0x14);
//		SPI_WriteData(0x01);
//		SPI_WriteData(0x53);
//		SPI_WriteData(0x07);
//		SPI_WriteData(0x02);
//		SPI_WriteData(0x02);
//		SPI_WriteData(0x66);
//		SPI_WriteData(0x14);
//		SPI_WriteData(0x03);


//		SPI_WriteComm(0xD4);
//		SPI_WriteData(0x14);
//		SPI_WriteData(0x01);
//		SPI_WriteData(0x53);
//		SPI_WriteData(0x25);
//		SPI_WriteData(0x02);
//		SPI_WriteData(0x02);
//		SPI_WriteData(0x66);
//		SPI_WriteData(0x14);
//		SPI_WriteData(0x03);

//		SPI_WriteComm(0xD5);
//		SPI_WriteData(0x14);
//		SPI_WriteData(0x01);
//		SPI_WriteData(0x53);
//		SPI_WriteData(0x07);
//		SPI_WriteData(0x02);
//		SPI_WriteData(0x02);
//		SPI_WriteData(0x66);
//		SPI_WriteData(0x14);
//		SPI_WriteData(0x03);
//		SPI_WriteComm(0x11);

//		LCD_delay(10);

//		SPI_WriteComm(0x3A); SPI_WriteData(0x66);//set_pixel_format 0X60 26k
//		SPI_WriteComm(0x29);SPI_WriteComm(0x2c);
//		
//		SPI_WriteComm(0x36);
//#if LCD_RGB_ORIENTATION //是否旋转90度
// SPI_WriteData(0x02);
//#else
// SPI_WriteData(0x00);
//#endif
//}