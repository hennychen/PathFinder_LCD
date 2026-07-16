/**
 * @file face_tracker.c
 * @brief Proportional face-tracking controller.
 *
 * Computes the pixel error between the face centre and the image centre,
 * applies a clamped P-correction, and drives the Pan/Tilt servos.
 */

#include "face_tracker.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "face_tracker";

/* ---- Helpers ---- */

static inline float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ---- Public API ---- */

void face_tracker_init(face_tracker_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    ctx->pan        = 90.0f;
    ctx->tilt       = 90.0f;
    ctx->lost_count = 0;
    ctx->tracking   = false;
    ESP_LOGI(TAG, "initialised: pan=%.1f tilt=%.1f", ctx->pan, ctx->tilt);
}

bool face_tracker_update(face_tracker_ctx_t *ctx, const face_box_t *target)
{
    if (ctx == NULL) {
        return false;
    }

    /* No target → accumulate lost frames and stop tracking. */
    if (target == NULL) {
        ctx->lost_count++;
        ctx->tracking = false;
        return false;
    }

    /* Face acquired → reset lost counter and mark tracking active. */
    ctx->lost_count = 0;
    ctx->tracking   = true;

    /* Face centre in image coordinates. */
    int cx = (int)target->x + (int)(target->width / 2);
    int cy = (int)target->y + (int)(target->height / 2);

    /* Pixel error from image centre (positive = right / down). */
    int err_x = cx - IMG_CENTER_X;
    int err_y = cy - IMG_CENTER_Y;

    /* Dead zone: face already centred → suppress servo jitter. */
    if (abs(err_x) < FACE_DEAD_ZONE_PX && abs(err_y) < FACE_DEAD_ZONE_PX) {
        return false;
    }

    /*
     * Proportional control.
     * Pan:  negative sign so that face-right (err_x > 0) decreases pan.
     * Tilt: negative sign so that face-down  (err_y > 0) decreases tilt.
     */
    float pan_delta  = -(float)err_x * FACE_PID_KP_PAN;
    float tilt_delta = -(float)err_y * FACE_PID_KP_TILT;

    /* Clamp per-frame step to avoid servo overshoot / mechanical jitter. */
    pan_delta  = clampf(pan_delta,  -FACE_MAX_STEP_DEG, FACE_MAX_STEP_DEG);
    tilt_delta = clampf(tilt_delta, -FACE_MAX_STEP_DEG, FACE_MAX_STEP_DEG);

    /* Integrate and clamp to the servo's mechanical range [0°, 180°]. */
    ctx->pan  = clampf(ctx->pan  + pan_delta,  0.0f, 180.0f);
    ctx->tilt = clampf(ctx->tilt + tilt_delta, 0.0f, 180.0f);

    /* Drive both servos. */
    esp_err_t r_pan  = drv_servo_set_angle(SERVO_PAN,  ctx->pan);
    esp_err_t r_tilt = drv_servo_set_angle(SERVO_TILT, ctx->tilt);
    if (r_pan != ESP_OK) {
        ESP_LOGW(TAG, "pan servo set failed: %s", esp_err_to_name(r_pan));
    }
    if (r_tilt != ESP_OK) {
        ESP_LOGW(TAG, "tilt servo set failed: %s", esp_err_to_name(r_tilt));
    }

    ESP_LOGD(TAG, "cx=%d cy=%d err=[%d,%d] pan=%.1f tilt=%.1f",
             cx, cy, err_x, err_y, ctx->pan, ctx->tilt);
    return true;
}

int face_tracker_get_lost_count(const face_tracker_ctx_t *ctx)
{
    return (ctx != NULL) ? ctx->lost_count : 0;
}
