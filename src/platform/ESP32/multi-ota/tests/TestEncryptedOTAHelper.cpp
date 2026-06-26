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

// Unit test for EncryptedOTAHelper (EncryptedOTAHelper.cpp). The ESP-IDF
// esp_encrypted_img API is replaced by a host mock (mocks/esp_encrypted_img*).
//
// Tip: build with `is_asan=true` in your gn args to let AddressSanitizer verify
// the decrypted-buffer ownership (free-previous-before-next, free-on-end/abort).

#include <pw_unit_test/framework.h>

#include "../EncryptedOTAHelper.h"
#include "mocks/esp_encrypted_img_mock.h"

#include <lib/support/Span.h>

#include <string.h>

using namespace chip;

namespace {

// Exposes the protected mechanics so the test can drive them directly.
class Harness : public EncryptedOTAHelper
{
public:
    using EncryptedOTAHelper::Decrypt;
    using EncryptedOTAHelper::DecryptAbort;
    using EncryptedOTAHelper::DecryptEnd;
    using EncryptedOTAHelper::DecryptStart;
    using EncryptedOTAHelper::IsDecrypting;
    using EncryptedOTAHelper::IsEncryptedOTAEnabled;
};

const char kKey[] = "-----BEGIN RSA PRIVATE KEY----- (mock) -----END-----";
CharSpan Key()
{
    return CharSpan(kKey, sizeof(kKey) - 1);
}

class TestEncryptedOTAHelper : public ::testing::Test
{
protected:
    void SetUp() override { gEspEncMock.Reset(); }
};

} // namespace

TEST_F(TestEncryptedOTAHelper, InitRejectsEmptyKey)
{
    Harness h;
    EXPECT_EQ(h.InitEncryptedOTA(CharSpan()), CHIP_ERROR_INVALID_ARGUMENT);
    EXPECT_FALSE(h.IsEncryptedOTAEnabled());
}

TEST_F(TestEncryptedOTAHelper, InitOnceThenRejectSecond)
{
    Harness h;
    EXPECT_EQ(h.InitEncryptedOTA(Key()), CHIP_NO_ERROR);
    EXPECT_TRUE(h.IsEncryptedOTAEnabled());
    EXPECT_EQ(h.InitEncryptedOTA(Key()), CHIP_ERROR_INCORRECT_STATE);
}

TEST_F(TestEncryptedOTAHelper, DecryptStartRequiresKey)
{
    Harness h;
    EXPECT_EQ(h.DecryptStart(), CHIP_ERROR_INCORRECT_STATE);
    EXPECT_EQ(gEspEncMock.startCalls, 0);
}

TEST_F(TestEncryptedOTAHelper, DecryptStartOpensSessionAndRejectsDouble)
{
    Harness h;
    ASSERT_EQ(h.InitEncryptedOTA(Key()), CHIP_NO_ERROR);

    EXPECT_EQ(h.DecryptStart(), CHIP_NO_ERROR);
    EXPECT_TRUE(h.IsDecrypting());
    EXPECT_EQ(gEspEncMock.startCalls, 1);

    EXPECT_EQ(h.DecryptStart(), CHIP_ERROR_INCORRECT_STATE);
    EXPECT_EQ(gEspEncMock.startCalls, 1);
}

TEST_F(TestEncryptedOTAHelper, DecryptStartFailureSurfaced)
{
    Harness h;
    ASSERT_EQ(h.InitEncryptedOTA(Key()), CHIP_NO_ERROR);
    gEspEncMock.startReturnsNull = true;
    EXPECT_EQ(h.DecryptStart(), CHIP_ERROR_INTERNAL);
    EXPECT_FALSE(h.IsDecrypting());
}

TEST_F(TestEncryptedOTAHelper, DecryptRequiresOpenSession)
{
    Harness h;
    ASSERT_EQ(h.InitEncryptedOTA(Key()), CHIP_NO_ERROR);
    ByteSpan out;
    const uint8_t in[] = { 1, 2, 3 };
    EXPECT_EQ(h.Decrypt(ByteSpan(in), out), CHIP_ERROR_INCORRECT_STATE);
}

TEST_F(TestEncryptedOTAHelper, DecryptTransformsBytesAndOwnsBuffer)
{
    Harness h;
    ASSERT_EQ(h.InitEncryptedOTA(Key()), CHIP_NO_ERROR);
    ASSERT_EQ(h.DecryptStart(), CHIP_NO_ERROR);

    const uint8_t in[] = { 0x10, 0x20, 0x30, 0x40 };
    ByteSpan out;
    EXPECT_EQ(h.Decrypt(ByteSpan(in), out), CHIP_NO_ERROR);
    ASSERT_EQ(out.size(), sizeof(in));
    for (size_t i = 0; i < sizeof(in); i++)
    {
        EXPECT_EQ(out.data()[i], in[i] ^ gEspEncMock.byteXor);
    }
    EXPECT_EQ(gEspEncMock.dataCalls, 1);

    // A second Decrypt must release the previous buffer before producing the next
    // (ASan would flag a leak/double-free here).
    const uint8_t in2[] = { 0xAA, 0xBB };
    ByteSpan out2;
    EXPECT_EQ(h.Decrypt(ByteSpan(in2), out2), CHIP_NO_ERROR);
    ASSERT_EQ(out2.size(), sizeof(in2));
    EXPECT_EQ(out2.data()[0], 0xAA ^ gEspEncMock.byteXor);
    EXPECT_EQ(gEspEncMock.allocCount, 2);

    EXPECT_EQ(h.DecryptEnd(), CHIP_NO_ERROR);
}

TEST_F(TestEncryptedOTAHelper, DecryptBuffersWithoutOutput)
{
    Harness h;
    ASSERT_EQ(h.InitEncryptedOTA(Key()), CHIP_NO_ERROR);
    ASSERT_EQ(h.DecryptStart(), CHIP_NO_ERROR);

    gEspEncMock.produceOutput = false;
    gEspEncMock.dataReturn    = ESP_ERR_NOT_FINISHED; // decryptor still buffering

    const uint8_t in[] = { 1, 2, 3, 4, 5 };
    ByteSpan out;
    EXPECT_EQ(h.Decrypt(ByteSpan(in), out), CHIP_NO_ERROR);
    EXPECT_TRUE(out.empty());
}

TEST_F(TestEncryptedOTAHelper, DecryptDataHardErrorPropagates)
{
    Harness h;
    ASSERT_EQ(h.InitEncryptedOTA(Key()), CHIP_NO_ERROR);
    ASSERT_EQ(h.DecryptStart(), CHIP_NO_ERROR);

    gEspEncMock.dataReturn = ESP_FAIL;
    const uint8_t in[]     = { 9, 9, 9 };
    ByteSpan out;
    EXPECT_EQ(h.Decrypt(ByteSpan(in), out), CHIP_ERROR_INTERNAL);
}

TEST_F(TestEncryptedOTAHelper, DecryptEndClosesSession)
{
    Harness h;
    ASSERT_EQ(h.InitEncryptedOTA(Key()), CHIP_NO_ERROR);
    ASSERT_EQ(h.DecryptStart(), CHIP_NO_ERROR);

    EXPECT_EQ(h.DecryptEnd(), CHIP_NO_ERROR);
    EXPECT_FALSE(h.IsDecrypting());
    EXPECT_EQ(gEspEncMock.endCalls, 1);

    // End with no open session is a state error.
    EXPECT_EQ(h.DecryptEnd(), CHIP_ERROR_INCORRECT_STATE);
}

TEST_F(TestEncryptedOTAHelper, DecryptEndFooterFailureSurfaced)
{
    Harness h;
    ASSERT_EQ(h.InitEncryptedOTA(Key()), CHIP_NO_ERROR);
    ASSERT_EQ(h.DecryptStart(), CHIP_NO_ERROR);

    gEspEncMock.endReturn = ESP_FAIL; // e.g. footer/auth mismatch
    EXPECT_EQ(h.DecryptEnd(), CHIP_ERROR_INTERNAL);
    EXPECT_FALSE(h.IsDecrypting()); // handle cleared regardless
}

TEST_F(TestEncryptedOTAHelper, AbortIsIdempotentAndClears)
{
    Harness h;
    ASSERT_EQ(h.InitEncryptedOTA(Key()), CHIP_NO_ERROR);
    ASSERT_EQ(h.DecryptStart(), CHIP_NO_ERROR);

    h.DecryptAbort();
    EXPECT_FALSE(h.IsDecrypting());
    EXPECT_EQ(gEspEncMock.abortCalls, 1);

    // Abort with no open session does nothing (no underlying call).
    h.DecryptAbort();
    EXPECT_EQ(gEspEncMock.abortCalls, 1);
}
