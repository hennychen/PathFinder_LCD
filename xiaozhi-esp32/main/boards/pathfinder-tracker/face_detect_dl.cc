/**
 * @file face_detect_dl.cc
 * @brief ESP-DL 人脸检测 C++ 实现（封装 HumanFaceDetect）
 */

#include "face_detect_dl.h"

#include "human_face_detect.hpp"
#include "dl_image_define.hpp"

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "FaceDetectDL";

static HumanFaceDetect *s_detector  = nullptr;
static bool              s_loaded    = false;
static float             s_score_thr = 0.5f;

static face_detect_result_t s_result = {};

static bool ensure_loaded(void)
{
    if (s_loaded) return true;

    if (!s_detector) {
        s_detector = new HumanFaceDetect(HumanFaceDetect::MSRMNP_S8_V1, false);
        if (!s_detector) {
            ESP_LOGE(TAG, "Failed to allocate HumanFaceDetect");
            return false;
        }
    }

    s_loaded = true;
    s_detector->set_score_thr(s_score_thr, 0);
    s_detector->set_score_thr(s_score_thr, 1);

    ESP_LOGI(TAG, "MSRMNP_S8_V1 model loaded (score_thr=%.2f)", s_score_thr);
    return true;
}

extern "C" bool face_detect_dl_init(void)
{
    return ensure_loaded();
}

extern "C" bool face_detect_dl_detect(const uint8_t *rgb565_data, int width, int height)
{
    if (!rgb565_data || width <= 0 || height <= 0) {
        ESP_LOGE(TAG, "Invalid input: data=%p w=%d h=%d", rgb565_data, width, height);
        return false;
    }

    if (!ensure_loaded()) {
        ESP_LOGE(TAG, "Model not loaded");
        return false;
    }

    dl::image::img_t img = {};
    img.data     = (void *)rgb565_data;
    img.width    = (uint16_t)width;
    img.height   = (uint16_t)height;
    img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565BE;

    int64_t t0 = esp_timer_get_time();

    std::list<dl::detect::result_t> &results = s_detector->run(img);

    int64_t elapsed_ms = (esp_timer_get_time() - t0) / 1000;

    if (results.empty()) {
        s_result.detected = false;
        ESP_LOGD(TAG, "No face detected (%lld ms)", (long long)elapsed_ms);
        return false;
    }

    auto best = results.begin();
    for (auto it = results.begin(); it != results.end(); ++it) {
        if (it->score > best->score) {
            best = it;
        }
    }

    int x1 = best->box[0];
    int y1 = best->box[1];
    int x2 = best->box[2];
    int y2 = best->box[3];

    s_result.detected = true;
    s_result.cx       = (x1 + x2) / 2;
    s_result.cy       = (y1 + y2) / 2;
    s_result.w        = x2 - x1;
    s_result.h        = y2 - y1;
    s_result.score    = best->score;

    ESP_LOGD(TAG, "Face: score=%.2f bbox=[%d,%d,%d,%d] center=(%d,%d) %dx%d (%lld ms)",
             best->score, x1, y1, x2, y2,
             s_result.cx, s_result.cy, s_result.w, s_result.h,
             (long long)elapsed_ms);

    return true;
}

extern "C" void face_detect_dl_get_result(face_detect_result_t *result)
{
    if (result) {
        *result = s_result;
    }
}

extern "C" void face_detect_dl_set_threshold(float score_thr)
{
    if (score_thr < 0.01f) score_thr = 0.01f;
    if (score_thr > 0.99f) score_thr = 0.99f;

    s_score_thr = score_thr;
    if (s_loaded && s_detector) {
        s_detector->set_score_thr(score_thr, 0);
        s_detector->set_score_thr(score_thr, 1);
    }
}

extern "C" bool face_detect_dl_is_loaded(void)
{
    return s_loaded;
}
