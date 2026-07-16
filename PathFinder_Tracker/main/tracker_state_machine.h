#ifndef TRACKER_STATE_MACHINE_H
#define TRACKER_STATE_MACHINE_H

#include <stdbool.h>
#include "tracker_config.h"
#include "face_tracker.h"

typedef struct {
    tracker_state_t   current_state;
    float             smoothed_angle;
    float             last_valid_angle;
    int               invalid_count;
    int               search_count;
    /* Face tracking fields */
    face_tracker_ctx_t face_ctx;
    int               face_lost_count;
} tracker_ctx_t;

void tracker_sm_init(tracker_ctx_t *ctx);

/* Audio-driven state transition (Core 0, called per audio frame). */
void tracker_sm_step(tracker_ctx_t *ctx, float sound_angle, bool valid);

/* Vision-driven state transition (Core 0, called per vision message). */
void tracker_sm_step_vision(tracker_ctx_t *ctx, bool face_found,
                            int16_t cx, int16_t cy,
                            uint16_t w, uint16_t h);

#endif /* TRACKER_STATE_MACHINE_H */
