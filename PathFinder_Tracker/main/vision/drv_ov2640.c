#include "drv_ov2640.h"
#include "tracker_config.h"
#include "esp_camera.h"
#include "esp_log.h"

static const char *TAG = "drv_ov2640";

/* Track the currently-held frame buffer so return_frame() can release it. */
static camera_fb_t *s_last_fb = NULL;

esp_err_t drv_ov2640_init(void)
{
    camera_config_t cam_cfg = {
        .pin_pwdn     = CAM_PIN_PWDN,
        .pin_reset    = CAM_PIN_RESET,
        .pin_xclk     = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,

        .pin_d7  = CAM_PIN_D7,
        .pin_d6  = CAM_PIN_D6,
        .pin_d5  = CAM_PIN_D5,
        .pin_d4  = CAM_PIN_D4,
        .pin_d3  = CAM_PIN_D3,
        .pin_d2  = CAM_PIN_D2,
        .pin_d1  = CAM_PIN_D1,
        .pin_d0  = CAM_PIN_D0,

        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href  = CAM_PIN_HREF,
        .pin_pclk  = CAM_PIN_PCLK,

        .xclk_freq_hz = CAM_XCLK_FREQ,

        .ledc_timer   = LEDC_TIMER_1,
        .ledc_channel = LEDC_CHANNEL_2,

        .pixel_format  = PIXFORMAT_RGB565,
        .frame_size    = FRAMESIZE_240X240,

        .fb_location = CAMERA_FB_IN_PSRAM,
        .fb_count    = 2,

        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
    };

    esp_err_t ret = esp_camera_init(&cam_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "OV2640 initialised: %dx%d RGB565, XCLK %d Hz",
             FACE_IMG_WIDTH, FACE_IMG_HEIGHT, CAM_XCLK_FREQ);
    return ESP_OK;
}

esp_err_t drv_ov2640_capture(camera_frame_t *frame)
{
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* If a previous frame was never returned, release it now to avoid
       leaking the single outstanding buffer. */
    if (s_last_fb != NULL) {
        esp_camera_fb_return(s_last_fb);
        s_last_fb = NULL;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == NULL) {
        ESP_LOGW(TAG, "esp_camera_fb_get returned NULL");
        return ESP_FAIL;
    }

    frame->data   = fb->buf;
    frame->width  = fb->width;
    frame->height = fb->height;
    frame->format = fb->format;

    s_last_fb = fb;
    return ESP_OK;
}

void drv_ov2640_return_frame(camera_frame_t *frame)
{
    (void)frame;  /* frame pointer is informational only */
    if (s_last_fb != NULL) {
        esp_camera_fb_return(s_last_fb);
        s_last_fb = NULL;
    }
}

esp_err_t drv_ov2640_deinit(void)
{
    if (s_last_fb != NULL) {
        esp_camera_fb_return(s_last_fb);
        s_last_fb = NULL;
    }
    return esp_camera_deinit();
}
