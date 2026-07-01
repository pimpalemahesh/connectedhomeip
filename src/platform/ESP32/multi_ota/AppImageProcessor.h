/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
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
 *
 */

#pragma once
#include "SubImageProcessor.h"
#include <esp_ota_ops.h>
#include <lib/support/Span.h>

namespace chip {

/**
 * @brief Application-firmware sub-processor: writes the app image to the inactive OTA partition and
 *        activates it on apply.
 */
class AppImageProcessor : public SubImageProcessor
{
public:
    CHIP_ERROR Init(const SubImageHeader & entry) override;
    bool IsInitialized() override;
    CHIP_ERROR IsReadyForOTA(DeviceState & state) override;
    CHIP_ERROR Write(ByteSpan & block) override;
    CHIP_ERROR Finish() override;
    void Abort(AbortContext & context) override;
    CHIP_ERROR Apply() override;

private:
    const esp_partition_t * mPartition = nullptr;
    esp_ota_handle_t mOtaHandle        = 0;
    bool mInitialized                  = false;
};

} // namespace chip
