/**
 * @file drv_aht20.c
 * @brief AHT20 温湿度传感器 I2C 驱动实现
 *
 * AHT20 协议：
 *   1. 初始化：发 0xBE 0x08 0x00，等 10ms
 *   2. 触发测量：发 0xAC 0x33 0x00，等 80ms
 *   3. 读取 7 字节：[status, rh_msb, rh_lsb, rh_t_mix, t_msb, t_lsb, crc]
 *   4. 湿度 = (raw_h / 2^20) × 100
 *      温度 = (raw_t / 2^20) × 200 − 50
 */
#include "drv_aht20.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "drv_aht20";

#define AHT20_ADDR_DEFAULT   0x38

static i2c_master_dev_handle_t s_dev = NULL;

esp_err_t drv_aht20_init(i2c_master_bus_handle_t bus, uint8_t addr)
{
    if (bus == NULL) return ESP_ERR_INVALID_ARG;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = (addr == 0) ? AHT20_ADDR_DEFAULT : addr,
        .scl_speed_hz    = 400000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C 设备添加失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 复位 + 初始化命令 */
    uint8_t init_cmd[3] = {0xBE, 0x08, 0x00};
    ret = i2c_master_transmit(s_dev, init_cmd, sizeof(init_cmd), 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化命令发送失败: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "AHT20 初始化完成 @0x%02X", dev_cfg.device_address);
    return ESP_OK;
}

esp_err_t drv_aht20_read(aht20_data_t *out)
{
    if (s_dev == NULL || out == NULL) return ESP_ERR_INVALID_STATE;

    /* 触发测量 */
    uint8_t trig[3] = {0xAC, 0x33, 0x00};
    esp_err_t ret = i2c_master_transmit(s_dev, trig, sizeof(trig), 100);
    if (ret != ESP_OK) return ret;

    /* 等待测量完成 (最多 80ms) */
    vTaskDelay(pdMS_TO_TICKS(80));

    /* 读取 7 字节数据 */
    uint8_t data[7];
    ret = i2c_master_receive(s_dev, data, sizeof(data), 100);
    if (ret != ESP_OK) return ret;

    /* 检查 BUSY 位 (bit7 of status byte) */
    if (data[0] & 0x80) {
        ESP_LOGW(TAG, "AHT20 仍在测量中");
        return ESP_ERR_INVALID_STATE;
    }

    /* 湿度 20-bit: data[1]<<12 | data[2]<<4 | data[3]>>4 */
    uint32_t raw_h = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | (data[3] >> 4);
    /* 温度 20-bit: (data[3]&0x0F)<<16 | data[4]<<8 | data[5] */
    uint32_t raw_t = (((uint32_t)data[3] & 0x0F) << 16) | ((uint32_t)data[4] << 8) | data[5];

    out->humidity    = (raw_h / 1048576.0f) * 100.0f;
    out->temperature = (raw_t / 1048576.0f) * 200.0f - 50.0f;

    return ESP_OK;
}
