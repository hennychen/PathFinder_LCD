#ifndef DRV_SERVO_H
#define DRV_SERVO_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    SERVO_PAN = 0,
    SERVO_TILT = 1,
} servo_id_t;

esp_err_t drv_servo_init(void);
esp_err_t drv_servo_set_angle(servo_id_t id, float angle);
float drv_servo_get_angle(servo_id_t id);
float drv_servo_angle_from_sound(float sound_angle);

#endif /* DRV_SERVO_H */
