/**
 * @file tracking_coordinator.c
 * @brief 多模态云台追踪协调器实现
 *
 * 后台任务以 20Hz 运行，将目标角度平滑过渡到实际舵机角度。
 * 平滑因子确保舵机不会突跳，延长机械寿命。
 */

#include "tracking_coordinator.h"
#include "servo_controller.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <stdlib.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#define TAG "Tracker"

/* ── 追踪参数 ── */
#define TRACK_DEADBAND_DEG      12      /* 死区（°）：变化小于此值不更新，避免环境噪音抖动 */
#define TRACK_SMOOTH_NUM        3       /* 平滑：每次移动 target/3 → ~3 步到位 */
#define TRACK_TASK_PERIOD_MS    50      /* 任务周期 = 50ms → 20Hz */
#define SOUND_ANGLE_COOLDOWN_MS 800    /* 声源角度更新冷却：800ms 内忽略新的角度变化 */

static track_mode_t s_mode         = TRACK_MODE_IDLE;
static float        s_sound_angle  = -1.0f;
static int64_t      s_last_sound_us = 0;
static int64_t      s_last_pan_update_us = 0;  /* 上次 Pan 实际更新时间，用于冷却过滤 */
static int          s_target_pan   = 90;
static int          s_target_tilt  = 90;

/* ── 声源角度 → Pan 角度的余弦投影 ──
 *
 * atan2 坐标系（4麦克风 TDM）：
 *   sound_angle: 0°=右(X+), 90°=前(Y+), 180°=左(X-), 270°=后(Y-)
 *   pan:         90°=居中, 180°=最右, 0°=最左
 *
 * 公式: pan = 90 + k * cos(angle)
 *   0°(右)   → 90 + k   (右转)
 *   90°(前)  → 90        (居中)
 *   180°(左) → 90 - k   (左转)
 *   270°(后) → 90        (居中)
 *
 * 4麦克风已区分前后，不再忽略后方，但后方映射到居中（Pan无法转向后方）。
 */
static int sound_angle_to_pan(float angle)
{
    /* 余弦映射：cos(0°)=1(右), cos(90°)=0(前/居中), cos(180°)=-1(左) */
    float cos_val = cosf(angle * PI / 180.0f);
    /* 幅度衰减系数 0.65：在 ±90°(前后) 处给舵机留余量，
     * 避免在正左/正右(cos=±1)时打到机械极限导致堵转。 */
    float pan = 90.0f + 60.0f * 0.65f * cos_val;
    /* 等价于 pan = 90 + 39 * cos(angle)，范围约 [51, 129] */
    int pan_i = (int)(pan + 0.5f);
    /* 底层 clamp 兜底 */
    if (pan_i < SERVO_PAN_SAFE_MIN)   pan_i = SERVO_PAN_SAFE_MIN;
    if (pan_i > SERVO_PAN_SAFE_MAX)   pan_i = SERVO_PAN_SAFE_MAX;
    return pan_i;
}

void tracking_init(void)
{
    /* 上电即进入 AUTO 模式：声源定位激活后 Pan 舵机自动追踪。
     * 用户仍可通过 MCP 工具 self.servo.set_mode 切换到 manual/idle。 */
    s_mode         = TRACK_MODE_AUTO;
    s_target_pan   = 90;
    s_target_tilt  = 90;
    s_sound_angle  = -1.0f;
    s_last_sound_us = 0;

    servo_set_pan_tilt(90, 90);
    ESP_LOGI(TAG, "Tracking coordinator initialized (AUTO, pan=90, tilt=90)");
}

void tracking_set_mode(track_mode_t mode)
{
    if (s_mode == mode) return;
    s_mode = mode;
    const char *names[] = {"IDLE", "AUTO", "FACE", "MANUAL"};
    ESP_LOGI(TAG, "Mode → %s", names[mode]);
}

track_mode_t tracking_get_mode(void)
{
    return s_mode;
}

void tracking_on_sound_angle(float angle)
{
    if (angle < 0.0f) {
        /* 声源无效，不更新 */
        return;
    }

    s_sound_angle   = angle;
    s_last_sound_us = esp_timer_get_time();

    if (s_mode != TRACK_MODE_AUTO) return;

    int new_pan = sound_angle_to_pan(angle);
    /* new_pan 总是有效（后方映射到居中，不再返回 -1） */

    /* 死区检查：角度变化不足 12° 不更新 */
    if (abs(new_pan - s_target_pan) < TRACK_DEADBAND_DEG) return;

    /* 冷却检查：800ms 内不重复更新，防止环境噪音导致摆动 */
    int64_t now = esp_timer_get_time();
    if (s_last_pan_update_us > 0 &&
        (now - s_last_pan_update_us) < SOUND_ANGLE_COOLDOWN_MS * 1000) {
        return;
    }

    s_target_pan       = new_pan;
    s_last_pan_update_us = now;
}

void tracking_manual_set_pan(int angle)
{
    tracking_set_mode(TRACK_MODE_MANUAL);
    /* 收窄到 Pan 安全范围，防止 MCP/AI 命令到机械极限 */
    if (angle < SERVO_PAN_SAFE_MIN)   angle = SERVO_PAN_SAFE_MIN;
    if (angle > SERVO_PAN_SAFE_MAX)   angle = SERVO_PAN_SAFE_MAX;
    s_target_pan = angle;
    ESP_LOGI(TAG, "Manual Pan → %d", angle);
}

void tracking_manual_set_tilt(int angle)
{
    tracking_set_mode(TRACK_MODE_MANUAL);
    /* 收窄到 Tilt 安全范围，防止 MCP/AI 命令到机械极限 */
    if (angle < SERVO_TILT_SAFE_MIN)   angle = SERVO_TILT_SAFE_MIN;
    if (angle > SERVO_TILT_SAFE_MAX)   angle = SERVO_TILT_SAFE_MAX;
    s_target_tilt = angle;
    ESP_LOGI(TAG, "Manual Tilt → %d", angle);
}

void tracking_manual_set_pan_tilt(int pan, int tilt)
{
    tracking_set_mode(TRACK_MODE_MANUAL);
    /* 收窄到各自安全范围，防止 MCP/AI 命令到机械极限 */
    if (pan < SERVO_PAN_SAFE_MIN)   pan = SERVO_PAN_SAFE_MIN;
    if (pan > SERVO_PAN_SAFE_MAX)   pan = SERVO_PAN_SAFE_MAX;
    if (tilt < SERVO_TILT_SAFE_MIN)   tilt = SERVO_TILT_SAFE_MIN;
    if (tilt > SERVO_TILT_SAFE_MAX)   tilt = SERVO_TILT_SAFE_MAX;
    s_target_pan  = pan;
    s_target_tilt = tilt;
    ESP_LOGI(TAG, "Manual Pan→%d Tilt→%d", pan, tilt);
}

void tracking_on_face_update(int pan_delta, int tilt_delta)
{
    if (s_mode != TRACK_MODE_FACE) return;

    /* 叠加增量到当前目标 */
    s_target_pan  += pan_delta;
    s_target_tilt += tilt_delta;

    /* Clamp 到安全范围 */
    if (s_target_pan < SERVO_PAN_SAFE_MIN)   s_target_pan  = SERVO_PAN_SAFE_MIN;
    if (s_target_pan > SERVO_PAN_SAFE_MAX)   s_target_pan  = SERVO_PAN_SAFE_MAX;
    if (s_target_tilt < SERVO_TILT_SAFE_MIN) s_target_tilt = SERVO_TILT_SAFE_MIN;
    if (s_target_tilt > SERVO_TILT_SAFE_MAX) s_target_tilt = SERVO_TILT_SAFE_MAX;
}

void tracking_face_lost(void)
{
    if (s_mode != TRACK_MODE_FACE) return;
    s_target_pan  = 90;
    s_target_tilt = 90;
}

int tracking_get_pan(void)  { return s_target_pan; }
int tracking_get_tilt(void) { return s_target_tilt; }

/* ── 后台平滑追踪任务 ──
 * 以 20Hz 频率将当前舵机角度平滑过渡到目标角度。
 * AUTO 模式下声源超时会自动将 Pan 回到居中。 */
static void tracking_task_fn(void *arg)
{
    int cur_pan  = 90;
    int cur_tilt = 90;

    while (1) {
        /* AUTO 模式：声源超时不再自动回中（之前导致舵机永久摆动）。
         * 用户可通过 MCP "center" 命令手动回中。 */

        /* FACE 模式：目标由 face_tracker 的 tracking_on_face_update() 更新，
         * 丢失时由 tracking_face_lost() 回中，此处无需额外处理。 */

        /* IDLE 模式：保持当前位置 */
        if (s_mode == TRACK_MODE_IDLE) {
            vTaskDelay(pdMS_TO_TICKS(TRACK_TASK_PERIOD_MS));
            continue;
        }

        /* 平滑移动 Pan */
        if (cur_pan != s_target_pan) {
            int diff = s_target_pan - cur_pan;
            int step = diff / TRACK_SMOOTH_NUM;
            if (step == 0) step = (diff > 0) ? 1 : -1;
            cur_pan += step;
            if ((step > 0 && cur_pan > s_target_pan) ||
                (step < 0 && cur_pan < s_target_pan)) {
                cur_pan = s_target_pan;
            }
            servo_set_pan(cur_pan);
        }

        /* 平滑移动 Tilt */
        if (cur_tilt != s_target_tilt) {
            int diff = s_target_tilt - cur_tilt;
            int step = diff / TRACK_SMOOTH_NUM;
            if (step == 0) step = (diff > 0) ? 1 : -1;
            cur_tilt += step;
            if ((step > 0 && cur_tilt > s_target_tilt) ||
                (step < 0 && cur_tilt < s_target_tilt)) {
                cur_tilt = s_target_tilt;
            }
            servo_set_tilt(cur_tilt);
        }

        vTaskDelay(pdMS_TO_TICKS(TRACK_TASK_PERIOD_MS));
    }
}

void tracking_start_task(void)
{
    xTaskCreate(tracking_task_fn, "tracking", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "Tracking task started (20Hz smooth)");
}
