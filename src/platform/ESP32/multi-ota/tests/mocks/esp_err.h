// Mock of ESP-IDF <esp_err.h> for host unit tests. NOT the real ESP-IDF header.
#pragma once

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_FINISHED 0x10E
#define ESP_ERR_INVALID_VERSION 0x10A

static inline const char * esp_err_to_name(esp_err_t e)
{
    (void) e;
    return "mock-esp-err";
}
