#include "esp32wifi.h"
#include "string.h"

static const char *TAG = "wifi";

/** 事件回调函数
 * @param arg   用户传递的参数
 * @param event_base    事件类别
 * @param event_id      事件ID
 * @param event_data    事件携带的数据
 * @return 无
*/

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if(event_base == WIFI_EVENT)        //wifi事件
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:      //WIFI以STA模式启动后触发此事件
            esp_wifi_connect();         //启动WIFI连接
            break;
        case WIFI_EVENT_STA_CONNECTED:  //WIFI连上路由器后，触发此事件
            ESP_LOGI(TAG, "connected to AP");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:   //WIFI从路由器断开连接后触发此事件
            esp_wifi_connect();             //继续重连
            ESP_LOGI(TAG,"connect to the AP fail,retry now");
            break;
        default:
            break;
        }
    }
    if(event_base == IP_EVENT)                  //IP相关事件
    {
        switch(event_id)
        {
            case IP_EVENT_STA_GOT_IP:           //只有获取到路由器分配的IP，才认为是连上了路由器
                ESP_LOGI(TAG,"get ip address");
                break;
        }
    }
}

//WIFI STA初始化
esp_err_t wifi_sta_init(void)
{   
    esp_err_t ret;
    
    /* 初始化 NVS */
    ret = nvs_flash_init();
    if ( ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    //ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());  //用于初始化tcpip协议栈
    ESP_ERROR_CHECK(esp_event_loop_create_default());       //创建一个默认系统事件调度循环，之后可以注册回调函数来处理系统的一些事件
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();   //使用默认配置创建STA对象
    assert(sta_netif);    
    
    //初始化WIFI
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    //注册事件
    ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL) );

    //WIFI配置
    wifi_config_t wifi_config = 
    { 
        .sta.threshold.authmode = WIFI_AUTH_WPA2_PSK,   //加密方式     
        .sta.pmf_cfg.capable = true,
        .sta.pmf_cfg.required = false,
    }; 
    
    //WIFI的SSID  
    memset(&wifi_config.sta.ssid, 0, sizeof(wifi_config.sta.ssid));
    memcpy(wifi_config.sta.ssid, DEFAULT_WIFI_SSID, strlen(DEFAULT_WIFI_SSID));
    
    //WIFI密码
    memset(&wifi_config.sta.password, 0, sizeof(wifi_config.sta.password));
    memcpy(wifi_config.sta.password, DEFAULT_WIFI_PASSWORD, strlen(DEFAULT_WIFI_PASSWORD));
    
    //启动WIFI
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));         //设置工作模式为STA
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );   //设置wifi配置
    ESP_ERROR_CHECK(esp_wifi_start());                         //启动WIFI
    
    ESP_LOGI(TAG, "wifi_init_sta finished.");
    return ESP_OK;
}
 
/* 获取 Wi-Fi 信号强度 */
int get_wifi_signal_strength(void)
{
    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err == ESP_OK) {
        return ap_info.rssi;    //返回信号强度（RSSI）
    } else {
        ESP_LOGE(TAG, "Failed to get Wi-Fi signal info");
        return -100;        // 如果无法获取信号强度，返回默认值
    }
}
