#ifndef DRV_ES7210_H
#define DRV_ES7210_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define ES7210_CHANNELS 4
#define ES7210_SAMPLE_SIZE 256

esp_err_t drv_es7210_init(void);
esp_err_t drv_es7210_read(float data_out[ES7210_CHANNELS][ES7210_SAMPLE_SIZE]);
int drv_es7210_get_i2s_port(void);

#endif /* DRV_ES7210_H */
