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

// Unit test for MultiOTAImageHeaderParser (MultiOTAImageHeader.cpp).
//
// Standalone: this lives under multi-ota/tests/ and is intentionally NOT wired
// into any aggregate CHIP test group. Build/run it explicitly, e.g.:
//     gn gen out/host
//     ninja -C out/host TestMultiOTAImageHeader
//     ./out/host/tests/TestMultiOTAImageHeader
//
// The header parser has no ESP-IDF dependency, so it compiles for the host as-is.

#include <pw_unit_test/framework.h>

#include "../MultiOTAImageHeader.h"

#include <lib/support/Span.h>

#include <stdint.h>
#include <vector>

using namespace chip;

namespace {

// ---- serialization helpers -------------------------------------------------

void AppendLE32(std::vector<uint8_t> & v, uint32_t x)
{
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 24));
}

struct Entry
{
    uint32_t imageId = 1;
    uint32_t version = 1;
    uint32_t offset  = 0;
    uint32_t length  = 100;
    uint8_t shaFill  = 0xAB;
};

// Serialize a full MultiImageHeader: fixed preamble + numImages entries.
std::vector<uint8_t> BuildHeader(const std::vector<Entry> & entries, uint32_t magic = kMultiOTAImageFileIdentifier,
                                 uint8_t numImagesOverride = 0, uint8_t reserved0 = 0)
{
    std::vector<uint8_t> v;
    AppendLE32(v, magic);
    v.push_back(numImagesOverride != 0 ? numImagesOverride : static_cast<uint8_t>(entries.size()));
    v.push_back(reserved0);
    v.push_back(0);
    v.push_back(0);
    for (const auto & e : entries)
    {
        AppendLE32(v, e.imageId);
        AppendLE32(v, e.version);
        AppendLE32(v, e.offset);
        AppendLE32(v, e.length);
        for (uint32_t i = 0; i < Crypto::kSHA256_Hash_Length; i++)
        {
            v.push_back(e.shaFill);
        }
    }
    return v;
}

ByteSpan AsSpan(const std::vector<uint8_t> & v)
{
    return ByteSpan(v.data(), v.size());
}

} // namespace

// ---- tests -----------------------------------------------------------------

TEST(TestMultiOTAImageHeader, DecodesSingleImageHeader)
{
    Entry e;
    e.imageId            = 0x12345678;
    e.version            = 7;
    e.offset             = 56; // 8 + 1*48
    e.length             = 4096;
    std::vector<uint8_t> raw = BuildHeader({ e });

    MultiOTAImageHeaderParser parser;
    parser.Init();
    EXPECT_TRUE(parser.IsInitialized());
    EXPECT_FALSE(parser.IsHeaderParsed());

    ByteSpan span = AsSpan(raw);
    MultiOTAImageHeader header;
    EXPECT_EQ(parser.AccumulateAndDecode(span, header), CHIP_NO_ERROR);
    EXPECT_TRUE(parser.IsHeaderParsed());

    EXPECT_EQ(header.numImages, 1u);
    ASSERT_EQ(header.subImages.size(), 1u);
    EXPECT_EQ(header.subImages[0].imageId, 0x12345678u);
    EXPECT_EQ(header.subImages[0].version, 7u);
    EXPECT_EQ(header.subImages[0].offset, 56u);
    EXPECT_EQ(header.subImages[0].length, 4096u);

    // The header consumed the whole buffer; nothing trails it here.
    EXPECT_EQ(span.size(), 0u);
}

TEST(TestMultiOTAImageHeader, DecodesMultipleImagesAndPreservesTrailingData)
{
    std::vector<Entry> entries;
    entries.push_back({ 0x300, 2, 152, 10, 0x11 }); // 8 + 3*48 = 152
    entries.push_back({ 0x400, 3, 162, 20, 0x22 });
    entries.push_back({ kAppImageProcessorTag, 9, 182, 30, 0x33 });

    std::vector<uint8_t> raw = BuildHeader(entries);
    // Append some "binary payload" bytes after the header.
    const uint8_t trailer[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    raw.insert(raw.end(), trailer, trailer + sizeof(trailer));

    MultiOTAImageHeaderParser parser;
    parser.Init();
    ByteSpan span = AsSpan(raw);
    MultiOTAImageHeader header;
    EXPECT_EQ(parser.AccumulateAndDecode(span, header), CHIP_NO_ERROR);

    ASSERT_EQ(header.subImages.size(), 3u);
    EXPECT_EQ(header.subImages[0].imageId, 0x300u);
    EXPECT_EQ(header.subImages[1].imageId, 0x400u);
    EXPECT_EQ(header.subImages[2].imageId, kAppImageProcessorTag);
    EXPECT_EQ(header.subImages[1].length, 20u);

    // span must now point exactly at the trailing payload bytes.
    ASSERT_EQ(span.size(), sizeof(trailer));
    EXPECT_EQ(span.data()[0], 0xDE);
    EXPECT_EQ(span.data()[3], 0xEF);
}

TEST(TestMultiOTAImageHeader, AccumulatesAcrossChunksByteByByte)
{
    std::vector<Entry> entries = { { 0x300, 1, 104, 5, 0x55 }, { kAppImageProcessorTag, 1, 109, 5, 0x66 } };
    std::vector<uint8_t> raw   = BuildHeader(entries); // 8 + 2*48 = 104 bytes

    MultiOTAImageHeaderParser parser;
    parser.Init();
    MultiOTAImageHeader header;

    // Feed one byte at a time; every call but the last must report "need more".
    for (size_t i = 0; i + 1 < raw.size(); i++)
    {
        ByteSpan chunk(&raw[i], 1);
        EXPECT_EQ(parser.AccumulateAndDecode(chunk, header), CHIP_ERROR_BUFFER_TOO_SMALL);
        EXPECT_FALSE(parser.IsHeaderParsed());
        EXPECT_EQ(chunk.size(), 0u); // chunk fully consumed
    }

    ByteSpan last(&raw[raw.size() - 1], 1);
    EXPECT_EQ(parser.AccumulateAndDecode(last, header), CHIP_NO_ERROR);
    EXPECT_TRUE(parser.IsHeaderParsed());
    ASSERT_EQ(header.subImages.size(), 2u);
    EXPECT_EQ(header.subImages[1].imageId, kAppImageProcessorTag);
}

TEST(TestMultiOTAImageHeader, RejectsBadMagic)
{
    std::vector<uint8_t> raw = BuildHeader({ Entry{} }, /*magic=*/0xDEADBEEF);
    MultiOTAImageHeaderParser parser;
    parser.Init();
    ByteSpan span = AsSpan(raw);
    MultiOTAImageHeader header;
    EXPECT_EQ(parser.AccumulateAndDecode(span, header), CHIP_ERROR_INVALID_FILE_IDENTIFIER);
    // On a hard error the parser clears itself.
    EXPECT_FALSE(parser.IsInitialized());
}

TEST(TestMultiOTAImageHeader, RejectsZeroImages)
{
    // numImages override = 0, no entries.
    std::vector<uint8_t> raw = BuildHeader({}, kMultiOTAImageFileIdentifier, /*numImagesOverride=*/0);
    // BuildHeader with empty entries + override 0 yields numImages byte = 0.
    MultiOTAImageHeaderParser parser;
    parser.Init();
    ByteSpan span = AsSpan(raw);
    MultiOTAImageHeader header;
    EXPECT_EQ(parser.AccumulateAndDecode(span, header), CHIP_ERROR_INVALID_ARGUMENT);
}

TEST(TestMultiOTAImageHeader, RejectsNonZeroReserved)
{
    std::vector<uint8_t> raw = BuildHeader({ Entry{} }, kMultiOTAImageFileIdentifier, 0, /*reserved0=*/1);
    MultiOTAImageHeaderParser parser;
    parser.Init();
    ByteSpan span = AsSpan(raw);
    MultiOTAImageHeader header;
    EXPECT_EQ(parser.AccumulateAndDecode(span, header), CHIP_ERROR_INVALID_ARGUMENT);
}

TEST(TestMultiOTAImageHeader, RejectsZeroImageId)
{
    Entry e;
    e.imageId                = 0;
    std::vector<uint8_t> raw = BuildHeader({ e });
    MultiOTAImageHeaderParser parser;
    parser.Init();
    ByteSpan span = AsSpan(raw);
    MultiOTAImageHeader header;
    EXPECT_EQ(parser.AccumulateAndDecode(span, header), CHIP_ERROR_INVALID_ARGUMENT);
}

TEST(TestMultiOTAImageHeader, RejectsZeroLengthEntry)
{
    Entry e;
    e.length                 = 0;
    std::vector<uint8_t> raw = BuildHeader({ e });
    MultiOTAImageHeaderParser parser;
    parser.Init();
    ByteSpan span = AsSpan(raw);
    MultiOTAImageHeader header;
    EXPECT_EQ(parser.AccumulateAndDecode(span, header), CHIP_ERROR_INVALID_ARGUMENT);
}

TEST(TestMultiOTAImageHeader, ClearResetsState)
{
    std::vector<uint8_t> raw = BuildHeader({ Entry{} });
    MultiOTAImageHeaderParser parser;
    parser.Init();
    ByteSpan span = AsSpan(raw);
    MultiOTAImageHeader header;
    EXPECT_EQ(parser.AccumulateAndDecode(span, header), CHIP_NO_ERROR);
    EXPECT_TRUE(parser.IsHeaderParsed());

    parser.Clear();
    EXPECT_FALSE(parser.IsInitialized());
    EXPECT_FALSE(parser.IsHeaderParsed());

    // Reusable after re-Init.
    parser.Init();
    ByteSpan span2 = AsSpan(raw);
    MultiOTAImageHeader header2;
    EXPECT_EQ(parser.AccumulateAndDecode(span2, header2), CHIP_NO_ERROR);
    EXPECT_EQ(header2.subImages.size(), 1u);
}
