// Test control block for the esp_encrypted_img mock. Include from the test to
// configure behavior and inspect call counts.
#pragma once

#include "esp_encrypted_img.h"

struct EspEncMock
{
    int startCalls  = 0;
    int dataCalls   = 0;
    int endCalls    = 0;
    int abortCalls  = 0;
    int allocCount  = 0; // output buffers handed to the caller (caller owns/frees)

    // Behavior knobs:
    bool startReturnsNull = false;          // esp_encrypted_img_decrypt_start -> nullptr
    bool produceOutput    = true;           // decrypt_data emits a fresh output buffer
    int byteXor           = 0x5A;           // transform applied to each output byte
    esp_err_t dataReturn  = ESP_OK;         // return code of decrypt_data
    esp_err_t endReturn   = ESP_OK;         // return code of decrypt_end

    void Reset() { *this = EspEncMock(); }
};

extern EspEncMock gEspEncMock;
