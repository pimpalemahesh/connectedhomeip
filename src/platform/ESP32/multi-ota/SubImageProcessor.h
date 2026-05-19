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
#include "MultiImageHeader.h"
#include <lib/core/CHIPError.h>
#include <lib/support/Span.h>

enum class DeviceReadinessState : uint8_t
{
    kReady,
    kAlreadyUpToDate,
    kNotReady,
};

namespace chip {
namespace ESP32 {
namespace MultiOTA {

class SubImageProcessor
{
public:
    virtual ~SubImageProcessor() = default;

    // Called once before any bytes are delivered.
    // targetVersion == SubImageHeader.version for this entry.
    // Must return within milliseconds — no blocking I/O.
    virtual DeviceReadinessState IsReadyForOTA(uint32_t targetVersion) = 0;

    // Called once with the full SubImageHeader immediately before the
    // first Write(), only when IsReadyForOTA() returned kReady.
    // Store entry.length to self-track progress; store entry.sha256
    // to verify integrity on the last chunk.
    virtual CHIP_ERROR Init(const SubImageHeader & entry) = 0;

    // Called per chunk. Chunks arrive in order; exactly entry.length
    // bytes total across all calls. Raw binary bytes only — no headers.
    // On the last chunk (running total == entry.length set in Init):
    //   verify SHA-256, commit, finalize transport.
    virtual CHIP_ERROR Write(ByteSpan block) = 0;

    // Called by MultiImageOTAProcessor::Apply() for each processor that received
    // bytes (kReady) this cycle. Make new firmware the active boot target.
    // Default: no-op. Override for processors that require an explicit apply step.
    virtual CHIP_ERROR Apply() { return CHIP_NO_ERROR; }

    // Called if the OTA session is aborted after Init() was called.
    // Component firmware rollback is the application's responsibility.
    virtual void Abort() {}
};

} // namespace MultiOTA
} // namespace ESP32
} // namespace chip
