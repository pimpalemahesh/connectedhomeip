// Implementation of the esp_ota / esp_partition / esp_system / esp_delta_ota
// mocks (see esp_ota_mock.h).
#include "esp_ota_mock.h"

#include "esp_system.h"

#include <cstring>

EspOtaMock gEspOtaMock;

const esp_partition_t kMockUpdatePartition  = { "update", 0x200000, 0x180000 };
const esp_partition_t kMockRunningPartition = { "running", 0x020000, 0x180000 };

void EspOtaMock::Reset()
{
    std::vector<uint8_t> w;
    std::vector<uint8_t> pf;
    std::vector<uint8_t> out;
    *this = EspOtaMock();
}

namespace {
// Captured esp_delta_ota write callback + user data from the last init.
esp_delta_ota_write_cb_with_user_data_t sDeltaWriteCb = nullptr;
void * sDeltaUserData                                 = nullptr;
} // namespace

extern "C" {

const esp_partition_t * esp_ota_get_next_update_partition(const esp_partition_t *)
{
    return gEspOtaMock.getNextReturnsNull ? nullptr : &kMockUpdatePartition;
}

const esp_partition_t * esp_ota_get_running_partition(void)
{
    return &kMockRunningPartition;
}

esp_err_t esp_ota_begin(const esp_partition_t * partition, size_t image_size, esp_ota_handle_t * out_handle)
{
    gEspOtaMock.beginCalls++;
    gEspOtaMock.beginPartition = partition;
    gEspOtaMock.lastBeginSize  = image_size;
    if (gEspOtaMock.beginRet == ESP_OK)
    {
        *out_handle = 0x1234;
    }
    return gEspOtaMock.beginRet;
}

esp_err_t esp_ota_write(esp_ota_handle_t, const void * data, size_t size)
{
    gEspOtaMock.writeCalls++;
    if (gEspOtaMock.writeRet == ESP_OK)
    {
        const uint8_t * p = static_cast<const uint8_t *>(data);
        gEspOtaMock.written.insert(gEspOtaMock.written.end(), p, p + size);
    }
    return gEspOtaMock.writeRet;
}

esp_err_t esp_ota_end(esp_ota_handle_t)
{
    gEspOtaMock.endCalls++;
    return gEspOtaMock.endRet;
}

esp_err_t esp_ota_abort(esp_ota_handle_t)
{
    gEspOtaMock.abortCalls++;
    return gEspOtaMock.abortRet;
}

esp_err_t esp_ota_set_boot_partition(const esp_partition_t * partition)
{
    gEspOtaMock.setBootCalls++;
    gEspOtaMock.bootPartition = partition;
    return gEspOtaMock.setBootRet;
}

esp_err_t esp_partition_read(const esp_partition_t *, size_t, void * dst, size_t size)
{
    if (gEspOtaMock.readRet == ESP_OK)
    {
        memset(dst, 0, size);
    }
    return gEspOtaMock.readRet;
}

esp_err_t esp_partition_get_sha256(const esp_partition_t *, uint8_t * sha_256)
{
    if (gEspOtaMock.getSha256Ret == ESP_OK)
    {
        memcpy(sha_256, gEspOtaMock.runningSha, sizeof(gEspOtaMock.runningSha));
    }
    return gEspOtaMock.getSha256Ret;
}

void esp_restart(void)
{
    gEspOtaMock.restartCalls++;
    // Do NOT actually restart/exit the test process.
}

esp_delta_ota_handle_t esp_delta_ota_init(esp_delta_ota_cfg_t * cfg)
{
    gEspOtaMock.deltaInitCalls++;
    if (gEspOtaMock.deltaInitReturnsNull)
    {
        return nullptr;
    }
    sDeltaWriteCb   = cfg->write_cb_with_user_data;
    sDeltaUserData  = cfg->user_data;
    static int token = 0;
    return &token;
}

esp_err_t esp_delta_ota_feed_patch(esp_delta_ota_handle_t, const uint8_t * buf, int size)
{
    gEspOtaMock.deltaFeedCalls++;
    gEspOtaMock.deltaPatchFed.insert(gEspOtaMock.deltaPatchFed.end(), buf, buf + size);
    if (gEspOtaMock.deltaFeedRet != ESP_OK)
    {
        return gEspOtaMock.deltaFeedRet;
    }
    // Deliver the configured reconstructed output (if any) through the write cb.
    if (!gEspOtaMock.deltaOutput.empty() && sDeltaWriteCb != nullptr)
    {
        esp_err_t err = sDeltaWriteCb(gEspOtaMock.deltaOutput.data(), gEspOtaMock.deltaOutput.size(), sDeltaUserData);
        gEspOtaMock.deltaOutput.clear();
        return err;
    }
    return ESP_OK;
}

esp_err_t esp_delta_ota_finalize(esp_delta_ota_handle_t)
{
    gEspOtaMock.deltaFinalizeCalls++;
    return gEspOtaMock.deltaFinalizeRet;
}

esp_err_t esp_delta_ota_deinit(esp_delta_ota_handle_t)
{
    gEspOtaMock.deltaDeinitCalls++;
    sDeltaWriteCb  = nullptr;
    sDeltaUserData = nullptr;
    return gEspOtaMock.deltaDeinitRet;
}

} // extern "C"
