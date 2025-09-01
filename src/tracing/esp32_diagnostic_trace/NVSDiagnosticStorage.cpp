#include <nvs_flash.h>
#include <tracing/esp32_diagnostic_trace/NVSDiagnosticStorage.h>
#include <string>

namespace chip {
namespace Tracing {
namespace Diagnostics {

CHIP_ERROR NVSDiagnosticStorage::Init(const char * nvs_namespace)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    esp_err_t ret = nvs_flash_init();
    VerifyOrReturnError(ret == ESP_OK, CHIP_ERROR_INTERNAL, ChipLogError(DeviceLayer, "Failed to initialize NVS: %s", esp_err_to_name(ret)) );

    mNvsNamespace = nvs_namespace;
    mInitialized = true;
    mStartIndex = 0;
    mLastIndex = 0;
    return err;
}

CHIP_ERROR NVSDiagnosticStorage::Deinit()
{
    mNvsNamespace = nullptr;
    mInitialized = false;
    mStartIndex = 0;
    mLastIndex = 0;
    return CHIP_NO_ERROR;
}

CHIP_ERROR NVSDiagnosticStorage::Store(const DiagnosticEntry & diagnostic)
{
    VerifyOrReturnError(mInitialized, CHIP_ERROR_INTERNAL);

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(mNvsNamespace, NVS_READWRITE, &handle);
    VerifyOrReturnError(ret == ESP_OK, CHIP_ERROR_INTERNAL);

    // Write struct as blob
    ret = nvs_set_blob(handle, getIndexKey(mLastIndex), &diagnostic, sizeof(diagnostic));
    VerifyOrReturnError(ret == ESP_OK, CHIP_ERROR_INTERNAL);

    mLastIndex++;
    if (mLastIndex == INT_MAX)
    {
        mStartIndex++;
        mLastIndex = 0;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR NVSDiagnosticStorage::Retrieve(MutableByteSpan & payload, uint32_t & read_entries)
{
    VerifyOrReturnError(mInitialized, CHIP_ERROR_INTERNAL);

    CHIP_ERROR err = CHIP_NO_ERROR;
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(mNvsNamespace, NVS_READONLY, &handle);
    VerifyOrReturnError(ret == ESP_OK, CHIP_ERROR_INTERNAL);

    chip::TLV::TLVWriter writer;
    writer.Init(payload.data(), payload.size());

    read_entries = 0;
    size_t bytes_written = 0;

    for (uint32_t i = mStartIndex; i < mLastIndex; i++)
    {
        DiagnosticEntry entry;
        size_t required_size = sizeof(DiagnosticEntry);
        ret = nvs_get_blob(handle, getIndexKey(i), &entry, &required_size);
        if (ret != ESP_OK) {
            ChipLogError(DeviceLayer, "Failed to read entry at index %lu: %s", i, esp_err_to_name(ret));
            continue; // skip corrupted
        }

        err = Encode(writer, entry);
        if (err == CHIP_ERROR_BUFFER_TOO_SMALL) {
            ChipLogProgress(DeviceLayer, "Buffer full after %lu entries", read_entries);
            break;
        }

        bytes_written = writer.GetLengthWritten();
        read_entries++;
    }

    ReturnErrorOnFailure(writer.Finalize());
    payload.reduce_size(bytes_written);

    return CHIP_NO_ERROR;
}

bool NVSDiagnosticStorage::IsBufferEmpty()
{
    return mStartIndex == mLastIndex;
}

uint32_t NVSDiagnosticStorage::GetDataSize()
{
    return (mLastIndex - mStartIndex) * sizeof(DiagnosticEntry);
}

CHIP_ERROR NVSDiagnosticStorage::ClearBuffer()
{
    VerifyOrReturnError(mInitialized, CHIP_ERROR_INTERNAL);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(mNvsNamespace, NVS_READWRITE, &handle);
    VerifyOrReturnError(err == ESP_OK, CHIP_ERROR_INTERNAL);

    err = nvs_erase_all(handle);
    VerifyOrReturnError(err == ESP_OK, CHIP_ERROR_INTERNAL);

    nvs_close(handle);
    return CHIP_NO_ERROR;
}

CHIP_ERROR NVSDiagnosticStorage::ClearBuffer(uint32_t entries)
{
    VerifyOrReturnError(mInitialized, CHIP_ERROR_INTERNAL);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(mNvsNamespace, NVS_READWRITE, &handle);
    VerifyOrReturnError(err == ESP_OK, CHIP_ERROR_INTERNAL);

    for (uint32_t i = mStartIndex; i < mLastIndex; i++)
    {
        nvs_erase_key(handle, getIndexKey(i));
        if (i == mStartIndex)
        {
            mStartIndex++;
        }
    }

    if (mStartIndex == mLastIndex)
    {
        mStartIndex = 0;
        mLastIndex = 0;
    }

    return CHIP_NO_ERROR;
}

const char * NVSDiagnosticStorage::getIndexKey(uint32_t index)
{
    return std::to_string(index).c_str();
}
} // namespace Diagnostics
} // namespace Tracing
} // namespace chip