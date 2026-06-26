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

#include "MockSubImageProcessor.h"

#ifdef CONFIG_ENABLE_MULTI_IMAGE_OTA

#include "ota/OTAHelper.h"
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/ESP32/multi-ota/MultiImageOTAProcessorImpl.h>

using namespace chip;

CHIP_ERROR MockSubImageProcessor::Init(const SubImageHeader & entry)
{
    mVersion       = entry.version;
    mBytesReceived = 0;
    mInitialized   = true;
    ChipLogProgress(SoftwareUpdate, "[mock:%s] begin OTA (target version %u, %u bytes)", mName,
                    static_cast<unsigned>(entry.version), static_cast<unsigned>(entry.length));
    return CHIP_NO_ERROR;
}

bool MockSubImageProcessor::IsInitialized()
{
    return mInitialized;
}

CHIP_ERROR MockSubImageProcessor::Write(ByteSpan & block)
{
    VerifyOrReturnError(mInitialized, CHIP_ERROR_INCORRECT_STATE);
    // A real device would forward these bytes to its transport (UART/SPI/BLE/...). Here we only
    // account for them and drop them.
    mBytesReceived += block.size();
    return CHIP_NO_ERROR;
}

CHIP_ERROR MockSubImageProcessor::Finish()
{
    VerifyOrReturnError(mInitialized, CHIP_ERROR_INCORRECT_STATE);
    ChipLogProgress(SoftwareUpdate, "[mock:%s] image received and verified (%llu bytes)", mName,
                    static_cast<unsigned long long>(mBytesReceived));
    return CHIP_NO_ERROR;
}

void MockSubImageProcessor::Abort(AbortContext & context)
{
    VerifyOrReturn(mInitialized);
    ChipLogProgress(SoftwareUpdate, "[mock:%s] OTA aborted (reason=%u) after %llu bytes", mName,
                    static_cast<unsigned>(context.reason), static_cast<unsigned long long>(mBytesReceived));
    mInitialized   = false;
    mBytesReceived = 0;
}

CHIP_ERROR MockSubImageProcessor::Apply()
{
    VerifyOrReturnError(mInitialized, CHIP_ERROR_INCORRECT_STATE);
    ChipLogProgress(SoftwareUpdate, "[mock:%s] applying image (version %u)", mName, static_cast<unsigned>(mVersion));
    return CHIP_NO_ERROR;
}

class MockSubImageProcessor2 : public MockSubImageProcessor
{
public:
    MockSubImageProcessor2(const char * name) : MockSubImageProcessor(name) { printf("SubImageProcessor: %s\n", name); }

    CHIP_ERROR IsReadyForOTA(DeviceState & state)
    {
        state = DeviceState::kReady;
        return CHIP_NO_ERROR;
    }
};

class MockSubImageProcessor3 : public MockSubImageProcessor
{
public:
    MockSubImageProcessor3(const char * name) : MockSubImageProcessor(name) { printf("SubImageProcessor: %s\n", name); }

    CHIP_ERROR IsReadyForOTA(DeviceState & state)
    {
        state = DeviceState::kNotReady;
        return CHIP_NO_ERROR;
    }
};

class MockSubImageProcessor4 : public MockSubImageProcessor
{
public:
    MockSubImageProcessor4(const char * name) : MockSubImageProcessor(name) { printf("SubImageProcessor: %s\n", name); }

    CHIP_ERROR IsReadyForOTA(DeviceState & state)
    {
        state = DeviceState::kAlreadyUpToDate;
        return CHIP_NO_ERROR;
    }
};

void RegisterMockSubImageProcessors()
{
    // Image IDs 2 and 3 (the application is ID 1). The instances and entry nodes are static so they
    // outlive every OTA cycle.
    static MockSubImageProcessor2 sMock2("device-ready");
    static MockSubImageProcessor3 sMock3("device-notready");
    static MockSubImageProcessor4 sMock4("device-alreadyuptodate");
    static ImageProcessorEntry sEntry2{ 0x00000002, &sMock2 };
    static ImageProcessorEntry sEntry3{ 0x00000003, &sMock3 };
    static ImageProcessorEntry sEntry4{ 0x00000004, &sMock4 };

    LogErrorOnFailure(OTAHelpers::Instance().RegisterSubImageProcessor(sEntry2));
    LogErrorOnFailure(OTAHelpers::Instance().RegisterSubImageProcessor(sEntry3));
    LogErrorOnFailure(OTAHelpers::Instance().RegisterSubImageProcessor(sEntry4));
    ChipLogProgress(SoftwareUpdate, "Registered mock sub-image processors (IDs 2, 3) for multi-image OTA testing");
}

#endif // CONFIG_ENABLE_MULTI_IMAGE_OTA
