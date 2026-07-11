#include "tk499.h" 
#include "LCD.h"
#include "ASCII.h"
#include "GB1616.h"	//16*16汉字字模
#include "GB4848.h"	//48*48汉字字模

__align(256) u32 LTDC_Buf[XSIZE_PHYS*YSIZE_PHYS];

void LCD_delay(volatile int time)  //简单软件延时
{
	volatile u32 i;	
	while(time--)
	for(i=500;i>0;i--);
}

void GPIO_RGB_INIT(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;//定义GPIO初始化结构体变量

	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB | RCC_AHBPeriph_GPIOD | RCC_AHBPeriph_GPIOE, ENABLE);
	
	//DE=PB4, PCLK=PB5, HSYNC=PB6, VSYNC=PB7
	GPIO_InitStructure.GPIO_Pin  =  GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_10MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_Init(GPIOB, &GPIO_InitStructure);
	
	//                               LCD_SPI_CS
	GPIO_InitStructure.GPIO_Pin  =   GPIO_Pin_11;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_Init(GPIOB, &GPIO_InitStructure);
	
	//                               SPI_DCLK      SPI_SDA         
	GPIO_InitStructure.GPIO_Pin  =  GPIO_Pin_2 | GPIO_Pin_0;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_Init(GPIOB, &GPIO_InitStructure);
	
	//            LCD_Black_Light On Off: PD8
	GPIO_InitStructure.GPIO_Pin  =   GPIO_Pin_8;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_Init(GPIOD, &GPIO_InitStructure);
	


	
 

  GPIO_InitStructure.GPIO_Pin  =  GPIO_Pin_All;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_10MHz;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
  GPIO_Init(GPIOE, &GPIO_InitStructure);
	
	GPIO_PinAFConfig(GPIOB, GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7, GPIO_AF_LTDC); //PB4~7复用为LTDC的同步信号线
	GPIO_PinAFConfig(GPIOE, GPIO_Pin_All, GPIO_AF_LTDC); //GPIOE所有的IO全部复用为LTDC的数据线
	
	//                               按键 
	GPIO_InitStructure.GPIO_Pin  =   GPIO_Pin_0;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
}

void LCD_WriteByteSPI(unsigned char byte)
{
    unsigned char n;
   
    for(n=0; n<8; n++)			
    {  
        if(byte&0x80) SPI_SDA(1)
        else SPI_SDA(0)
        
        SPI_DCLK(0);
				byte<<= 1;
        SPI_DCLK(1);
    }
}

void SPI_WriteComm(u16 CMD)//3线9bit 串行接口
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
void LTDC_Clock_Set(void)	//设置LTDC时钟
{
	RCC->AHB1ENR |= 1<<31;
	RCC->CR |= 1<<28;
	RCC->PLLDCKCFGR = 0x1<<16;  //分频系数 0~3 --> 2,4,6,8
	RCC->PLLLCDCFGR = 6<<6;   	//倍频系数
}
void set_resolutionXX(LCD_FORM_TypeDef* LCD_FORM)
{
    u32 aHorStart;
    u32 aHorEnd;
    u32 aVerStart;
    u32 aVerEnd;

    aHorStart = LCD_FORM->blkHorEnd + 1;
    aHorEnd = aHorStart + LCD_FORM->aHorLen;  
    aVerStart = LCD_FORM->blkVerEnd + 1 ;
    aVerEnd = aVerStart + LCD_FORM->aVerLen;

		LTDC->P_HOR = aHorEnd;//总宽度
    LTDC->HSYNC = (LCD_FORM->sHsyncStart <<16 )|LCD_FORM->sHsyncEnd;//水平同步信号起始和结束，位于背景色中间
    LTDC->A_HOR = (aHorStart<<16)|aHorEnd;//水平激活起始和结束
    LTDC->A_HOR_LEN = LCD_FORM->aHorLen ;//水平激活域宽度
    LTDC->BLK_HOR = (0<<16)|LCD_FORM->blkHorEnd;//背景开始和结束宽度0~激活地址	
    LTDC->P_VER =  aVerEnd;
    LTDC->VSYNC = (LCD_FORM->sVsyncStart<<16)|LCD_FORM->sVsyncEnd;
    LTDC->A_VER = (aVerStart<<16)|aVerEnd;
    LTDC->A_VER_LEN = LCD_FORM->aVerLen ;
		LTDC->BLK_VER = (0<<16)|LCD_FORM->blkVerEnd;
}
void Set_LCD_Timing_to_LTDC(void)
{
    LCD_FORM_TypeDef LCD_FORM;
    LTDC->OUT_EN = 0;
    LTDC->DP_ADDR0 = (u32)LTDC_Buf;//第0层地址
//    LTDC->DP_ADDR1 = (u32)(LTDC_Buf + SDRAM_RGB_OFFSET);//第一层地址
    LTDC->BLK_DATA = 0x0000;//背景色

		LCD_FORM.sHsyncStart = LCD_HSPW;  //水平激活起始
    LCD_FORM.sHsyncEnd = LCD_HFPD-LCD_HSPW+1;    //水平激活结束
    LCD_FORM.aHorLen = XSIZE_PHYS - 1;  //水平分辨率
    LCD_FORM.blkHorEnd = LCD_HFPD;    //水平消隐

    LCD_FORM.sVsyncStart = LCD_VSPW;  //垂直激活起始
    LCD_FORM.sVsyncEnd = LCD_VFPD-LCD_VSPW+1;    //垂直激活结束
    LCD_FORM.aVerLen= YSIZE_PHYS - 1; 	 //垂直分辨率
    LCD_FORM.blkVerEnd = LCD_VFPD;   //垂直消隐
	
    set_resolutionXX(&LCD_FORM);

		LTDC->VI_FORMAT = 0;
		LTDC->POL_CTL = 0x8+3;
		LTDC->OUT_EN |= 0x107;
}
void LTDC_IRQHandler(void)
{
    LTDC->INTR_CLR = 2;
    LTDC->DP_SWT ^= 1;//连续显示两幅图片
    if(LTDC->DP_SWT !=0 )
    {
//        fun_test(SDRAM_RGB_ADD);
    }
    else
    {
//        fun_test(SDRAM_RGB_ADD+SDRAM_RGB_OFFSET);
    }
//    if(LTDC->INTR_STA & 2)
//    {

//    }
}
/**********************************************
函数名：Lcd矩形填充函数

入口参数：xStart x方向的起始点
          ySrart y方向的终止点
          xLong 要选定矩形的x方向长度
          yLong  要选定矩形的y方向长度
返回值：无
***********************************************/
void Lcd_ColorBox(u16 xStart,u16 yStart,u16 xLong,u16 yLong,u32 Color)
{
#if LCD_RGB_ORIENTATION //是否旋转90度
  u16 i,j;
	u32 temp;
	temp = YSIZE_PHYS*xStart;
	for(i=0;i<yLong;i++)
	{
		for(j=0;j<xLong;j++)
		LTDC_Buf[yStart+i+YSIZE_PHYS*j+temp]=Color;
	}
#else
  u16 i,j;
	u32 temp;
	temp = XSIZE_PHYS*yStart;
	for(i=0;i<yLong;i++)
	{
		for(j=0;j<xLong;j++)
		LTDC_Buf[xStart+j+XSIZE_PHYS*i+temp]=Color;
	}
#endif	
}
void LCD_Initial(void) 
{    
	GPIO_RGB_INIT();//初始化液晶屏相关GPIO
	LTDC_Clock_Set();
	Set_LCD_Timing_to_LTDC();
	Lcd_Initialize();
	Lcd_Light_ON;//打开背光
}
/******************************************
函数名：Lcd图像填充100*100
功能：向Lcd指定位置填充图像
入口参数：
******************************************/
void LCD_Fill_Pic(u16 x, u16 y,u16 pic_H, u16 pic_V, u32* pic)
{
#if LCD_RGB_ORIENTATION //是否旋转90度
  u16 i,j;
	u32 Xstart,k=0;
	Xstart = YSIZE_PHYS*x;
	for(i=pic_V;i>0;i--)
	{
		for(j=0;j<pic_H;j++)
		LTDC_Buf[Xstart+i+YSIZE_PHYS*j+y]=pic[k++];
	}
#else
  u16 i,j;
	u32 Ystart,k=0;
	Ystart = XSIZE_PHYS*y;
	for(i=0;i<pic_V;i++)
	{
		for(j=0;j<pic_H;j++)
		LTDC_Buf[x+j+XSIZE_PHYS*i+Ystart]=pic[k++];
	}
#endif	
}
/*半透明算法:   混合算法目前在常用到的算法是AlphaBlend。   
计算公式如下:假设一幅图象是A，另一幅透明的图象是B，那么透过B去看A，看上去的图象C就是B和A的混合图象，   
设B图象的透明度为alpha(取值为0-1，1为完全透明，0为完全不透明).    
Alpha混合公式如下：       R(C)=(1-alpha)*R(B) + alpha*R(A)       G(C)=(1-alpha)*G(B) + alpha*G(A)       B(C)=(1-alpha)*B(B) + alpha*B(A)       R(x)、G(x)、B(x)分别指颜色x的RGB分量原色值。
从上面的公式可以知道，Alpha其实是一个决定混合透明度的数值。  
改变这个 alpha 值可以得到一个渐变的效果。
*/
void LCD_alpha_Pic(u16 x, u16 y,u16 pic_H, u16 pic_V, int Color,u8  alpha)
{
	u16 i,j;
	u32 Ystart,k=0;
	
	unsigned char Ra,Ga,Ba, Rb,Gb,Bb, Rc,Gc,Bc;
	Rb=Color>>16;Gb=Color>>8;Bb=Color;
	Ystart = XSIZE_PHYS*y;
	for(i=0;i<pic_V;i++)
	{
		for(j=0;j<pic_H;j++)
		{
			Ra=LTDC_Buf[x+j+XSIZE_PHYS*i+Ystart]>>16;
			Ga=LTDC_Buf[x+j+XSIZE_PHYS*i+Ystart]>>8;
			Ba=LTDC_Buf[x+j+XSIZE_PHYS*i+Ystart];
			
			
			Rc=((255-alpha)*Ra+alpha*Rb)>>8;
			Gc=((255-alpha)*Ga+alpha*Gb)>>8;
			Bc=((255-alpha)*Ba+alpha*Bb)>>8;
			
//			Rc=(1-alpha)*Ra+alpha*Rb;
//			Gc=(1-alpha)*Ga+alpha*Gb;
//			Bc=(1-alpha)*Ba+alpha*Bb;
			
			
			LTDC_Buf[x+j+XSIZE_PHYS*i+Ystart]=(Rc<<16)|(Gc<<8)|Bc;
		}
	}
}

//=============== 在x，y 坐标上打一个颜色为Color的点 ===============
void DrawPixel(u16 x, u16 y, int Color)
{
#if LCD_RGB_ORIENTATION //是否旋转90度
 LTDC_Buf[y+YSIZE_PHYS*x] = Color;
#else
 LTDC_Buf[x+XSIZE_PHYS*y] = Color;
#endif					  
}
//------------------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------------------
//8*16字体 ASCII码 显示
//函数名：SPILCD_ShowChar
//参  数：
//(x,y): 
//num:要显示的字符:" "--->"~"
//flag:有背景色(1)无背景色(0)
void SPILCD_ShowChar(unsigned short x,unsigned short y,unsigned char num, unsigned int fColor, unsigned int bColor,unsigned char flag) 
{       
	unsigned char temp;
	unsigned int pos,i,j;  

	num=num-' ';//得到偏移后的值
	i=num*16; 	
	#if LCD_RGB_ORIENTATION //是否旋转90度
		y = YSIZE_PHYS-y;
	#endif
	for(pos=0;pos<16;pos++)
		{
			temp=nAsciiDot[i+pos];	//调通调用ASCII字体
			for(j=0;j<8;j++)
		   {                 
		        if(temp&0x80)
							DrawPixel(x+j,y,fColor);
						else 
							if(flag) DrawPixel(x+j,y,bColor); //如果背景色标志flag为1
							temp<<=1; 
		    }
			  #if LCD_RGB_ORIENTATION //是否旋转90度
				 y--;
				#else
				 y++;
				#endif	
			 
		}		 
}  

//写一个16x16的汉字
void PutGB1616(unsigned short x, unsigned short  y, unsigned char c[2], unsigned int fColor,unsigned int bColor,unsigned char flag)
{
	unsigned int i,j,k;
	unsigned short m;
	for (k=0;k<7050;k++) { //自建汉字库：GB2312汉字6763个，带符号共7050个，循环查询内码
	  if ((codeGB_16[k].Index[0]==c[0])&&(codeGB_16[k].Index[1]==c[1]))
			{ 
    	for(i=0;i<32;i++) 
			{
				m=codeGB_16[k].Msk[i];
				for(j=0;j<8;j++) 
				{		
					if((m&0x80)==0x80) {
						DrawPixel(x+j,y,fColor);
						}
					else {
						if(flag) DrawPixel(x+j,y,bColor);
						}
					m=m<<1;
				} 
				if(i%2){y++;x=x-8;}
				else x=x+8;
		  }
		}  
	  }	
	}
//显示一串字
void LCD_PutString(unsigned short x, unsigned short y, char *s, unsigned int fColor, unsigned int bColor,unsigned char flag) 
	{
		unsigned char l=0;
		while(*s) 
			{
				if( (unsigned char)*s < 0x80) 
						{
							SPILCD_ShowChar(x+l*8,y,*s,fColor,bColor,flag);
							s++;l++;
						}
				else
						{
							PutGB1616(x+l*8,y,(unsigned char*)s,fColor,bColor,flag);
							s+=2;l+=2;
						}
			}
	}
//写一个n*n的汉字
void Put_GB_n(unsigned short x, unsigned short y, unsigned char c[2], unsigned int fColor,unsigned int bColor,unsigned char flag,unsigned int n)
{
	unsigned int i,j,k,a=0;
	unsigned short m;
	#if LCD_RGB_ORIENTATION //是否旋转90度
	y = YSIZE_PHYS-y;
	#endif	
	for (k=0;k<12;k++) { //12标示自建汉字库中的个数，循环查询内码
	  if ((codeGB_48[k].Index[0]==c[0])&&(codeGB_48[k].Index[1]==c[1]))
			{ 
    	for(i=0;i<((n*n)>>3);i++) 
			{
				for(a=0;a<(n>>3);a++)
				{
					m=codeGB_48[k].Msk[i+a];
					for(j=0;j<8;j++) 
					{		
						if((m&0x80)==0x80) {
							DrawPixel(x+j,y,fColor);
							}
						else {
							if(flag) DrawPixel(x+j,y,bColor);
							}
						m=m<<1;
					} 
					 x=x+8;
				}
				i = i+a-1;
				#if LCD_RGB_ORIENTATION //是否旋转90度
				 y--;
				#else
				 y++;
				#endif	
				x=x-8*a;
		  }
		}  
	  }	
	}
//显示一串汉字，字号为n
void LCD_PutString_GB_n(unsigned short x, unsigned short y, char *s, unsigned int fColor, unsigned int bColor,unsigned char flag,unsigned int n) 
	{
		unsigned int l=0;
		
		while(*s) 
			{
				Put_GB_n(x+l,y,(unsigned char*)s,fColor,bColor,flag,n);
				s+=2;l+=n;;
			}
	}