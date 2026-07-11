
//#ifdef  use_capacitive_touch_panel
#include "touch_CTP.h"
#include "HAL_rcc.h"
#include "LCD.h"
#include "touch_CTP.h"
#if USE_CTP	

u8 buf[10];
volatile int16_t GUI_Value_X,GUI_Value_Y;
volatile unsigned char	touchInfo_flag; 
int GUI_TOUCH_Measure(void)
{
	int i;
	I2C1->IC_DATA_CMD =CPT_addr | 0x200;//写入从机地址，开启起始信号
	I2CTXByte(I2C1,CMD_WRITE,0xD0);I2CTXByte(I2C1,CMD_WRITE,0x07);I2CTXByte(I2C1,CMD_WRITE,0x00);I2CTXByte(I2C1,CMD_WRITE,0x00);
	for(i=0;i<10;i++) 
		{
			*(buf+i)=I2CRXByte(I2C1);//库函数法读取IIC数据
		}
	if (buf[8]==32)
	{
		GUI_Value_X = buf[4] | ((buf[7]&0x0f)<<8);//x坐标
		GUI_Value_Y = buf[5] | ((buf[7]&0XF0)<<4);//y坐标

		touchInfo_flag = 1;//触摸有效
	}
	else
	{
		touchInfo_flag = 0;//触摸无效
	}
	I2CTXByte(I2C1,CMD_WRITE,0xD0);I2CTXByte(I2C1,CMD_WRITE,0);I2CTXByte(I2C1,CMD_WRITE,0x02);I2CTXByte(I2C1,CMD_WRITE,0xAB);
}


//while(1)
//	
//{
//	if ((GPIOD->IDR & GPIO_Pin_7) == 0)
//	Touch_Test();
//	
//	if ((GPIOA->IDR & GPIO_Pin_0) == 1)Lcd_ColorBox(0,0,XSIZE_PHYS,YSIZE_PHYS,Blue);//用蓝色清屏
//}
void Touch_Test(void)
{
	char i,j;
	u8 buf[32];
	char s[32];
	u16 touchX=0,touchY=0;
	
	I2C1->IC_DATA_CMD =CPT_addr | 0x200;//写入从机地址，开启起始信号
	
	I2CTXByte(I2C1,CMD_WRITE,0xD0);I2CTXByte(I2C1,CMD_WRITE,0x07);I2CTXByte(I2C1,CMD_WRITE,0x00);I2CTXByte(I2C1,CMD_WRITE,0x00);
	
//	I2CTXByte(I2C1,CMD_WRITE,CPT_addr+1);   //发送读命令	
	for(i=0;i<16;i++) 
		{
			*(buf+i)=I2CRXByte(I2C1);//库函数法读取IIC数据
		}
//		j = buf[1]&0x0f;
//		if(j>5) return ;
//		for(i=6;i<6*j;i++) 
//		{
//			*(buf+i)=I2CRXByte(I2C1);//库函数法读取IIC数据
//		}
//		for(i=0;i<j;i++)
//	{
		if(buf[8]==32)
		{
		touchX = buf[4] | ((buf[7]&0x0f)<<8);//x坐标
		touchY = buf[5] | ((buf[7]&0XF0)<<4);//y坐标

		Lcd_ColorBox(touchX,touchY,2,2,Red);
//		
//		sprintf(s,"touchX=%4d",touchX);
//		LCD_PutString(100,140,s,Red,Yellow,1);

//		sprintf(s,"touchY=%4d",touchY);
//		LCD_PutString(100,160,s,Red,Yellow,1);
		
		
//		touchX = buf[9] | (((int16_t)buf[12]&0x0f)<<8);//x坐标
//		touchY = buf[10] | (((int16_t)buf[12]&0XF0)<<4);//y坐标

//		Lcd_ColorBox(touchX,touchY,2,2,Yellow);
		
		
//		for(i=0;i<10;i++)
//		{
//			sprintf(s,"XRA=%3d",buf[i]);
//			LCD_PutString(200,180+i*16,s,Red,Yellow,1);
//		}
		
		}
		I2CTXByte(I2C1,CMD_WRITE,0xD0);I2CTXByte(I2C1,CMD_WRITE,0);I2CTXByte(I2C1,CMD_WRITE,0x02);I2CTXByte(I2C1,CMD_WRITE,0xAB);
//		if(touchX==2650)while(1);
//		if((touchX>0)&&(touchX<2048))
//		{
//		if(i>2)Lcd_ColorBox(touchX,touchY,2,2,Yellow >>(8*(i-3)));
//		 else Lcd_ColorBox(touchX,touchY,2,2,Red>>(8*i));

//		printf("touchX= %d \r\n",touchX);
//		printf("touchY= %d \r\n",touchY);
//		}
//	}

}
#endif