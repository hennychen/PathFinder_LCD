/**
 * @file face_detector.cpp
 * @brief ESP-DL face detection wrapper (extern "C" interface).
 *
 * Wraps the ESP-DL HumanFaceDetect (MSRMNP_S8_V1 two-stage model) so the
 * rest of the C codebase can call it without touching C++.
 */

#include "face_detector.h"

#include "human_face_detect.hpp"     /* HumanFaceDetect           */
#include "dl_image_define.hpp"       /* dl::image::img_t, pix_type */
#include "dl_detect_define.hpp"      /* dl::detect::result_t       */

#include "esp_log.h"
#include "esp_timer.h"

#include <list>

static const char *TAG = "face_detector";

/* ------------------------------------------------------------------ */
/*  Singleton detector instance                                       */
/* ------------------------------------------------------------------ */

static HumanFaceDetect *s_detector = nullptr;

/* ------------------------------------------------------------------ */
/*  C-callable API                                                    */
/* ------------------------------------------------------------------ */

extern "C" {

esp_err_t face_detector_init(void)
{
    if (s_detector != nullptr) {
        ESP_LOGW(TAG, "face detector already initialised");
        return ESP_OK;
    }

    /*
     * MSRMNP_S8_V1 = two-stage (MSR candidate + MNP refinement).
     * This is the lighter-weight model recommended for ESP32-S3.
     * lazy_load = false → model is loaded eagerly during init so the
     * first face_detector_detect() call is not penalised.
     */
    s_detector = new (std::nothrow) HumanFaceDetect(HumanFaceDetect::MSRMNP_S8_V1, false);
    if (s_detector == nullptr) {
        ESP_LOGE(TAG, "failed to allocate HumanFaceDetect");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "ESP-DL HumanFaceDetect (MSRMNP_S8_V1) initialised");
    return ESP_OK;
}

esp_err_t face_detector_detect(const camera_frame_t *frame, face_result_t *result)
{
    if (s_detector == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (frame == NULL || frame->data == NULL || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * Build an ESP-DL image descriptor that wraps the camera's RGB565
     * frame buffer in-place (zero-copy).  The OV2640 with PIXFORMAT_RGB565
     * produces little-endian RGB565 pixels, matching DL_IMAGE_PIX_TYPE_RGB565LE.
     */
    dl::image::img_t img;
    img.data     = frame->data;
    img.width    = static_cast<uint16_t>(frame->width);
    img.height   = static_cast<uint16_t>(frame->height);
    img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE;

    /* Diagnostic: check frame data is not all zeros */
    uint16_t *pix = (uint16_t *)frame->data;
    uint32_t non_zero = 0;
    uint32_t sample_count = (frame->width * frame->height > 256) ? 256 : (frame->width * frame->height);
    for (uint32_t i = 0; i < sample_count; i++) {
        if (pix[i] != 0) non_zero++;
    }

    /* Run inference and measure wall-clock time. */
    int64_t t0 = esp_timer_get_time();
    std::list<dl::detect::result_t> &detect_results = s_detector->run(img);
    int64_t t1 = esp_timer_get_time();
    int64_t inf_us = t1 - t0;

    result->inference_ms = static_cast<uint32_t>(inf_us / 1000);
    result->count        = 0;

    ESP_LOGI(TAG, "frame %dx%d fmt=%d nonzero=%u/%u inf=%lldus results=%zu",
             frame->width, frame->height, frame->format,
             non_zero, sample_count, inf_us, detect_results.size());

    /* Convert ESP-DL results → flat face_box_t array (capped at MAX_FACES). */
    for (const auto &res : detect_results) {
        if (result->count >= MAX_FACES) {
            break;
        }
        face_box_t *fb = &result->faces[result->count];
        /* ESP-DL box is [x1, y1, x2, y2] (top-left → bottom-right). */
        fb->x          = static_cast<int16_t>(res.box[0]);
        fb->y          = static_cast<int16_t>(res.box[1]);
        fb->width      = static_cast<uint16_t>(res.box[2] - res.box[0]);
        fb->height     = static_cast<uint16_t>(res.box[3] - res.box[1]);
        fb->confidence = res.score;
        result->count++;

        ESP_LOGD(TAG, "face[%d] score=%.3f  box=[%d,%d,%d,%d]",
                 result->count - 1, res.score,
                 res.box[0], res.box[1], res.box[2], res.box[3]);
    }

    return ESP_OK;
}

const face_box_t *face_detector_pick_largest(const face_result_t *result)
{
    if (result == NULL || result->count == 0) {
        return NULL;
    }

    const face_box_t *largest = &result->faces[0];
    uint32_t max_area = static_cast<uint32_t>(largest->width) * largest->height;

    for (int i = 1; i < result->count; i++) {
        uint32_t area = static_cast<uint32_t>(result->faces[i].width) *
                        result->faces[i].height;
        if (area > max_area) {
            max_area = area;
            largest  = &result->faces[i];
        }
    }

    return largest;
}

} /* extern "C" */
