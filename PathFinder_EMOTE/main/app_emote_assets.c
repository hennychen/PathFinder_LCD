/**
 * @file app_emote_assets.c
 * @brief EAF 表情动画资源管理 — MMAP bin 解析实现
 *
 * 架构：
 *   emote 分区 (Flash) ──mmap_assets_new──> mmap_handle
 *                                            │
 *                          ┌─────────────────┤
 *                          ▼                 ▼
 *                    index.json         *.eaf 文件 (24个)
 *                          │
 *                    cJSON 解析
 *                          │
 *                    名称索引表 (name → asset_id)
 *
 * 数据访问零拷贝：mmap_assets_get_mem() 返回 Flash 映射地址
 */
#include "app_emote_assets.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mmap_assets.h"
#include "cJSON.h"

static const char *TAG = "emote_assets";

#define EMOTE_PARTITION_LABEL  "emote"
#define EMOTE_INDEX_JSON       "index.json"
#define EMOTE_MAX_ENTRIES      64
#define EMOTE_NAME_MAX         96

/* ── 内部索引条目 ── */
typedef struct {
    char     name[EMOTE_NAME_MAX];
    char     file[EMOTE_NAME_MAX];
    int      asset_id;   /* mmap 中的文件序号，-1 = 未找到 */
} emote_entry_t;

/* ── 全局状态 ── */
static mmap_assets_handle_t s_mmap    = NULL;
static emote_entry_t       *s_entries  = NULL;
static size_t               s_count    = 0;
static bool                 s_ready    = false;

/* ─────────────────────────────────────────────────────────
 *  index.json 解析
 * ───────────────────────────────────────────────────────── */

static esp_err_t parse_index_json(const char *json, size_t json_len)
{
    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (root == NULL || !cJSON_IsArray(root)) {
        ESP_LOGE(TAG, "index.json 解析失败");
        if (root) cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    s_count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        if (s_count >= EMOTE_MAX_ENTRIES) break;

        emote_entry_t *e = &s_entries[s_count];
        memset(e, 0, sizeof(*e));
        e->asset_id = -1;

        cJSON *j_name = cJSON_GetObjectItem(item, "name");
        cJSON *j_file = cJSON_GetObjectItem(item, "file");

        if (cJSON_IsString(j_name) && j_name->valuestring[0]) {
            strncpy(e->name, j_name->valuestring, EMOTE_NAME_MAX - 1);
        }
        if (cJSON_IsString(j_file) && j_file->valuestring[0]) {
            strncpy(e->file, j_file->valuestring, EMOTE_NAME_MAX - 1);
        }

        s_count++;
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "index.json 解析完成: %zu 条动画", s_count);
    return ESP_OK;
}

/* ─────────────────────────────────────────────────────────
 *  公开 API
 * ───────────────────────────────────────────────────────── */

esp_err_t app_emote_assets_init(void)
{
    if (s_ready) return ESP_OK;

    /* 分配索引表 */
    s_entries = calloc(EMOTE_MAX_ENTRIES, sizeof(emote_entry_t));
    if (s_entries == NULL) {
        ESP_LOGE(TAG, "索引表内存分配失败");
        return ESP_ERR_NO_MEM;
    }

    /* 挂载 MMAP 资源包 */
    /* max_files=0 + checksum=0 → 从 bin 头自动检测 */
    mmap_assets_config_t cfg = {
        .partition_label  = EMOTE_PARTITION_LABEL,
        .max_files        = 0,
        .checksum         = 0,
        .flags.mmap_enable   = true,
        .flags.full_check    = true,
    };

    esp_err_t ret = mmap_assets_new(&cfg, &s_mmap);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mmap 挂载失败 (分区 '%s'): %s",
                 EMOTE_PARTITION_LABEL, esp_err_to_name(ret));
        ESP_LOGE(TAG, "请确认已烧录 emote-assets.bin 到 emote 分区");
        free(s_entries);
        s_entries = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "MMAP 挂载成功, 文件数: %d",
             mmap_assets_get_stored_files(s_mmap));

    /* 在 mmap 中查找 index.json */
    int index_id = -1;
    int total = mmap_assets_get_stored_files(s_mmap);
    for (int i = 0; i < total; i++) {
        const char *fname = mmap_assets_get_name(s_mmap, i);
        if (fname && strcmp(fname, EMOTE_INDEX_JSON) == 0) {
            index_id = i;
            break;
        }
    }

    if (index_id < 0) {
        ESP_LOGE(TAG, "MMAP 包中未找到 index.json");
        mmap_assets_del(s_mmap);
        s_mmap = NULL;
        free(s_entries);
        s_entries = NULL;
        return ESP_ERR_NOT_FOUND;
    }

    /* 解析 index.json */
    const char *json_data = (const char *)mmap_assets_get_mem(s_mmap, index_id);
    size_t json_size = mmap_assets_get_size(s_mmap, index_id);

    ret = parse_index_json(json_data, json_size);
    if (ret != ESP_OK) {
        mmap_assets_del(s_mmap);
        s_mmap = NULL;
        free(s_entries);
        s_entries = NULL;
        return ret;
    }

    /* 为每个动画条目解析其 .eaf 文件在 mmap 中的 asset_id */
    for (size_t i = 0; i < s_count; i++) {
        emote_entry_t *e = &s_entries[i];
        for (int j = 0; j < total; j++) {
            const char *fname = mmap_assets_get_name(s_mmap, j);
            if (fname && strcmp(fname, e->file) == 0) {
                e->asset_id = j;
                break;
            }
        }
        if (e->asset_id < 0) {
            ESP_LOGW(TAG, "动画 '%s' 的文件 '%s' 未找到", e->name, e->file);
        }
    }

    s_ready = true;
    ESP_LOGI(TAG, "EAF 表情资源初始化完成 (%zu 个动画)", s_count);
    return ESP_OK;
}

bool app_emote_assets_is_ready(void)
{
    return s_ready;
}

size_t app_emote_assets_get_count(void)
{
    return s_ready ? s_count : 0;
}

const char *app_emote_assets_get_name(size_t index)
{
    if (!s_ready || index >= s_count) return NULL;
    return s_entries[index].name;
}

const void *app_emote_assets_get_data(size_t index, size_t *out_size)
{
    if (!s_ready || index >= s_count) return NULL;
    if (s_entries[index].asset_id < 0) return NULL;

    int aid = s_entries[index].asset_id;
    if (out_size) {
        *out_size = mmap_assets_get_size(s_mmap, aid);
    }
    return mmap_assets_get_mem(s_mmap, aid);
}

int app_emote_assets_find_by_name(const char *name)
{
    if (!s_ready || name == NULL) return -1;
    for (size_t i = 0; i < s_count; i++) {
        if (strcmp(s_entries[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

const void *app_emote_assets_get_data_by_name(const char *name, size_t *out_size)
{
    int idx = app_emote_assets_find_by_name(name);
    if (idx < 0) return NULL;
    return app_emote_assets_get_data((size_t)idx, out_size);
}
