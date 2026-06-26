// Mock of ESP-IDF <esp_app_format.h> for host unit tests. NOT the real header.
//
// Only the leading fields AppImageProcessor reads (magic, ..., chip_id) need to
// be laid out; the struct size must match what the code accumulates before the
// chip_id check, so keep the field order/sizes in sync with esp-idf.
#pragma once

#include <stdint.h>

typedef struct
{
    uint8_t magic;
    uint8_t segment_count;
    uint8_t spi_mode;
    uint8_t spi_speed : 4;
    uint8_t spi_size : 4;
    uint32_t entry_addr;
    uint8_t wp_pin;
    uint8_t spi_pin_drv[3];
    uint16_t chip_id;
    uint8_t min_chip_rev;
    uint8_t min_chip_rev_full[2];
    uint8_t max_chip_rev_full[2];
    uint8_t reserved[4];
    uint8_t hash_appended;
} __attribute__((packed)) esp_image_header_t;

// Matches CONFIG_IDF_FIRMWARE_CHIP_ID for esp32 in real builds; value is
// arbitrary for the host test as long as the test uses the same constant.
#ifndef CONFIG_IDF_FIRMWARE_CHIP_ID
#define CONFIG_IDF_FIRMWARE_CHIP_ID 0x0000
#endif
