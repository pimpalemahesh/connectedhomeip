/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "AppImageProcessor.h"

#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>

#include "esp_log.h"
#include "esp_ota_ops.h"

#define TAG "AppImageProcessor"

namespace chip {
namespace ESP32 {
namespace MultiOTA {

DeviceReadinessState AppImageProcessor::IsReadyForOTA(uint32_t targetVersion)
{
    // Verify that the next OTA partition is available before committing to the session.
    const esp_partition_t * partition = esp_ota_get_next_update_partition(nullptr);
    if (partition == nullptr)
    {
        ChipLogError(SoftwareUpdate, "AppImageProcessor: no OTA partition available");
        return DeviceReadinessState::kNotReady;
    }

    // The OTA Requestor already decided to update based on softwareVersion; always ready.
    return DeviceReadinessState::kReady;
}

CHIP_ERROR AppImageProcessor::Init(const SubImageHeader & entry)
{
    mPartition = esp_ota_get_next_update_partition(nullptr);
    if (mPartition == nullptr)
    {
        ChipLogError(SoftwareUpdate, "AppImageProcessor::Init: OTA partition not found");
        return CHIP_ERROR_INTERNAL;
    }

    esp_err_t err = esp_ota_begin(mPartition, OTA_WITH_SEQUENTIAL_WRITES, &mOtaHandle);
    if (err != ESP_OK)
    {
        ChipLogError(SoftwareUpdate, "esp_ota_begin failed: %s", esp_err_to_name(err));
        return CHIP_ERROR_INTERNAL;
    }

    mTotalLength   = entry.length;
    mBytesReceived = 0;
    memcpy(mExpectedSha256, entry.sha256, Crypto::kSHA256_Hash_Length);
    mOtaBegun = true;

    ReturnErrorOnFailure(mSha256.Begin());

    ChipLogProgress(SoftwareUpdate, "AppImageProcessor: begin write to partition %s (%" PRIu32 " bytes)", mPartition->label,
                    mTotalLength);
    return CHIP_NO_ERROR;
}

CHIP_ERROR AppImageProcessor::Write(ByteSpan block)
{
    VerifyOrReturnError(mOtaBegun, CHIP_ERROR_INCORRECT_STATE);

    esp_err_t err = esp_ota_write(mOtaHandle, block.data(), block.size());
    if (err != ESP_OK)
    {
        ChipLogError(SoftwareUpdate, "esp_ota_write failed: %s", esp_err_to_name(err));
        return CHIP_ERROR_WRITE_FAILED;
    }

    ReturnErrorOnFailure(mSha256.AddData(block));
    mBytesReceived += static_cast<uint32_t>(block.size());

    if (mBytesReceived == mTotalLength)
    {
        // All bytes received — verify the SHA-256 digest from SubImageHeader.
        ReturnErrorOnFailure(VerifyDigest());

        // Validate the ESP image internally (checks magic, checksum, etc.).
        esp_err_t endErr = esp_ota_end(mOtaHandle);
        mOtaBegun        = false;
        if (endErr != ESP_OK)
        {
            if (endErr == ESP_ERR_OTA_VALIDATE_FAILED)
                ChipLogError(SoftwareUpdate, "AppImageProcessor: ESP image validation failed");
            else
                ChipLogError(SoftwareUpdate, "esp_ota_end failed: %s", esp_err_to_name(endErr));
            return CHIP_ERROR_INTEGRITY_CHECK_FAILED;
        }
        ChipLogProgress(SoftwareUpdate, "AppImageProcessor: image written and validated at 0x%" PRIx32, mPartition->address);
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR AppImageProcessor::Apply()
{
    VerifyOrReturnError(mPartition != nullptr, CHIP_ERROR_INCORRECT_STATE);

    esp_err_t err = esp_ota_set_boot_partition(mPartition);
    if (err != ESP_OK)
    {
        ChipLogError(SoftwareUpdate, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        return CHIP_ERROR_INTERNAL;
    }

    ChipLogProgress(SoftwareUpdate, "AppImageProcessor: boot partition set to 0x%" PRIx32, mPartition->address);
    return CHIP_NO_ERROR;
}

void AppImageProcessor::Abort()
{
    if (mOtaBegun)
    {
        esp_ota_abort(mOtaHandle);
        mOtaBegun = false;
        ChipLogProgress(SoftwareUpdate, "AppImageProcessor: OTA aborted");
    }
    mPartition     = nullptr;
    mBytesReceived = 0;
}

CHIP_ERROR AppImageProcessor::VerifyDigest()
{
    uint8_t digest[Crypto::kSHA256_Hash_Length];
    MutableByteSpan digestSpan(digest);
    ReturnErrorOnFailure(mSha256.Finish(digestSpan));

    if (memcmp(digest, mExpectedSha256, Crypto::kSHA256_Hash_Length) != 0)
    {
        ChipLogError(SoftwareUpdate, "AppImageProcessor: SHA-256 mismatch — image corrupted");
        return CHIP_ERROR_INTEGRITY_CHECK_FAILED;
    }

    ChipLogProgress(SoftwareUpdate, "AppImageProcessor: SHA-256 verified");
    return CHIP_NO_ERROR;
}

} // namespace MultiOTA
} // namespace ESP32
} // namespace chip
