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

// Unit test for the base (non-delta, non-encrypted) AppImageProcessor path. The
// ESP-IDF esp_ota / esp_partition / esp_system APIs are replaced by host mocks
// (mocks/esp_ota_mock*). Built with CONFIG_OTA_AUTO_REBOOT_ON_APPLY defined so
// Apply() schedules the reboot timer (it never fires — the event loop is not run).

#include <pw_unit_test/framework.h>

#include "../AppImageProcessor.h"
#include "mocks/esp_ota_mock.h"

#include <lib/support/Span.h>
#include <platform/CHIPDeviceLayer.h>

using namespace chip;

namespace {

class TestAppImageProcessor : public ::testing::Test
{
public:
    static void SetUpTestSuite() { ASSERT_EQ(DeviceLayer::PlatformMgr().InitChipStack(), CHIP_NO_ERROR); }
    static void TearDownTestSuite() { DeviceLayer::PlatformMgr().Shutdown(); }

protected:
    void SetUp() override { gEspOtaMock.Reset(); }

    SubImageHeader entry{}; // contents are irrelevant to the base path
};

} // namespace

TEST_F(TestAppImageProcessor, WritesChunksThenFinishesAndApplies)
{
    AppImageProcessor p;
    EXPECT_EQ(p.Init(entry), CHIP_NO_ERROR);
    EXPECT_TRUE(p.IsInitialized());

    DeviceState state = DeviceState::kUnknown;
    EXPECT_EQ(p.IsReadyForOTA(state), CHIP_NO_ERROR);
    EXPECT_EQ(state, DeviceState::kReady);

    const uint8_t b1[] = { 1, 2, 3, 4 };
    ByteSpan s1(b1);
    EXPECT_EQ(p.Write(s1), CHIP_NO_ERROR);
    EXPECT_EQ(gEspOtaMock.beginCalls, 1);
    EXPECT_EQ(gEspOtaMock.beginPartition, &kMockUpdatePartition);
    EXPECT_EQ(gEspOtaMock.lastBeginSize, static_cast<size_t>(OTA_WITH_SEQUENTIAL_WRITES));

    const uint8_t b2[] = { 5, 6 };
    ByteSpan s2(b2);
    EXPECT_EQ(p.Write(s2), CHIP_NO_ERROR);
    EXPECT_EQ(gEspOtaMock.beginCalls, 1); // begin only on first chunk

    const uint8_t expected[] = { 1, 2, 3, 4, 5, 6 };
    ASSERT_EQ(gEspOtaMock.written.size(), sizeof(expected));
    EXPECT_EQ(memcmp(gEspOtaMock.written.data(), expected, sizeof(expected)), 0);

    EXPECT_EQ(p.Finish(), CHIP_NO_ERROR);
    EXPECT_EQ(gEspOtaMock.endCalls, 1);

    EXPECT_EQ(p.Apply(), CHIP_NO_ERROR);
    EXPECT_EQ(gEspOtaMock.setBootCalls, 1);
    EXPECT_EQ(gEspOtaMock.bootPartition, &kMockUpdatePartition);
    EXPECT_EQ(gEspOtaMock.restartCalls, 0); // timer scheduled, not fired
}

TEST_F(TestAppImageProcessor, RejectsOperationsBeforeInit)
{
    AppImageProcessor p;
    EXPECT_FALSE(p.IsInitialized());

    const uint8_t b[] = { 1 };
    ByteSpan s(b);
    EXPECT_EQ(p.Write(s), CHIP_ERROR_INCORRECT_STATE);
    EXPECT_EQ(p.Finish(), CHIP_ERROR_INCORRECT_STATE);

    DeviceState state = DeviceState::kUnknown;
    EXPECT_EQ(p.IsReadyForOTA(state), CHIP_ERROR_INCORRECT_STATE);
    EXPECT_EQ(gEspOtaMock.beginCalls, 0);
}

TEST_F(TestAppImageProcessor, WriteFailsWhenNoUpdatePartition)
{
    gEspOtaMock.getNextReturnsNull = true;
    AppImageProcessor p;
    ASSERT_EQ(p.Init(entry), CHIP_NO_ERROR);

    const uint8_t b[] = { 1, 2 };
    ByteSpan s(b);
    EXPECT_EQ(p.Write(s), CHIP_ERROR_INTERNAL);
    EXPECT_EQ(gEspOtaMock.beginCalls, 0);
}

TEST_F(TestAppImageProcessor, WriteSurfacesBeginFailure)
{
    gEspOtaMock.beginRet = ESP_FAIL;
    AppImageProcessor p;
    ASSERT_EQ(p.Init(entry), CHIP_NO_ERROR);

    const uint8_t b[] = { 1, 2 };
    ByteSpan s(b);
    EXPECT_EQ(p.Write(s), CHIP_ERROR_INTERNAL);
}

TEST_F(TestAppImageProcessor, WriteSurfacesWriteFailure)
{
    gEspOtaMock.writeRet = ESP_FAIL;
    AppImageProcessor p;
    ASSERT_EQ(p.Init(entry), CHIP_NO_ERROR);

    const uint8_t b[] = { 1, 2 };
    ByteSpan s(b);
    EXPECT_EQ(p.Write(s), CHIP_ERROR_WRITE_FAILED);
}

TEST_F(TestAppImageProcessor, FinishSurfacesEndFailure)
{
    gEspOtaMock.endRet = ESP_FAIL;
    AppImageProcessor p;
    ASSERT_EQ(p.Init(entry), CHIP_NO_ERROR);

    const uint8_t b[] = { 1, 2 };
    ByteSpan s(b);
    ASSERT_EQ(p.Write(s), CHIP_NO_ERROR);
    EXPECT_EQ(p.Finish(), CHIP_ERROR_INTERNAL);
}

TEST_F(TestAppImageProcessor, ApplyBeforeAnyWriteIsRejected)
{
    AppImageProcessor p;
    ASSERT_EQ(p.Init(entry), CHIP_NO_ERROR);
    // No Write() -> no partition opened.
    EXPECT_EQ(p.Apply(), CHIP_ERROR_INCORRECT_STATE);
    EXPECT_EQ(gEspOtaMock.setBootCalls, 0);
}

TEST_F(TestAppImageProcessor, ApplySurfacesSetBootFailure)
{
    gEspOtaMock.setBootRet = ESP_FAIL;
    AppImageProcessor p;
    ASSERT_EQ(p.Init(entry), CHIP_NO_ERROR);

    const uint8_t b[] = { 1, 2 };
    ByteSpan s(b);
    ASSERT_EQ(p.Write(s), CHIP_NO_ERROR);
    EXPECT_EQ(p.Apply(), CHIP_ERROR_INTERNAL);
}

TEST_F(TestAppImageProcessor, AbortTearsDownAndBlocksApply)
{
    AppImageProcessor p;
    ASSERT_EQ(p.Init(entry), CHIP_NO_ERROR);

    const uint8_t b[] = { 1, 2, 3 };
    ByteSpan s(b);
    ASSERT_EQ(p.Write(s), CHIP_NO_ERROR);

    AbortContext ctx{ AbortReason::kError, CHIP_ERROR_TIMEOUT };
    p.Abort(ctx);
    EXPECT_EQ(gEspOtaMock.abortCalls, 1);
    EXPECT_FALSE(p.IsInitialized());

    // After abort the processor is no longer initialized.
    EXPECT_EQ(p.Apply(), CHIP_ERROR_INCORRECT_STATE);
}
