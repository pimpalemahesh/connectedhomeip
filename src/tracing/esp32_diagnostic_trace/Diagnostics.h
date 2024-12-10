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
#include <lib/core/CHIPError.h>
#include <lib/core/TLVCircularBuffer.h>
#include <lib/support/Span.h>

namespace chip {
namespace Tracing {

namespace Diagnostics {

enum class DIAGNOSTICS_TAG
{
    LABEL = 0,
    VALUE,
    TIMESTAMP
};

/**
 * @class DiagnosticEntry
 * @brief Abstract base class for encoding diagnostic entries into TLV format.
 *
 */
class DiagnosticEntry
{
public:
    /**
     * @brief Virtual destructor for proper cleanup in derived classes.
     */
    virtual ~DiagnosticEntry() = default;

    /**
     * @brief Pure virtual method to encode diagnostic data into a TLV structure.
     *
     * @param writer A reference to the `chip::TLV::CircularTLVWriter` instance
     *               used to encode the TLV data.
     * @return CHIP_ERROR Returns an error code indicating the success or
     *                    failure of the encoding operation.
     */
    virtual CHIP_ERROR Encode(chip::TLV::CircularTLVWriter & writer) = 0;
};

template <typename T>
class Diagnostic : public DiagnosticEntry
{
public:
    Diagnostic(const char * label, T value, uint32_t timestamp) : label_(label), value_(value), timestamp_(timestamp) {}

    CHIP_ERROR Encode(chip::TLV::CircularTLVWriter & writer) override
    {
        chip::TLV::TLVType DiagnosticOuterContainer = chip::TLV::kTLVType_NotSpecified;
        ReturnErrorOnFailure(
            writer.StartContainer(chip::TLV::AnonymousTag(), chip::TLV::kTLVType_Structure, DiagnosticOuterContainer));
        // TIMESTAMP
        ReturnErrorOnFailure(writer.Put(chip::TLV::ContextTag(DIAGNOSTICS_TAG::TIMESTAMP), timestamp_));
        // LABEL
        ReturnErrorOnFailure(writer.PutString(chip::TLV::ContextTag(DIAGNOSTICS_TAG::LABEL), label_));
        // VALUE
        if constexpr (std::is_same_v<T, const char *>)
        {
            ReturnErrorOnFailure(writer.PutString(chip::TLV::ContextTag(DIAGNOSTICS_TAG::VALUE), value_));
        }
        else
        {
            ReturnErrorOnFailure(writer.Put(chip::TLV::ContextTag(DIAGNOSTICS_TAG::VALUE), value_));
        }
        ReturnErrorOnFailure(writer.EndContainer(DiagnosticOuterContainer));
        ReturnErrorOnFailure(writer.Finalize());
        ChipLogProgress(DeviceLayer, "Diagnostic Value written to storage successfully. label: %s\n", label_);
        return CHIP_NO_ERROR;
    }

private:
    const char * label_;
    T value_;
    uint32_t timestamp_;
};

/**
 * @brief Interface for storing and retrieving diagnostic data.
 */
class DiagnosticStorageInterface
{
public:
    /**
     * @brief Virtual destructor for the interface.
     */
    virtual ~DiagnosticStorageInterface() = default;

    /**
     * @brief Stores a diagnostic entry.
     * @param diagnostic  Reference to a DiagnosticEntry object containing the
     *                    diagnostic data to store.
     * @return CHIP_ERROR Returns CHIP_NO_ERROR on success, or an appropriate error code on failure.
     */
    virtual CHIP_ERROR Store(DiagnosticEntry & diagnostic) = 0;

    /**
     * @brief Retrieves diagnostic data as a payload.
     * @param payload  Reference to a MutableByteSpan where the retrieved
     *                 diagnostic data will be stored.
     * @return CHIP_ERROR Returns CHIP_NO_ERROR on success, or an appropriate error code on failure.
     */
    virtual CHIP_ERROR Retrieve(MutableByteSpan & payload) = 0;

    virtual bool IsEmptyBuffer() = 0;

    virtual uint32_t GetDataSize() = 0;
};

} // namespace Diagnostics
} // namespace Tracing
} // namespace chip
