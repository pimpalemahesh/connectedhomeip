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

#pragma once

#include "SubImageProcessor.h"

#include <crypto/CHIPCryptoPAL.h>

#include "esp_ota_ops.h"

namespace chip {
namespace ESP32 {
namespace MultiOTA {

// Built-in sub-processor for the primary ESP32 application firmware slot.
// Register this at application initialisation with the platform-defined app image ID.
//
// Image ID assignment (range 1–15, platform-defined):
//   ID 1 — primary ESP32 application firmware
class AppImageProcessor : public SubImageProcessor
{
public:
    static constexpr uint8_t kImageId = 1; // platform-defined; stable ABI

    // SubImageProcessor interface
    DeviceReadinessState IsReadyForOTA(uint32_t targetVersion) override;
    CHIP_ERROR           Init(const SubImageHeader & entry) override;
    CHIP_ERROR           Write(ByteSpan block) override;
    CHIP_ERROR           Apply() override;
    void                 Abort() override;

private:
    CHIP_ERROR VerifyDigest();

    const esp_partition_t *   mPartition     = nullptr;
    esp_ota_handle_t          mOtaHandle     = 0;
    uint32_t                  mTotalLength   = 0;
    uint32_t                  mBytesReceived = 0;
    uint8_t                   mExpectedSha256[Crypto::kSHA256_Hash_Length]{};
    Crypto::Hash_SHA256_stream mSha256;
    bool                      mOtaBegun = false;
};

} // namespace MultiOTA
} // namespace ESP32
} // namespace chip
