#ifndef FACE_DETECTOR_H
#define FACE_DETECTOR_H

#include "drv_ov2640.h"
#include <stdbool.h>

#define MAX_FACES 4

typedef struct {
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
    float   confidence;
} face_box_t;

typedef struct {
    int       count;
    face_box_t faces[MAX_FACES];
    uint32_t  inference_ms;
} face_result_t;

/* C interface (face_detector.cpp implements these with extern "C") */
#ifdef __cplusplus
extern "C" {
#endif

esp_err_t face_detector_init(void);
esp_err_t face_detector_detect(const camera_frame_t *frame, face_result_t *result);
const face_box_t *face_detector_pick_largest(const face_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* FACE_DETECTOR_H */
