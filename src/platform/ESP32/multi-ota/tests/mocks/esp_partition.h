// Mock of ESP-IDF <esp_partition.h> for host unit tests. NOT the real header.
#pragma once

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    const char * label;
    uint32_t address;
    uint32_t size;
} esp_partition_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t esp_partition_read(const esp_partition_t * partition, size_t src_offset, void * dst, size_t size);
esp_err_t esp_partition_get_sha256(const esp_partition_t * partition, uint8_t * sha_256);

#ifdef __cplusplus
}
#endif
