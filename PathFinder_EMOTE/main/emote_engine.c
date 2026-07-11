/**
 * @file emote_engine.c
 * @brief 表情联动引擎实现 — 传感器数据驱动
 *
 * 设计理念：
 *   不再使用运动事件驱动（运动事件频繁翻转导致抖动/花屏），
 *   而是根据各传感器数据综合评估，选择最合适的表情。
 *
 * 评估流程 (每 EVAL_INTERVAL_MS = 5秒)：
 *   1. 读取环境传感器快照 (温度/湿度/气压/UV) + IMU 倾角
 *   2. 按优先级评估规则表（UV极端 > 温度异常 > 倾角大 > 默认）
 *   3. 推荐表情与当前不同 → 切换；相同 → 无操作
 *
 * 手动点击：切换到下一个动画，10秒内不自动评估
 */
#include "emote_engine.h"
#include "app_emote_assets.h"
#include "sensor_manager.h"
#include "motion_engine.h"
#include "lv_eaf.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "emote_eng";

#define EAF_FRAME_DELAY_MS      200
#define EVAL_INTERVAL_MS        5000   /* 传感器数据评估间隔 5s */
#define MANUAL_OVERRIDE_MS      10000  /* 手动切换后 10s 不自动评估 */

/* ── 手动轮播列表 ── */
static const char *s_manual_anim_names[] = {
    "smile_05s",
    "smile_static",
    "ponder_05s",
    "question_05s",
    "yawn_20s",
    "sad_05s15s",
    "shy_20s_40s",
    "sigh_20s_40s",
    "mock_05s",
    "music_25s",
    "badminton_12",
    "cry_10s_20s",
};
#define MANUAL_COUNT (sizeof(s_manual_anim_names) / sizeof(s_manual_anim_names[0]))

/* ── 传感器数据 → 表情映射规则 (按优先级从高到低) ── */
typedef struct {
    const char *emote_name;   /* EAF 动画名 (前缀匹配) */
    uint8_t     priority;     /* 优先级 (高者优先) */
    const char *reason;       /* 触发原因 (调试用) */
} sensor_rule_t;

/* ── 全局状态 ── */
static struct {
    lv_obj_t  *eaf_obj;
    lv_obj_t  *name_label;

    /* 当前播放 */
    int       current_idx;          /* 当前动画序号 (-1 = 未设置) */
    int64_t   last_eval_us;         /* 上次评估时间 */
    int64_t   manual_override_us;   /* 手动覆盖到期时间 */

    /* 手动轮播 */
    int       manual_idx;
} s_state;

/* ─────────────────────────────────────────────────────────
 *  内部辅助函数
 * ───────────────────────────────────────────────────────── */

/* ── 动画名称友好映射表（前缀匹配 → 友好显示名）── */
typedef struct {
    const char *prefix;    /* 原始名前缀 */
    const char *display;   /* 友好显示名 */
} emote_name_map_t;

static const emote_name_map_t s_name_map[] = {
    {"angry",       "Angry"},
    {"asleep",      "Asleep"},
    {"badminton",   "Badminton"},
    {"confident",   "Confident"},
    {"cry",         "Cry"},
    {"investigate", "Investigate"},
    {"laugh",       "Laugh"},
    {"leisure",     "Leisure"},
    {"mock",        "Mock"},
    {"music",       "Music"},
    {"mute",        "Mute"},
    {"panic",       "Panic"},
    {"ponder",      "Ponder"},
    {"question",    "Question"},
    {"sad",         "Sad"},
    {"shocked",     "Shocked"},
    {"shy",         "Shy"},
    {"sigh",        "Sigh"},
    {"smile",       "Smile"},
    {"snigger",     "Snigger"},
    {"yawn",        "Yawn"},
    {"yummy",       "Yummy"},
};
#define NAME_MAP_COUNT (sizeof(s_name_map) / sizeof(s_name_map[0]))

/* 将原始动画名转换为友好显示名 */
static const char *emote_friendly_name(const char *raw_name)
{
    if (!raw_name) return "EMOTE";
    for (size_t i = 0; i < NAME_MAP_COUNT; i++) {
        size_t plen = strlen(s_name_map[i].prefix);
        if (strncmp(raw_name, s_name_map[i].prefix, plen) == 0) {
            return s_name_map[i].display;
        }
    }
    return raw_name;  /* 未匹配时返回原始名 */
}

/* 动画名称容错查找：先精确，再前缀匹配 */
static int find_emote_index(const char *name)
{
    if (!name) return -1;

    int idx = app_emote_assets_find_by_name(name);
    if (idx >= 0) return idx;

    size_t name_len = strlen(name);
    size_t count = app_emote_assets_get_count();
    for (size_t i = 0; i < count; i++) {
        const char *emote_name = app_emote_assets_get_name(i);
        if (emote_name && strncmp(emote_name, name, name_len) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* 切换 EAF 动画 (pause → 切换 → resume，避免带宽峰值) */
static void play_emote(int idx)
{
    if (!s_state.eaf_obj || idx < 0) return;

    /* 相同动画不重载 */
    if (idx == s_state.current_idx) return;

    size_t sz = 0;
    const void *data = app_emote_assets_get_data((size_t)idx, &sz);
    if (!data || sz == 0) {
        ESP_LOGW(TAG, "动画 %d 数据为空", idx);
        return;
    }

    lv_eaf_pause(s_state.eaf_obj);
    lv_eaf_set_src_data(s_state.eaf_obj, data, sz);
    lv_eaf_set_loop_count(s_state.eaf_obj, -1);
    lv_eaf_set_frame_delay(s_state.eaf_obj, EAF_FRAME_DELAY_MS);

    const char *name = app_emote_assets_get_name((size_t)idx);
    if (name && s_state.name_label)
        lv_label_set_text(s_state.name_label, emote_friendly_name(name));

    lv_eaf_resume(s_state.eaf_obj);
    s_state.current_idx = idx;

    ESP_LOGI(TAG, "播放 [%d] %s (%zuB)", idx, name ? name : "?", sz);
}

/* ─────────────────────────────────────────────────────────
 *  传感器数据 → 表情评估
 * ───────────────────────────────────────────────────────── */

/* 根据传感器数据返回推荐表情 */
static sensor_rule_t evaluate_sensors(void)
{
    env_snapshot_t env;
    bool has_env = (sensor_manager_get_env(&env) == ESP_OK);

    float pitch = 0, roll = 0;
    motion_engine_get_angles(&pitch, &roll);
    float tilt_mag = sqrtf(pitch * pitch + roll * roll);

    /* ── 优先级从高到低评估 ── */

    /* 1. UV 极端 (>=8) → 恐慌 */
    if (has_env && env.uv.uv_index >= 8.0f) {
        return (sensor_rule_t){ "panic_05s_15", 90, "UV Extreme" };
    }

    /* 2. UV 高 (>=6) → 嘲讽 */
    if (has_env && env.uv.uv_index >= 6.0f) {
        return (sensor_rule_t){ "mock_05s", 80, "UV High" };
    }

    /* 3. 高温 (>=32°C) → 叹气 */
    if (has_env && env.aht20.temperature >= 32.0f) {
        return (sensor_rule_t){ "sigh_20s_40s", 70, "Hot" };
    }

    /* 4. 低温 (<=10°C) → 悲伤 */
    if (has_env && env.aht20.temperature <= 10.0f) {
        return (sensor_rule_t){ "sad_05s15s", 65, "Cold" };
    }

    /* 5. 高湿 (>=85%) → 哭 */
    if (has_env && env.aht20.humidity >= 85.0f) {
        return (sensor_rule_t){ "cry_10s_20s", 60, "Humid" };
    }

    /* 6. 大倾角 (>=20°) → 疑惑 */
    if (tilt_mag >= 20.0f) {
        return (sensor_rule_t){ "question_05s", 50, "Tilted" };
    }

    /* 7. 中等倾角 (>=12°) → 探究 */
    if (tilt_mag >= 12.0f) {
        return (sensor_rule_t){ "investigate_", 40, "Slight Tilt" };
    }

    /* 8. 低气压 (<=1000hPa, 可能下雨) → 思考 */
    if (has_env && env.bmp280.pressure > 0 && env.bmp280.pressure <= 100000.0f) {
        return (sensor_rule_t){ "ponder_05s", 30, "Low Pressure" };
    }

    /* 9. 一切正常 → 休闲微笑 */
    return (sensor_rule_t){ "leisure_05s_", 10, "Normal" };
}

/* ─────────────────────────────────────────────────────────
 *  公开 API
 * ───────────────────────────────────────────────────────── */

esp_err_t emote_engine_init(lv_obj_t *eaf_obj, lv_obj_t *name_label)
{
    if (!eaf_obj) return ESP_ERR_INVALID_ARG;

    s_state.eaf_obj     = eaf_obj;
    s_state.name_label  = name_label;
    s_state.current_idx = -1;
    s_state.last_eval_us = 0;
    s_state.manual_override_us = 0;
    s_state.manual_idx = 0;

    /* 初始播放默认表情 (休闲) */
    int idx = find_emote_index("leisure_05s_");
    if (idx < 0) idx = find_emote_index("smile_static");
    if (idx >= 0) {
        size_t sz = 0;
        const void *data = app_emote_assets_get_data((size_t)idx, &sz);
        if (data && sz > 0) {
            lv_eaf_set_src_data(s_state.eaf_obj, data, sz);
            lv_eaf_set_loop_count(s_state.eaf_obj, -1);
            lv_eaf_set_frame_delay(s_state.eaf_obj, EAF_FRAME_DELAY_MS);
            s_state.current_idx = idx;
            const char *name = app_emote_assets_get_name((size_t)idx);
            if (name && s_state.name_label)
                lv_label_set_text(s_state.name_label, emote_friendly_name(name));
        }
    }

    ESP_LOGI(TAG, "表情引擎初始化完成 (传感器驱动模式, 评估间隔 %ds)",
             EVAL_INTERVAL_MS / 1000);
    return ESP_OK;
}

void emote_engine_tick(void)
{
    int64_t now = esp_timer_get_time();

    /* 手动覆盖期间不自动评估 */
    if (s_state.manual_override_us > 0 && now < s_state.manual_override_us) {
        return;
    }
    s_state.manual_override_us = 0;

    /* 评估间隔未到 */
    if (now - s_state.last_eval_us < (int64_t)EVAL_INTERVAL_MS * 1000) {
        return;
    }
    s_state.last_eval_us = now;

    /* 评估传感器数据 */
    sensor_rule_t recommended = evaluate_sensors();

    int new_idx = find_emote_index(recommended.emote_name);
    if (new_idx < 0) {
        ESP_LOGW(TAG, "推荐表情 '%s' 未找到", recommended.emote_name);
        return;
    }

    /* 相同表情不切换 */
    if (new_idx == s_state.current_idx) return;

    ESP_LOGI(TAG, "传感器评估: %s → %s", recommended.reason, recommended.emote_name);
    play_emote(new_idx);
}

void emote_engine_manual_next(void)
{
    s_state.manual_idx++;
    if (s_state.manual_idx >= (int)MANUAL_COUNT) s_state.manual_idx = 0;

    const char *name = s_manual_anim_names[s_state.manual_idx];
    int idx = find_emote_index(name);
    if (idx < 0) {
        size_t count = app_emote_assets_get_count();
        if (count > 0) idx = s_state.manual_idx % (int)count;
    }

    if (idx >= 0) {
        play_emote(idx);
        /* 设置手动覆盖期 */
        s_state.manual_override_us = esp_timer_get_time() +
            (int64_t)MANUAL_OVERRIDE_MS * 1000;
        ESP_LOGI(TAG, "手动切换 → %s (10s内不自动评估)", name);
    }
}

const char *emote_engine_get_current_name(void)
{
    if (s_state.current_idx >= 0)
        return app_emote_assets_get_name((size_t)s_state.current_idx);
    return NULL;
}
