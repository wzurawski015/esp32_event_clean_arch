#include "ports/internal_temp_port.h"
#include "driver/temperature_sensor.h"
#include "esp_log.h"
#include <stdlib.h>
static const char* TAG = "INFRA_TEMP";
struct internal_temp_dev { temperature_sensor_handle_t handle; };
static port_err_t map_err(esp_err_t err) { return (err == ESP_OK) ? PORT_OK : PORT_FAIL; }
port_err_t internal_temp_create(const internal_temp_cfg_t* cfg, internal_temp_dev_t** out_dev) {
    if (!out_dev) return PORT_ERR_INVALID_ARG;
    struct internal_temp_dev* d = calloc(1, sizeof(struct internal_temp_dev));
    if (!d) return PORT_FAIL;
    temperature_sensor_config_t conf = TEMPERATURE_SENSOR_CONFIG_DEFAULT(cfg?cfg->min_c:-10, cfg?cfg->max_c:80);
    if(temperature_sensor_install(&conf, &d->handle) != ESP_OK) { free(d); return PORT_FAIL; }
    if(temperature_sensor_enable(d->handle) != ESP_OK) { temperature_sensor_uninstall(d->handle); free(d); return PORT_FAIL; }
    *out_dev = d; ESP_LOGI(TAG, "Initialized"); return PORT_OK;
}
port_err_t internal_temp_read(internal_temp_dev_t* dev, float* out) {
    if(!dev||!out) return PORT_ERR_INVALID_ARG;
    return map_err(temperature_sensor_get_celsius(dev->handle, out));
}
port_err_t internal_temp_delete(internal_temp_dev_t* dev) {
    if(!dev) return PORT_OK; temperature_sensor_disable(dev->handle);
    temperature_sensor_uninstall(dev->handle); free(dev); return PORT_OK;
}
