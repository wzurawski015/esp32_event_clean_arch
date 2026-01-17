#include "ports/led_strip_port.h"
#include "led_strip.h" // Biblioteka Espressif
#include "esp_log.h"
#include <stdlib.h>

static const char* TAG = "LED_INFRA";

struct led_strip_dev {
    led_strip_handle_t handle;
};

port_err_t led_port_create(const led_strip_cfg_t* cfg, led_strip_dev_t** out_dev)
{
    if (!cfg || !out_dev) return PORT_ERR_INVALID_ARG;

    struct led_strip_dev* d = calloc(1, sizeof(struct led_strip_dev));
    if (!d) return PORT_FAIL;

    led_strip_config_t strip_config = {
        .strip_gpio_num = cfg->gpio_num,
        .max_leds = cfg->max_leds,
        .led_pixel_format = (cfg->type == LED_STRIP_SK6812) ? LED_PIXEL_FORMAT_GRBW : LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812, 
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT, 
        .resolution_hz = 10 * 1000 * 1000, 
        .flags.with_dma = cfg->use_dma,
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &d->handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(err));
        free(d);
        return PORT_FAIL;
    }

    *out_dev = d;
    ESP_LOGI(TAG, "LED Strip initialized on GPIO%d, len=%d", cfg->gpio_num, cfg->max_leds);
    return PORT_OK;
}

port_err_t led_port_set_pixel(led_strip_dev_t* dev, int index, uint8_t r, uint8_t g, uint8_t b)
{
    if (!dev) return PORT_ERR_INVALID_ARG;
    return (led_strip_set_pixel(dev->handle, index, r, g, b) == ESP_OK) ? PORT_OK : PORT_FAIL;
}

port_err_t led_port_set_pixel_rgbw(led_strip_dev_t* dev, int index, uint8_t r, uint8_t g, uint8_t b, uint8_t w)
{
    if (!dev) return PORT_ERR_INVALID_ARG;
    // Niektóre wersje led_strip nie mają _rgbw, używamy surowego, jeśli trzeba
    // Ale w 2.5.3 powinno być. Jeśli nie - mapuj na set_pixel
    return (led_strip_set_pixel_rgbw(dev->handle, index, r, g, b, w) == ESP_OK) ? PORT_OK : PORT_FAIL;
}

port_err_t led_port_clear(led_strip_dev_t* dev)
{
    if (!dev) return PORT_ERR_INVALID_ARG;
    return (led_strip_clear(dev->handle) == ESP_OK) ? PORT_OK : PORT_FAIL;
}

port_err_t led_port_refresh(led_strip_dev_t* dev, uint32_t timeout_ms)
{
    if (!dev) return PORT_ERR_INVALID_ARG;
    return (led_strip_refresh(dev->handle) == ESP_OK) ? PORT_OK : PORT_FAIL;
}

port_err_t led_port_delete(led_strip_dev_t* dev)
{
    if (!dev) return PORT_OK;
    led_strip_del(dev->handle);
    free(dev);
    return PORT_OK;
}
