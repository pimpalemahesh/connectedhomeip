// Test control block for the esp_ota / esp_partition / esp_system / esp_delta_ota
// mocks. Include from a test to configure behavior and inspect recorded calls.
#pragma once

#include "esp_delta_ota.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

#include <cstdint>
#include <vector>

struct EspOtaMock
{
    // ---- recorded calls ----
    int beginCalls    = 0;
    int writeCalls    = 0;
    int endCalls      = 0;
    int abortCalls    = 0;
    int setBootCalls  = 0;
    int restartCalls  = 0;

    std::vector<uint8_t> written;                  // all esp_ota_write payloads, concatenated
    size_t lastBeginSize                  = 0;     // image_size passed to esp_ota_begin
    const esp_partition_t * beginPartition = nullptr;
    const esp_partition_t * bootPartition  = nullptr;

    // ---- behavior knobs ----
    bool getNextReturnsNull = false;
    esp_err_t beginRet      = ESP_OK;
    esp_err_t writeRet      = ESP_OK;
    esp_err_t endRet        = ESP_OK;
    esp_err_t abortRet      = ESP_OK;
    esp_err_t setBootRet    = ESP_OK;

    // ---- partition mock (delta base image) ----
    uint8_t runningSha[32] = { 0 };
    esp_err_t getSha256Ret = ESP_OK;
    esp_err_t readRet      = ESP_OK;

    // ---- delta mock ----
    int deltaInitCalls         = 0;
    int deltaFeedCalls         = 0;
    int deltaFinalizeCalls     = 0;
    int deltaDeinitCalls       = 0;
    bool deltaInitReturnsNull  = false;
    esp_err_t deltaFeedRet     = ESP_OK;
    esp_err_t deltaFinalizeRet = ESP_OK;
    esp_err_t deltaDeinitRet   = ESP_OK;
    std::vector<uint8_t> deltaPatchFed; // patch bytes handed to esp_delta_ota_feed_patch
    // When non-empty, feed_patch delivers these "reconstructed" bytes to the
    // registered write callback (drives AppImageProcessor::WritePatchedOutput).
    std::vector<uint8_t> deltaOutput;

    void Reset();
};

extern EspOtaMock gEspOtaMock;

// Stable partition objects the mock returns.
extern const esp_partition_t kMockUpdatePartition;
extern const esp_partition_t kMockRunningPartition;
