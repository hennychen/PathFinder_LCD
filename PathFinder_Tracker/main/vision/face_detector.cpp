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
#include <cmath>

static const char *TAG = "face_detector";

static uint32_t s_diag_counter = 0;  /* Log diagnostics every N frames */

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

    /* Lower score threshold: default 0.5 is too high for low-res OV2640.
       idx=0 → MSR stage, idx=1 → MNP stage */
    s_detector->set_score_thr(0.1f, 0);  /* MSR: 0.5 → 0.1 */
    s_detector->set_score_thr(0.3f, 1);  /* MNP: 0.5 → 0.3 */

    ESP_LOGI(TAG, "ESP-DL HumanFaceDetect (MSRMNP_S8_V1) init, score_thr lowered to 0.1/0.3");
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

    bool do_diag = (s_diag_counter++ % 30 == 0);  /* every 30th frame */

    /* Diagnostic: pixel-level analysis to verify image content */
    if (!do_diag) goto skip_diag;
    {
    uint16_t *pix = (uint16_t *)frame->data;
    int total_pixels = frame->width * frame->height;
    uint32_t sample_count = (total_pixels > 1024) ? 1024 : total_pixels;
    uint32_t nonzero = 0;
    uint32_t sum_r = 0, sum_g = 0, sum_b = 0;
    uint16_t min_val = 0xFFFF, max_val = 0;
    float mean_val = 0.0f;

    /* Sample evenly across the frame */
    for (uint32_t i = 0; i < sample_count; i++) {
        uint32_t idx = (i * total_pixels) / sample_count;
        uint16_t p = pix[idx];
        if (p != 0) nonzero++;
        if (p < min_val) min_val = p;
        if (p > max_val) max_val = p;
        mean_val += (float)p;
        /* RGB565: R=bits[15:11], G=bits[10:5], B=bits[4:0] */
        sum_r += (p >> 11) & 0x1F;
        sum_g += (p >> 5) & 0x3F;
        sum_b += p & 0x1F;
    }
    mean_val /= (float)sample_count;

    /* Compute variance for a subset */
    float var = 0.0f;
    for (uint32_t i = 0; i < sample_count; i++) {
        uint32_t idx = (i * total_pixels) / sample_count;
        float d = (float)pix[idx] - mean_val;
        var += d * d;
    }
    var = var / (float)sample_count;
    float stddev = sqrtf(var);

    ESP_LOGI(TAG, "pix: nz=%u/%u min=0x%04X max=0x%04X mean=%.0f std=%.0f "
             "R=%u G=%u B=%u",
             nonzero, sample_count, min_val, max_val,
             mean_val, stddev,
             sum_r / sample_count, sum_g / sample_count, sum_b / sample_count);

    /* ASCII art dump: 32x24 grid, downsampled from full frame */
    {
        const int AW = 32, AH = 24;
        const char *ramp = " .,-:;+*o#%@";  /* dark → bright */
        int ramp_len = 11;
        printf("\n=== CAMERA ASCII ART (%dx%d → %dx%d) ===\n", (int)frame->width, (int)frame->height, AW, AH);
        for (int ay = 0; ay < AH; ay++) {
            printf("|");
            for (int ax = 0; ax < AW; ax++) {
                /* Sample center of each block */
                int fx = (ax * (int)frame->width) / AW + (int)frame->width / (2 * AW);
                int fy = (ay * (int)frame->height) / AH + (int)frame->height / (2 * AH);
                uint16_t p = pix[fy * frame->width + fx];
                /* Convert RGB565 to luminance */
                int r = (p >> 11) & 0x1F;
                int g = (p >> 5) & 0x3F;
                int b = p & 0x1F;
                int lum = (r * 77 + g * 150 + b * 29) >> 8;  /* approx luminance */
                int idx = (lum * ramp_len) / 64;  /* scale to 0..ramp_len-1 */
                if (idx >= ramp_len) idx = ramp_len - 1;
                printf("%c", ramp[idx]);
            }
            printf("|\n");
        }
        printf("=== END ASCII ART ===\n");
    }
    }
    skip_diag:;

    /* Run inference and measure wall-clock time. */
    int64_t t0 = esp_timer_get_time();
    std::list<dl::detect::result_t> &detect_results = s_detector->run(img);
    int64_t t1 = esp_timer_get_time();
    int64_t inf_us = t1 - t0;

    result->inference_ms = static_cast<uint32_t>(inf_us / 1000);
    result->count        = 0;

    if (do_diag)
    ESP_LOGI(TAG, "inf=%lldus results=%zu", inf_us, detect_results.size());

    /* Log individual detection scores when faces found */
    if (!detect_results.empty()) {
        int i = 0;
        for (const auto &res : detect_results) {
            ESP_LOGI(TAG, "  face[%d] score=%.3f box=[%d,%d,%d,%d]",
                     i++, res.score,
                     res.box[0], res.box[1], res.box[2], res.box[3]);
            if (i >= 3) break;
        }
    }

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
