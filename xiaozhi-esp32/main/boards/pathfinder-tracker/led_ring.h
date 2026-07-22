/**
 * @file led_ring.h
 * @brief WS2812 36-LED ring driver for AcousticEye sound direction indication.
 *
 * Maps sound source angle (0~360°) to a single LED on a 36-LED ring.
 * LED index = angle / 10 % 36.
 */

#ifndef LED_RING_H
#define LED_RING_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the WS2812 LED ring on BUILTIN_LED_GPIO (GPIO48).
 *        Call once at startup, before tracking_init().
 */
void led_ring_init(void);

/**
 * @brief Light up the LED corresponding to the given angle.
 *        All other LEDs are turned off. Auto-fades after ~1s of no sound.
 *
 * @param angle Sound source angle in degrees (0~360), or negative to clear.
 */
void led_ring_show_angle(float angle);

#ifdef __cplusplus
}
#endif

#endif /* LED_RING_H */
