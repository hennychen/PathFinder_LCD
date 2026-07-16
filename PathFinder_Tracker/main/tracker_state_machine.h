#ifndef TRACKER_STATE_MACHINE_H
#define TRACKER_STATE_MACHINE_H

#include <stdbool.h>
#include "tracker_config.h"

typedef struct {
    tracker_state_t current_state;
    float   smoothed_angle;
    float   last_valid_angle;
    int     invalid_count;
    int     search_count;
} tracker_ctx_t;

void tracker_sm_init(tracker_ctx_t *ctx);
void tracker_sm_step(tracker_ctx_t *ctx, float sound_angle, bool valid);

#endif /* TRACKER_STATE_MACHINE_H */
