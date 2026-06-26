// Mock of ESP-IDF <esp_delta_ota.h> for host unit tests. NOT the real header.
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void * esp_delta_ota_handle_t;

typedef esp_err_t (*esp_delta_ota_read_cb_with_user_data_t)(uint8_t * buf, size_t size, int src_offset, void * user_data);
typedef esp_err_t (*esp_delta_ota_write_cb_with_user_data_t)(const uint8_t * buf, size_t size, void * user_data);

typedef struct
{
    esp_delta_ota_read_cb_with_user_data_t read_cb_with_user_data;
    esp_delta_ota_write_cb_with_user_data_t write_cb_with_user_data;
    void * user_data;
} esp_delta_ota_cfg_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_delta_ota_handle_t esp_delta_ota_init(esp_delta_ota_cfg_t * cfg);
esp_err_t esp_delta_ota_feed_patch(esp_delta_ota_handle_t handle, const uint8_t * buf, int size);
esp_err_t esp_delta_ota_finalize(esp_delta_ota_handle_t handle);
esp_err_t esp_delta_ota_deinit(esp_delta_ota_handle_t handle);

#ifdef __cplusplus
}
#endif
