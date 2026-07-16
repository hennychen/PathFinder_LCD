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
#include "drv_uart_comm.h"
#include "esp_log.h"

static const char *TAG = "tracker_sm";

/* ---- Tuning constants ---- */
#define SMOOTH_ALPHA          0.3f    /* Exponential smoothing factor         */
#define MAX_INVALID_FRAMES    100     /* ~2 s @20 ms/frame → SEARCH           */
#define SEARCH_DURATION       250     /* ~5 s in SEARCH → IDLE                */
#define SERVO_STEP_MAX        3.0f    /* Max deg/frame to reduce servo jitter */

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
            drv_uart_send_state((uint8_t)TRACKER_STATE_ACOUSTIC_TRACK);
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

            /* Forward smoothed angle to Board A. */
            drv_uart_send_angle(ctx->smoothed_angle, 1);
        } else {
            if (++ctx->invalid_count >= MAX_INVALID_FRAMES) {
                ctx->search_count  = 0;
                ctx->current_state = TRACKER_STATE_SEARCH;
                ESP_LOGI(TAG, "ACOUSTIC_TRACK -> SEARCH (signal lost)");
                drv_uart_send_state((uint8_t)TRACKER_STATE_SEARCH);
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

        if (valid) {
            /* Re-acquired – re-seed smoother and resume tracking. */
            ctx->smoothed_angle   = angle_normalize(sound_angle);
            ctx->last_valid_angle = ctx->smoothed_angle;
            ctx->invalid_count    = 0;
            ctx->current_state    = TRACKER_STATE_ACOUSTIC_TRACK;
            ESP_LOGI(TAG, "SEARCH -> ACOUSTIC_TRACK (re-acquired, angle=%.1f deg)", sound_angle);
            drv_uart_send_state((uint8_t)TRACKER_STATE_ACOUSTIC_TRACK);
        } else if (ctx->search_count >= SEARCH_DURATION) {
            /* Give up – centre the servo and go idle. */
            drv_servo_set_angle(SERVO_PAN, 90.0f);
            drv_ws2812_clear();
            drv_ws2812_show();
            ctx->current_state = TRACKER_STATE_IDLE;
            ESP_LOGI(TAG, "SEARCH -> IDLE (search timeout)");
            drv_uart_send_state((uint8_t)TRACKER_STATE_IDLE);
        }
        break;
    }

    /* ----------------------------------------------------------- */
    case TRACKER_STATE_FACE_TRACK:
        /* Placeholder – Phase 3 will implement vision-based tracking. */
        break;

    default:
        ctx->current_state = TRACKER_STATE_IDLE;
        break;
    }
}
