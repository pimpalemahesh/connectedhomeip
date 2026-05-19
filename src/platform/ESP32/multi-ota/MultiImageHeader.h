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
#include <stdint.h>

#define MULTI_IMAGE_HEADER_MAGIC 0x4D494F54u // "MIOT"

// Fixed 8 bytes at the start of every multi-image OTA payload.
struct __attribute__((packed)) MultiImageHeader
{
    uint32_t magic;      // must equal MULTI_IMAGE_HEADER_MAGIC
    uint8_t numImages;   // number of SubImageHeader entries (max 255)
    uint8_t reserved[3]; // must be zero
};
static_assert(sizeof(MultiImageHeader) == 8);

// Fixed 48 bytes per component entry.
struct __attribute__((packed)) SubImageHeader
{
    uint8_t imageId;     // identifies which sub-processor handles this binary
    uint32_t version;    // expected installed version of this binary
    uint32_t offset;     // byte offset of binary data from payload start
    uint32_t length;     // exact byte count of the binary
    uint8_t sha256[32];  // mandatory SHA-256 digest of [offset, offset+length)
    uint8_t reserved[3]; // must be zero; reserved for future extensions
};
static_assert(sizeof(SubImageHeader) == 48);

// Safe field read from a packed/potentially-unaligned pointer.
// Always use this instead of direct field access on SubImageHeader*.
template <typename T>
inline T ReadField(const void * src)
{
    T val;
    memcpy(&val, src, sizeof(T));
    return val;
}
