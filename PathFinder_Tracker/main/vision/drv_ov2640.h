#ifndef DRV_OV2640_H
#define DRV_OV2640_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define FACE_IMG_WIDTH   320
#define FACE_IMG_HEIGHT  240

typedef struct {
    uint8_t  *data;
    uint32_t  width;
    uint32_t  height;
    uint32_t  format;
} camera_frame_t;

esp_err_t drv_ov2640_init(void);
esp_err_t drv_ov2640_capture(camera_frame_t *frame);
void drv_ov2640_return_frame(camera_frame_t *frame);
esp_err_t drv_ov2640_deinit(void);

#endif /* DRV_OV2640_H */
