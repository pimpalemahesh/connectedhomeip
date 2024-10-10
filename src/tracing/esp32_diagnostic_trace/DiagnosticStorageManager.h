/*
 *
 *    Copyright (c) 2024 Project CHIP Authors
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

#pragma once

#include "Diagnostics.h"
#include <lib/support/CHIPMem.h>
#include <lib/core/CHIPError.h>

#define END_USER_BUFFER_SIZE CONFIG_END_USER_BUFFER_SIZE
#define NETWORK_BUFFER_SIZE CONFIG_NETWORK_BUFFER_SIZE

namespace chip {
namespace Tracing {
using namespace chip::Platform;

class DiagnosticStorageImpl : public DiagnosticStorageInterface
{
public:

    static DiagnosticStorageImpl& GetInstance()
    {
        static DiagnosticStorageImpl instance;
        return instance;
    }

    DiagnosticStorageImpl(const DiagnosticStorageImpl &) = delete;
    DiagnosticStorageImpl & operator=(const DiagnosticStorageImpl &) = delete;

    CHIP_ERROR Store(DiagnosticEntry & diagnostic) override;

    CHIP_ERROR Retrieve(MutableByteSpan &payload) override;

    bool IsEmptyBuffer();

private:
    DiagnosticStorageImpl();
    ~DiagnosticStorageImpl();

    TLVCircularBuffer mEndUserCircularBuffer;
    TLVCircularBuffer mNetworkCircularBuffer;
    uint8_t mEndUserBuffer[END_USER_BUFFER_SIZE];
    uint8_t mNetworkBuffer[NETWORK_BUFFER_SIZE];
};

} // namespace Tracing
} // namespace chip
