/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
 *    All rights reserved.
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

// Unit test for the synchronous public surface of MultiImageOTAProcessorImpl:
// sub-processor registration and the default apply policy.
//
// The dispatch/skip routing itself runs on the Matter thread via ScheduleWork
// (HandleProcessBlock & friends), so it is not exercised here; that path is
// validated on hardware. <platform/ESP32/ESP32Utils.h> is shadowed by a host
// stub (mocks/platform/ESP32/ESP32Utils.h).

#include <pw_unit_test/framework.h>

#include "../MultiImageOTAProcessorImpl.h"
#include "../SubImageProcessor.h"

using namespace chip;

namespace {

// Minimal SubImageProcessor that records nothing — registration only needs an
// instance to associate with a tag.
class FakeSubProcessor : public SubImageProcessor
{
public:
    CHIP_ERROR Init(const SubImageHeader &) override { return CHIP_NO_ERROR; }
    bool IsInitialized() override { return false; }
    CHIP_ERROR IsReadyForOTA(DeviceState & state) override
    {
        state = DeviceState::kReady;
        return CHIP_NO_ERROR;
    }
    CHIP_ERROR Write(ByteSpan &) override { return CHIP_NO_ERROR; }
    void Abort(AbortContext &) override {}
};

} // namespace

TEST(TestMultiImageOTAProcessor, RegisterProcessorSucceeds)
{
    MultiImageOTAProcessorImpl proc;
    FakeSubProcessor sub;
    ImageProcessorEntry entry{ /*tag=*/0x300, &sub };
    EXPECT_EQ(proc.RegisterProcessor(entry), CHIP_NO_ERROR);
}

TEST(TestMultiImageOTAProcessor, RegisterRejectsNullProcessor)
{
    MultiImageOTAProcessorImpl proc;
    ImageProcessorEntry entry{ /*tag=*/0x300, nullptr };
    EXPECT_EQ(proc.RegisterProcessor(entry), CHIP_ERROR_INVALID_ARGUMENT);
}

TEST(TestMultiImageOTAProcessor, RegisterRejectsZeroTag)
{
    MultiImageOTAProcessorImpl proc;
    FakeSubProcessor sub;
    ImageProcessorEntry entry{ /*tag=*/0, &sub };
    EXPECT_EQ(proc.RegisterProcessor(entry), CHIP_ERROR_INVALID_ARGUMENT);
}

TEST(TestMultiImageOTAProcessor, RegisterRejectsDuplicateTag)
{
    MultiImageOTAProcessorImpl proc;
    FakeSubProcessor a, b;
    ImageProcessorEntry first{ /*tag=*/kAppImageProcessorTag, &a };
    ImageProcessorEntry second{ /*tag=*/kAppImageProcessorTag, &b };
    EXPECT_EQ(proc.RegisterProcessor(first), CHIP_NO_ERROR);
    EXPECT_EQ(proc.RegisterProcessor(second), CHIP_ERROR_DUPLICATE_KEY_ID);
}

TEST(TestMultiImageOTAProcessor, RegisterDistinctTagsSucceed)
{
    MultiImageOTAProcessorImpl proc;
    FakeSubProcessor app, coproc;
    ImageProcessorEntry e1{ kAppImageProcessorTag, &app };
    ImageProcessorEntry e2{ 0x400, &coproc };
    EXPECT_EQ(proc.RegisterProcessor(e1), CHIP_NO_ERROR);
    EXPECT_EQ(proc.RegisterProcessor(e2), CHIP_NO_ERROR);
}

TEST(TestMultiImageOTAProcessor, ShouldApplyUpdateDefaultsToProceed)
{
    MultiImageOTAProcessorImpl proc;
    EXPECT_TRUE(proc.ShouldApplyUpdate(Span<const SubImageResult>()));
}
