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

#include "diagnostics.h"
#include <lib/core/TLVCircularBuffer.h>
#include <lib/support/CHIPMem.h>
#include <lib/core/CHIPError.h>

#define END_USER_BUFFER_SIZE 2048
#define NETWORK_BUFFER_SIZE 1024

namespace chip {
namespace Tracing {
using namespace chip::Platform;
using namespace chip::TLV;

// TAGs for storing TLV data
enum TAG
{
    METRIC     = 0,
    TRACE      = 1,
    LABEL      = 2,
    VALUE      = 3,
    COUNTER    = 4,
    TIMESTAMP  = 5
};

enum class DiagnosticType
{
    kEndUser = 0,
    kNetwork = 1,
    kUndefined = 2
};

/**
 * @brief
 *   InMemory implementation of the IDiagnosticStorage interface.
 *
 * @details
 *   Uses TLV circular buffers to store and retrieve diagnostic data.
 */
class InMemoryDiagnosticStorage : public IDiagnosticStorage
{
public:

    // Singletone instance
    static InMemoryDiagnosticStorage& GetInstance(DiagnosticType diagnosticType)
    {
        if (diagnosticType == DiagnosticType::kEndUser) {
            static InMemoryDiagnosticStorage endUserInstance(DiagnosticType::kEndUser);
            return endUserInstance;
        }
        else if (diagnosticType == DiagnosticType::kNetwork) {
            static InMemoryDiagnosticStorage networkInstance(DiagnosticType::kNetwork);
            return networkInstance;
        }
        else {
            static InMemoryDiagnosticStorage undefinedInstance(DiagnosticType::kUndefined);
            return undefinedInstance;
        }
    }


    // Deleted copy constructor and assignment operator to prevent copying
    InMemoryDiagnosticStorage(const InMemoryDiagnosticStorage &) = delete;
    InMemoryDiagnosticStorage & operator=(const InMemoryDiagnosticStorage &) = delete;

    // Override store method from IDiagnosticStorage interface
    CHIP_ERROR Store(Diagnostics & diagnostic) override;

    // Override retrieve method from IDiagnosticStorage interface
    CHIP_ERROR Retrieve(MutableByteSpan payload) override;

    /**
     * @brief Check if the in-memory diagnostic buffer is empty.
     * 
     * This method checks whether there is any diagnostic data stored in the in-memory buffer.
     * 
     * @return true if the buffer is empty, false if there is data stored.
     */
    bool IsEmptyBuffer();

    TLVCircularBuffer getInMemoryBuffer();

private:
    InMemoryDiagnosticStorage(DiagnosticType diagnosticType);
    ~InMemoryDiagnosticStorage();

    DiagnosticType mDiagnosticType = DiagnosticType::kUndefined;

    // TLVCircularBuffer instance to store end user diagnostic data
    TLVCircularBuffer mEndUserCircularBuffer;

    // TLVCircularBuffer instance to store network diagnostic data
    TLVCircularBuffer mNetworkCircularBuffer;

    // Buffer to store end user diagnostics data
    uint8_t mEndUserBuffer[END_USER_BUFFER_SIZE];

    // Buffer to store network diagnostics data
    uint8_t mNetworkBuffer[NETWORK_BUFFER_SIZE];

    CHIP_ERROR StoreDiagnosticData(CircularTLVWriter & writer, Diagnostics & diagnostic);

    // Method for storing metric diagnostics
    CHIP_ERROR StoreMetric(CircularTLVWriter & writer, const Metric<int32_t> * metric);

    // Method for storing trace diagnostics
    CHIP_ERROR StoreTrace(CircularTLVWriter & writer, const Trace * trace);

    // Method for storing counter diagnostics
    CHIP_ERROR StoreCounter(CircularTLVWriter & writer, const Counter * counter);

    // Method for reading data from the buffer and write it to the payload
    CHIP_ERROR ReadAndCopyData(CircularTLVReader & reader, chip::TLV::TLVWriter & writer);

    void LogBufferStats();
};

} // namespace Tracing
} // namespace chip
