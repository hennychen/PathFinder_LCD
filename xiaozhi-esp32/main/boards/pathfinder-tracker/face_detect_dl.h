/**
 * @file face_detect_dl.h
 * @brief ESP-DL 人脸检测 C 封装接口
 *
 * 将 espressif/human_face_detect 组件的 C++ HumanFaceDetect 类
 * 封装为 C 可调用接口，供 face_tracker.c 使用。
 *
 * 默认使用 MSR_S8_V1 + MNP_S8_V1 两阶段模型：
 *   - 第一阶段 MSR：120x160 输入，~33ms (ESP32-S3)
 *   - 第二阶段 MNP：48x48 输入，~6ms
 *   - 总延迟约 44ms，模型大小 187KB (FLASH_RODATA)
 */

#ifndef FACE_DETECT_DL_H
#define FACE_DETECT_DL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool   detected;
    int    cx, cy;
    int    w, h;
    float  score;
} face_detect_result_t;

bool face_detect_dl_init(void);
bool face_detect_dl_detect(const uint8_t *rgb565_data, int width, int height);
void face_detect_dl_get_result(face_detect_result_t *result);
void face_detect_dl_set_threshold(float score_thr);
bool face_detect_dl_is_loaded(void);

#ifdef __cplusplus
}
#endif

#endif /* FACE_DETECT_DL_H */
