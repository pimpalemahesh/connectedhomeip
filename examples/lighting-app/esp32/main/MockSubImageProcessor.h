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
 */

#pragma once

#include <sdkconfig.h>

#ifdef CONFIG_ENABLE_MULTI_IMAGE_OTA

#include <platform/ESP32/multi-ota/SubImageProcessor.h>

/**
 * @brief Test-only sub-image processor that simulates a secondary device's OTA.
 *
 * It reports ready, consumes (and drops) the bytes the dispatcher routes to it, and logs each
 * lifecycle step exactly as a real device processor would. It lets the multi-image OTA flow —
 * routing, per-sub-image integrity, and the write/finish/apply fan-out across several active
 * processors — be exercised end-to-end without real secondary hardware. Owned by the example.
 */
class MockSubImageProcessor : public chip::SubImageProcessor
{
public:
    explicit MockSubImageProcessor(const char * name) : mName(name) {}

    CHIP_ERROR Init(const chip::SubImageHeader & entry) override;
    bool IsInitialized() override;
    virtual CHIP_ERROR IsReadyForOTA(chip::DeviceState & state) override;
    CHIP_ERROR Write(chip::ByteSpan & block) override;
    CHIP_ERROR Finish() override;
    void Abort(chip::AbortContext & context) override;
    CHIP_ERROR Apply() override;

private:
    const char * mName;
    bool mInitialized       = false;
    uint32_t mVersion       = 0;
    uint64_t mBytesReceived = 0;
};

/// Create and register the example's mock sub-image processors (image IDs 2 and 3) with the
/// multi-image dispatcher. Call once during app startup.
void RegisterMockSubImageProcessors();

#endif // CONFIG_ENABLE_MULTI_IMAGE_OTA
