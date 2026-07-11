#include <stdio.h>
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

#define HIGH 1
#define LOW 0



#define LCD_SPI_CS(a)	\
						if (a)	\
						gpio_set_level(1, HIGH);	\
						else		\
						gpio_set_level(1, LOW);					
#define SPI_DCLK(a)	\
						if (a)	\
						gpio_set_level(13, HIGH);	\
						else		\
						gpio_set_level(13, LOW);		
#define SPI_SDA(a)	\
						if (a)	\
						gpio_set_level(20, HIGH);	\
						else		\
						gpio_set_level(20, LOW);	

void configure_LCD_GPIO(void);
void Lcd_Initialize(void);
