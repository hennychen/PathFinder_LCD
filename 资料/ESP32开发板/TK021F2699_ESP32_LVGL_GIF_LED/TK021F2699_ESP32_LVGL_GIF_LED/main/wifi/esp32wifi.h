/**
 * @file esp32wifi.h
 * This file exists only to be compatible with Arduino's library structure
 */

#ifndef ESP32WIFI_H
#define ESP32WIFI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_eap_client.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"
#include "esp_mac.h"

#define DEFAULT_WIFI_SSID           "TK499_2"
#define DEFAULT_WIFI_PASSWORD       "HJR112169"

esp_err_t wifi_sta_init(void);

/* 获取 Wi-Fi 信号强度 */
int get_wifi_signal_strength(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*ESP32WIFI_H*/
