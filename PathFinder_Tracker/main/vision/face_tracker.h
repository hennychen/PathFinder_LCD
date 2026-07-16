/**
 * @file face_tracker.h
 * @brief Proportional (P) face-tracking controller for the Pan/Tilt gimbal.
 *
 * Given a detected face box, computes the pixel error from the image centre
 * and drives the two MG90S servos with a clamped proportional step.
 */

#ifndef FACE_TRACKER_H
#define FACE_TRACKER_H

#include "face_detector.h"
#include "drv_servo.h"
#include <stdbool.h>

/* ---- P-controller tuning ---- */
#define FACE_PID_KP_PAN     0.08f
#define FACE_PID_KP_TILT    0.06f
#define FACE_DEAD_ZONE_PX   10
#define FACE_MAX_STEP_DEG   2.0f

#define IMG_CENTER_X  (FACE_IMG_WIDTH / 2)
#define IMG_CENTER_Y  (FACE_IMG_HEIGHT / 2)

typedef struct {
    float pan;
    float tilt;
    int   lost_count;
    bool  tracking;
} face_tracker_ctx_t;

/**
 * @brief Initialise the tracker context: pan=90°, tilt=90°, not tracking.
 */
void face_tracker_init(face_tracker_ctx_t *ctx);

/**
 * @brief Update servo angles based on the latest face box.
 *
 * @param ctx     Tracker context (stateful pan/tilt angles).
 * @param target  Largest detected face, or NULL if no face found.
 * @return true if the servos were moved this frame, false otherwise
 *         (no face, in dead-zone, or NULL context).
 */
bool face_tracker_update(face_tracker_ctx_t *ctx, const face_box_t *target);

/**
 * @brief Number of consecutive frames with no face detected.
 */
int face_tracker_get_lost_count(const face_tracker_ctx_t *ctx);

#endif /* FACE_TRACKER_H */
