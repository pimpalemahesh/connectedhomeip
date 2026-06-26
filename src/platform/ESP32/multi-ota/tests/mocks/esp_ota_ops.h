// Mock of ESP-IDF <esp_ota_ops.h> for host unit tests. NOT the real header.
#pragma once

#include "esp_err.h"
#include "esp_partition.h"

#include <stddef.h>
#include <stdint.h>

typedef uint32_t esp_ota_handle_t;

#define OTA_SIZE_UNKNOWN 0xffffffff
#define OTA_WITH_SEQUENTIAL_WRITES 0xfffffffe

#ifdef __cplusplus
extern "C" {
#endif

const esp_partition_t * esp_ota_get_next_update_partition(const esp_partition_t * start_from);
const esp_partition_t * esp_ota_get_running_partition(void);
esp_err_t esp_ota_begin(const esp_partition_t * partition, size_t image_size, esp_ota_handle_t * out_handle);
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void * data, size_t size);
esp_err_t esp_ota_end(esp_ota_handle_t handle);
esp_err_t esp_ota_abort(esp_ota_handle_t handle);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t * partition);

#ifdef __cplusplus
}
#endif
