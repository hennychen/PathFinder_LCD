/**
 * @file face_tracker.h
 * @brief 人脸检测与追踪模块（ESP-DL 神经网络版）
 *
 * 使用 MSRMNP_S8_V1 两阶段模型做人脸检测，
 * 通过比例控制器驱动 Pan/Tilt 舵机平滑追踪。
 */

#ifndef FACE_TRACKER_H
#define FACE_TRACKER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool   detected;
    int    cx, cy;
    int    w, h;
    float  score;
} face_info_t;

void face_tracker_start(void);
void face_tracker_stop(void);
bool face_tracker_is_running(void);
bool face_tracker_detected(void);
void face_tracker_get_info(face_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* FACE_TRACKER_H */
