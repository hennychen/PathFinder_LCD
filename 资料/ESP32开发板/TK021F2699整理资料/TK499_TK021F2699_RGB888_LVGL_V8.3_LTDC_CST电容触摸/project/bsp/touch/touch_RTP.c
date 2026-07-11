#include "Tk499.h"
#include "sys.h"
#include "stdio.h"
#include "touch_RTP.h"

#if USE_RTP

volatile int ADC_Value_X,ADC_Value_Y,ADC_Value_t,GUI_Value_X,GUI_Value_Y;
static volatile u16 Temp_Value_X[20],Temp_Value_Y[20],flag_ADC=0;
volatile unsigned char	touchInfo_flag;  //GUI用，有触摸按下

void TOUCHPAD_IRQHandler()
{
	if(TOUCHPAD->TPCR & (0x1<<16))
	{
//		TOUCHPAD->TPCR &= (~0x2);// 禁用A/D触摸屏中断
//		TOUCHPAD->TPCR |= 0x1<<16;// 比较标志位,写1清零
		Temp_Value_X[flag_ADC] = TOUCHPAD->TPYDR;
		Temp_Value_Y[flag_ADC] = TOUCHPAD->TPXDR;
		if(flag_ADC==19) 
		{
			u16 k,cou,temp;
			flag_ADC = 0;
			//将数据升序排列
			for(k=0;k<18;k++)
			{	  
				for(cou=0;cou<18-k;cou++)
				{
					if(Temp_Value_Y[cou]>Temp_Value_Y[cou+1])
					{
						temp=Temp_Value_Y[cou+1];
						Temp_Value_Y[cou+1]=Temp_Value_Y[cou];
						Temp_Value_Y[cou]=temp;
					}  
				}
			}
			if((Temp_Value_Y[16]>TOUCH_Y_MAX)||(Temp_Value_Y[3]<TOUCH_Y_MIN)) {ADC_Value_Y=8000;goto restart_ADC;}
			if(Temp_Value_Y[16]-Temp_Value_Y[3]>200) goto restart_ADC; 
			for(k=0;k<18;k++)
			{	  
				for(cou=0;cou<18-k;cou++)
				{
					if(Temp_Value_X[cou]>Temp_Value_X[cou+1])
					{
						temp=Temp_Value_X[cou+1];
						Temp_Value_X[cou+1]=Temp_Value_X[cou];
						Temp_Value_X[cou]=temp;
					}  
				}
			}
			if((Temp_Value_X[16]>TOUCH_X_MAX)||(Temp_Value_X[3]<TOUCH_X_MIN)) {ADC_Value_X=8000;goto restart_ADC;}
			if(Temp_Value_X[16]-Temp_Value_X[3]>200) goto restart_ADC; 
			ADC_Value_X=0;
			ADC_Value_Y=0;
			for(k=3;k<17;k++)
			{
				ADC_Value_X+=Temp_Value_X[k];
				ADC_Value_Y+=Temp_Value_Y[k];
			}
			
			ADC_Value_X = ADC_Value_X/14;
			ADC_Value_Y = ADC_Value_Y/14;
			
//			if((ADC_Value_Y>600)&&(ADC_Value_Y<3750))
//			{
//				if((ADC_Value_X>450)&&(ADC_Value_X<3900))
//				{
					GUI_Value_X=(3900-ADC_Value_X)*854/(3900-450);
					GUI_Value_Y=(ADC_Value_Y-600)*480/(3750-600);
					
					
					touchInfo_flag = 1;  //GUI用，XY数据都有效，有触摸按下
//				}
//				else touchInfo_flag = 0;
//			}
			
//			if(ADC_Value_X>2100) ADC_Value_X = ADC_Value_X-200*(ADC_Value_X-2100)/((3700-500)/2);
//			if(ADC_Value_X<2100) ADC_Value_X = ADC_Value_X-(50*(2100-ADC_Value_X)/((3700-500)/2));
//			
//			if(ADC_Value_Y>2100) ADC_Value_Y = ADC_Value_Y+100*(ADC_Value_Y-2100)/((3900-280)/2);
//			if(ADC_Value_Y<2100)  ADC_Value_Y = ADC_Value_Y-(100*(2100-ADC_Value_Y)/((3900-280)/2));

			
			restart_ADC:
			flag_ADC = 0;
			 
		}
		else flag_ADC++;
		TOUCHPAD->TPCR |= 0x2;//重新使能A/D触摸屏中断	
	}

}
#endif

void touchpad_Config(void)
{
	TK499_NVIC_Init(3,3,TOUCHPAD_IRQn,3);//抢占 1，子优先级 3，组 2	
	
	RCC->APB2ENR |= 0x1<<8;
	RCC->APB2ENR |= 0x1<<25;
	
	GPIOB->CRL &= 0xffff0000;                
	GPIOB->CRL |= 0xbbbb;	    
	GPIOB->AFRL &= ~0xffff; 
	GPIOB->AFRL |= 0xdddd;
	 
	
	TOUCHPAD->ADCFG =0x3E1E70;      //16分频   AD比较器禁用
	TOUCHPAD->ADCR = 0x200;      //0x200单周期模式;0x400连续扫描模式
	TOUCHPAD->ADCHS = 0xc;      //通道使能 6,7,8,9
//	TOUCHPAD->TPCSR = 0x10;   //X\Y通道选择 1，0
	TOUCHPAD->TPFR = 0x0ffffff;    //采样1次 有效阈值为0xfff

	TOUCHPAD->TPCR = 0x1;      //使能触摸屏模式
	TOUCHPAD->TPCR |= 0x2;       //触摸屏中断使能
	
//	TOUCHPAD->ADCR |=0x1;        //使能ADC中断
	
	TOUCHPAD->ADCFG |= 0x1;      //ADC使能
	TOUCHPAD->ADCR |= 0x1<<8;  //A/D转换开始 (ADC start)
	
}
