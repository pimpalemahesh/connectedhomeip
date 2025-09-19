/*
 *
 *    Copyright (c) 2025 Project CHIP Authors
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
#include <lib/core/TLVCircularBuffer.h>
#include <tracing/esp32_diagnostic_trace/StorageInterface.h>

namespace chip {
namespace Tracing {
namespace Diagnostics {

/**
 * @brief Diagnostic storage class
 */
class CircularDiagnosticBuffer : public chip::TLV::TLVCircularBuffer, public DiagnosticStorageInterface
{
public:
    CircularDiagnosticBuffer(uint8_t * buffer, uint32_t bufferLength) : chip::TLV::TLVCircularBuffer(buffer, bufferLength) {}

    CHIP_ERROR Store(const DiagnosticEntry & entry) override;
    CHIP_ERROR Retrieve(MutableByteSpan & span, uint32_t & read_entries) override;
    bool IsBufferEmpty() override;
    uint32_t GetDataSize() override;
    CHIP_ERROR ClearBuffer() override;
    CHIP_ERROR ClearBuffer(uint32_t entries) override;

private:
    chip::TLV::CircularTLVReader mReader;
    chip::TLV::CircularTLVWriter mWriter;
};

} // namespace Diagnostics
} // namespace Tracing
} // namespace chip
