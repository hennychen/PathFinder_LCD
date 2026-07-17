/**
 * @file tracker_state_machine.c
 * @brief Sound-source → servo closed-loop state machine.
 *
 * Orchestrates the ES7210 sound localiser, WS2812 LED ring, and MG90S pan
 * servo into a coherent tracking system.
 *
 * States:
 *   IDLE            – LEDs off, waiting for sound.
 *   ACOUSTIC_TRACK  – Actively tracking with angle smoothing + servo slew limit.
 *   SEARCH          – Holding last position after signal loss, then IDLE.
 *   FACE_TRACK      – Placeholder for Phase 3.
 */

#include "tracker_state_machine.h"
#include "drv_servo.h"
#include "drv_ws2812.h"
#include "comm_link.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "tracker_sm";

/* ---- Tuning constants ---- */
#define SMOOTH_ALPHA          0.3f    /* Exponential smoothing factor         */
#define MAX_INVALID_FRAMES    100     /* ~2 s @20 ms/frame → SEARCH           */
#define SEARCH_DURATION       250     /* ~5 s in SEARCH → IDLE                */
#define SERVO_STEP_MAX        3.0f    /* Max deg/frame to reduce servo jitter */
#define FACE_LOST_MAX         15      /* Frames before face→acoustic fallback  */
#define TILT_SCAN_AMPLITUDE   30.0f   /* ±30° scan range around center        */
#define TILT_SCAN_PERIOD      200     /* Frames for full sweep (≈4 s)          */

/* WS2812 indication colours (GRB) */
#define LED_FACE_COLOR        0x0000FF  /* Green when face-tracking   */
#define LED_FACE_FOUND_COLOR  0x00FF00  /* Red flash on face detected */

/* ----------------------------------------------------------------- */
/*  Helpers                                                          */
/* ----------------------------------------------------------------- */

/* Shortest angular difference (a − b) accounting for 360° wraparound.
 * Result lies in (−180, 180]. */
static float angle_diff(float a, float b)
{
    float diff = a - b;
    while (diff > 180.0f)  diff -= 360.0f;
    while (diff <= -180.0f) diff += 360.0f;
    return diff;
}

/* Normalize an angle to [0, 360). */
static float angle_normalize(float angle)
{
    while (angle < 0.0f)     angle += 360.0f;
    while (angle >= 360.0f)  angle -= 360.0f;
    return angle;
}

/* Move the pan servo towards target_pan, clamped to ±SERVO_STEP_MAX per call. */
static void servo_slew_to(float target_pan)
{
    float current = drv_servo_get_angle(SERVO_PAN);
    float delta   = target_pan - current;
    if (delta >  SERVO_STEP_MAX) delta =  SERVO_STEP_MAX;
    if (delta < -SERVO_STEP_MAX) delta = -SERVO_STEP_MAX;
    drv_servo_set_angle(SERVO_PAN, current + delta);
}

/* ----------------------------------------------------------------- */
/*  Public API                                                       */
/* ----------------------------------------------------------------- */

void tracker_sm_init(tracker_ctx_t *ctx)
{
    ctx->current_state    = TRACKER_STATE_IDLE;
    ctx->smoothed_angle   = 0.0f;
    ctx->last_valid_angle = 0.0f;
    ctx->invalid_count    = 0;
    ctx->search_count     = 0;
    ctx->face_lost_count  = 0;
    face_tracker_init(&ctx->face_ctx);
}

void tracker_sm_step(tracker_ctx_t *ctx, float sound_angle, bool valid)
{
    switch (ctx->current_state) {

    /* ----------------------------------------------------------- */
    case TRACKER_STATE_IDLE:
    {
        drv_ws2812_clear();
        drv_ws2812_show();

        if (valid) {
            /* Seed the smoother with the first reading so it doesn't
               have to slew from 0°. */
            ctx->smoothed_angle   = angle_normalize(sound_angle);
            ctx->last_valid_angle = ctx->smoothed_angle;
            ctx->invalid_count    = 0;
            ctx->current_state    = TRACKER_STATE_ACOUSTIC_TRACK;
            ESP_LOGI(TAG, "IDLE -> ACOUSTIC_TRACK (angle=%.1f deg)", sound_angle);
            comm_link_send_state((uint8_t)TRACKER_STATE_ACOUSTIC_TRACK);
        }
        break;
    }

    /* ----------------------------------------------------------- */
    case TRACKER_STATE_ACOUSTIC_TRACK:
    {
        if (valid) {
            /* Exponential smoothing with 360° wraparound handling. */
            float diff = angle_diff(sound_angle, ctx->smoothed_angle);
            ctx->smoothed_angle   = angle_normalize(ctx->smoothed_angle + diff * SMOOTH_ALPHA);
            ctx->last_valid_angle = ctx->smoothed_angle;
            ctx->invalid_count    = 0;

            /* LED feedback at the smoothed angle. */
            drv_ws2812_show_angle(ctx->smoothed_angle);

            /* Servo movement with per-frame slew limiting. */
            float target_pan = drv_servo_angle_from_sound(ctx->smoothed_angle);
            servo_slew_to(target_pan);

            /* Centre tilt during acoustic tracking (no elevation data
               from sound source localisation). */
            float cur_tilt = drv_servo_get_angle(SERVO_TILT);
            float tilt_delta = 90.0f - cur_tilt;
            if (tilt_delta >  SERVO_STEP_MAX) tilt_delta =  SERVO_STEP_MAX;
            if (tilt_delta < -SERVO_STEP_MAX) tilt_delta = -SERVO_STEP_MAX;
            drv_servo_set_angle(SERVO_TILT, cur_tilt + tilt_delta);

            /* Forward smoothed angle to Board A via best channel. */
            comm_link_send_angle(ctx->smoothed_angle, 1);
        } else {
            if (++ctx->invalid_count >= MAX_INVALID_FRAMES) {
                ctx->search_count  = 0;
                ctx->current_state = TRACKER_STATE_SEARCH;
                ESP_LOGI(TAG, "ACOUSTIC_TRACK -> SEARCH (signal lost)");
                comm_link_send_state((uint8_t)TRACKER_STATE_SEARCH);
            }
        }
        break;
    }

    /* ----------------------------------------------------------- */
    case TRACKER_STATE_SEARCH:
    {
        ctx->search_count++;

        /* Hold LED + servo at the last known valid direction. */
        drv_ws2812_show_angle(ctx->last_valid_angle);
        float hold_pan = drv_servo_angle_from_sound(ctx->last_valid_angle);
        servo_slew_to(hold_pan);

        /* Tilt scanning: sinusoidal sweep to search for faces/objects. */
        float scan_phase = (float)ctx->search_count / (float)TILT_SCAN_PERIOD;
        float target_tilt = 90.0f + TILT_SCAN_AMPLITUDE * sinf(scan_phase * 2.0f * 3.14159265f);
        float cur_tilt = drv_servo_get_angle(SERVO_TILT);
        float td = target_tilt - cur_tilt;
        if (td >  SERVO_STEP_MAX) td =  SERVO_STEP_MAX;
        if (td < -SERVO_STEP_MAX) td = -SERVO_STEP_MAX;
        drv_servo_set_angle(SERVO_TILT, cur_tilt + td);

        if (valid) {
            /* Re-acquired – re-seed smoother and resume tracking. */
            ctx->smoothed_angle   = angle_normalize(sound_angle);
            ctx->last_valid_angle = ctx->smoothed_angle;
            ctx->invalid_count    = 0;
            ctx->current_state    = TRACKER_STATE_ACOUSTIC_TRACK;
            ESP_LOGI(TAG, "SEARCH -> ACOUSTIC_TRACK (re-acquired, angle=%.1f deg)", sound_angle);
            comm_link_send_state((uint8_t)TRACKER_STATE_ACOUSTIC_TRACK);
        } else if (ctx->search_count >= SEARCH_DURATION) {
            /* Give up – centre the servo and go idle. */
            drv_servo_set_angle(SERVO_PAN, 90.0f);
            drv_ws2812_clear();
            drv_ws2812_show();
            ctx->current_state = TRACKER_STATE_IDLE;
            ESP_LOGI(TAG, "SEARCH -> IDLE (search timeout)");
            comm_link_send_state((uint8_t)TRACKER_STATE_IDLE);
        }
        break;
    }

    /* ----------------------------------------------------------- */
    case TRACKER_STATE_FACE_TRACK:
        /* Handled entirely by tracker_sm_step_vision().
           Audio frames are ignored in this state to prevent
           servo contention between PID and acoustic loops. */
        break;

    default:
        ctx->current_state = TRACKER_STATE_IDLE;
        break;
    }
}

/* ----------------------------------------------------------------- */
/*  Vision-driven state step (called from main loop per vision msg)  */
/* ----------------------------------------------------------------- */

void tracker_sm_step_vision(tracker_ctx_t *ctx, bool face_found,
                            int16_t cx, int16_t cy,
                            uint16_t w, uint16_t h)
{
    /* Build face box from vision message */
    face_box_t box;
    box.x = (int16_t)(cx - w / 2);
    box.y = (int16_t)(cy - h / 2);
    box.width  = w;
    box.height = h;
    box.confidence = 1.0f;

    switch (ctx->current_state) {

    case TRACKER_STATE_IDLE:
    case TRACKER_STATE_ACOUSTIC_TRACK:
    case TRACKER_STATE_SEARCH:
        if (face_found) {
            /* Transition to face tracking */
            tracker_state_t prev = ctx->current_state;
            ctx->face_lost_count = 0;
            face_tracker_init(&ctx->face_ctx);
            face_tracker_update(&ctx->face_ctx, &box);
            ctx->current_state = TRACKER_STATE_FACE_TRACK;
            ESP_LOGI(TAG, "%s -> FACE_TRACK (face detected %dx%d at %d,%d)",
                     prev == TRACKER_STATE_IDLE ? "IDLE" :
                     prev == TRACKER_STATE_ACOUSTIC_TRACK ? "ACOUSTIC" : "SEARCH",
                     w, h, cx, cy);
            comm_link_send_state((uint8_t)TRACKER_STATE_FACE_TRACK);
            drv_ws2812_clear();
            drv_ws2812_show();
        }
        break;

    case TRACKER_STATE_FACE_TRACK:
        if (face_found) {
            ctx->face_lost_count = 0;
            bool moved = face_tracker_update(&ctx->face_ctx, &box);
            /* Subtle LED feedback: single LED indicates face lock (green) */
            drv_ws2812_clear();
            drv_ws2812_set_led(0, 0, 255, 0);
            drv_ws2812_show();
        } else {
            /* No face box — feed NULL to increment lost counter */
            bool still = face_tracker_update(&ctx->face_ctx, NULL);
            if (++ctx->face_lost_count >= FACE_LOST_MAX) {
                /* Fallback: resume acoustic tracking */
                ctx->current_state = TRACKER_STATE_ACOUSTIC_TRACK;
                ctx->invalid_count = 0;
                ESP_LOGI(TAG, "FACE_TRACK -> ACOUSTIC_TRACK (face lost %d frames)",
                         FACE_LOST_MAX);
                comm_link_send_state((uint8_t)TRACKER_STATE_ACOUSTIC_TRACK);
            }
        }
        break;

    default:
        break;
    }
}
