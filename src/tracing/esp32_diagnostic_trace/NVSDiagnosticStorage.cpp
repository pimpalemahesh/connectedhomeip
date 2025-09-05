#include <nvs_flash.h>
#include <string>
#include <limits>
#include <tracing/esp32_diagnostic_trace/NVSDiagnosticStorage.h>

namespace chip {
namespace Tracing {
namespace Diagnostics {

NVSDiagnosticStorage::NVSDiagnosticStorage(const char * nvs_namespace)
{
    esp_err_t ret = nvs_flash_init();
    VerifyOrReturn(ret == ESP_OK, ChipLogError(DeviceLayer, "Failed to initialize NVS"));
    mNvsNamespace = nvs_namespace;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(mNvsNamespace, NVS_READWRITE, &handle);
    VerifyOrReturn(err == ESP_OK, ChipLogError(DeviceLayer, "Failed to open NVS namespace: %s", esp_err_to_name(err)));

    uint32_t startIdx = 0, lastIdx = 0;
    nvs_get_u32(handle, "start_idx", &startIdx);
    nvs_get_u32(handle, "last_idx", &lastIdx);

    mStartIndex = startIdx;
    mLastIndex  = lastIdx;

    nvs_close(handle);
    mInitialized = true;
}

NVSDiagnosticStorage::~NVSDiagnosticStorage()
{
    mNvsNamespace = nullptr;
    mInitialized  = false;
    mStartIndex   = 0;
    mLastIndex    = 0;
}

CHIP_ERROR NVSDiagnosticStorage::Store(const DiagnosticEntry & diagnostic)
{
    VerifyOrReturnError(mInitialized, CHIP_ERROR_INTERNAL, ChipLogError(DeviceLayer, "NVS not initialized"));
    VerifyOrReturnError(mNvsNamespace != nullptr, CHIP_ERROR_INTERNAL, ChipLogError(DeviceLayer, "NVS namespace is null"));

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(mNvsNamespace, NVS_READWRITE, &handle);
    VerifyOrReturnError(ret == ESP_OK, CHIP_ERROR_INTERNAL);

    ret = nvs_set_blob(handle, getIndexKey(mLastIndex), &diagnostic, sizeof(diagnostic));
    VerifyOrReturnError(ret == ESP_OK, CHIP_ERROR_INTERNAL,
                        ChipLogError(DeviceLayer, "Failed to write entry at index %lu: %s", mLastIndex, esp_err_to_name(ret)));

    uint32_t oldLastIndex = mLastIndex;
    mLastIndex++;

    if (mLastIndex == mStartIndex && oldLastIndex != mStartIndex)
    {
        mStartIndex++;
        if (mStartIndex == mMaxEntries)
        {
            mStartIndex = 0;
        }
    }

    if (mLastIndex == mMaxEntries)
    {
        mLastIndex = 0;
    }

    nvs_set_u32(handle, "start_idx", mStartIndex);
    nvs_set_u32(handle, "last_idx", mLastIndex);
    ret = nvs_commit(handle);
    VerifyOrReturnError(ret == ESP_OK, CHIP_ERROR_INTERNAL,
                        ChipLogError(DeviceLayer, "Failed to commit NVS changes: %s", esp_err_to_name(ret)));
    nvs_close(handle);

    return CHIP_NO_ERROR;
}

CHIP_ERROR NVSDiagnosticStorage::Retrieve(MutableByteSpan & payload, uint32_t & read_entries)
{
    VerifyOrReturnError(mInitialized, CHIP_ERROR_INTERNAL, ChipLogError(DeviceLayer, "NVS not initialized"));
    VerifyOrReturnError(mNvsNamespace != nullptr, CHIP_ERROR_INTERNAL, ChipLogError(DeviceLayer, "NVS namespace is null"));

    CHIP_ERROR err = CHIP_NO_ERROR;
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(mNvsNamespace, NVS_READONLY, &handle);
    VerifyOrReturnError(ret == ESP_OK, CHIP_ERROR_INTERNAL);

    chip::TLV::TLVWriter writer;
    writer.Init(payload.data(), payload.size());

    read_entries         = 0;
    size_t bytes_written = 0;

    for (uint32_t i = mStartIndex; i < mLastIndex; i++)
    {
        DiagnosticEntry entry;
        size_t required_size = sizeof(DiagnosticEntry);
        ret                  = nvs_get_blob(handle, getIndexKey(i), &entry, &required_size);
        if (ret != ESP_OK)
        {
            ChipLogError(DeviceLayer, "Failed to read entry at index %lu: %s", i, esp_err_to_name(ret));
            continue;
        }
        err = Encode(writer, entry);
        if (err == CHIP_ERROR_BUFFER_TOO_SMALL)
        {
            ChipLogProgress(DeviceLayer, "Buffer full after %lu entries", read_entries);
            break;
        }

        bytes_written = writer.GetLengthWritten();
        read_entries++;
    }

    ReturnErrorOnFailure(writer.Finalize());
    payload.reduce_size(bytes_written);

    nvs_close(handle);

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

    err = nvs_commit(handle);
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

    for (uint32_t i = mStartIndex; i < mStartIndex + entries && i < mLastIndex; i++)
    {
        nvs_erase_key(handle, getIndexKey(i));
    }

    err = nvs_commit(handle);
    VerifyOrReturnError(err == ESP_OK, CHIP_ERROR_INTERNAL);

    mStartIndex += entries;
    if (mStartIndex >= mMaxEntries)
    {
        mStartIndex = 0;
    }
    if (mStartIndex >= mLastIndex)
    {
        mStartIndex = 0;
        mLastIndex  = 0;
    }

    nvs_close(handle);
    return CHIP_NO_ERROR;
}

CHIP_ERROR NVSDiagnosticStorage::SetMaxEntries(uint32_t max_entries)
{
    VerifyOrReturnError(mInitialized, CHIP_ERROR_INTERNAL);
    mMaxEntries = max_entries;
    return CHIP_NO_ERROR;
}

const char * NVSDiagnosticStorage::getIndexKey(uint32_t index)
{
    mIndexKeyBuffer = std::to_string(index);
    return mIndexKeyBuffer.c_str();
}
} // namespace Diagnostics
} // namespace Tracing
} // namespace chip
