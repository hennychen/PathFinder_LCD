#ifndef SOUND_LOCALIZER_H
#define SOUND_LOCALIZER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float angle;
    float confidence;
    bool  valid;
} localization_result_t;

void sound_localizer_init(float sample_rate);
void sound_localizer_set_threshold(float threshold);
localization_result_t sound_localizer_compute(float mic_data[4][256]);

#endif /* SOUND_LOCALIZER_H */
