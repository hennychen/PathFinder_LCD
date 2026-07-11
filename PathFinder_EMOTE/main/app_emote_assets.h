/**
 * @file app_emote_assets.h
 * @brief EAF 表情动画资源管理 — 从 MMAP 打包 bin 中提取动画数据
 *
 * 工作流程：
 *   1. app_emote_assets_init()  — 挂载 emote 分区上的 MMAP bin
 *   2. 解析 index.json 构建名称索引表
 *   3. UI 通过名称或序号获取 EAF 原始数据指针 (零拷贝)
 *
 * 数据来源：ESP Emote GFX Packer NEXT 导出的 emote-assets.bin
 * 格式：MMAP 打包 (esp_mmap_assets) + index.json + 24 个 .eaf 动画文件
 */
#ifndef APP_EMOTE_ASSETS_H
#define APP_EMOTE_ASSETS_H

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief 挂载 emote 分区上的 MMAP 资源包并解析索引
 *
 * 在 app_main 初始化阶段调用一次。
 * 失败时后续 get 调用将返回空数据。
 *
 * @return ESP_OK 或错误码
 */
esp_err_t app_emote_assets_init(void);

/**
 * @brief 资源包是否已成功挂载
 */
bool app_emote_assets_is_ready(void);

/**
 * @brief 获取动画条目总数
 */
size_t app_emote_assets_get_count(void);

/**
 * @brief 按序号获取动画名称
 * @param index 序号 [0, count)
 * @return 名称字符串，越界返回 NULL
 */
const char *app_emote_assets_get_name(size_t index);

/**
 * @brief 按序号获取 EAF 原始数据 (零拷贝 mmap 指针)
 * @param index    序号
 * @param out_size 输出数据大小 (字节)
 * @return 数据指针，失败返回 NULL
 */
const void *app_emote_assets_get_data(size_t index, size_t *out_size);

/**
 * @brief 按名称查找序号
 * @param name 动画名称 (如 "smile_05s")
 * @return 序号，未找到返回 -1
 */
int app_emote_assets_find_by_name(const char *name);

/**
 * @brief 按名称获取 EAF 数据 (便捷封装)
 * @param name     动画名称
 * @param out_size 输出数据大小
 * @return 数据指针，失败返回 NULL
 */
const void *app_emote_assets_get_data_by_name(const char *name, size_t *out_size);

#endif /* APP_EMOTE_ASSETS_H */
