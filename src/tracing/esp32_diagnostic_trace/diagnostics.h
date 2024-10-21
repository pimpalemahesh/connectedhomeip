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
#include <lib/support/Span.h>

namespace chip {
namespace Tracing {

// Base class for diagnostics data, which can be extended for different types of diagnostics
class Diagnostics {
public:
    virtual ~Diagnostics() = default;
    virtual const char* GetType() const = 0; // Returns the type of diagnostic (e.g., METRIC, TRACE)
};

// Template class for storing metrics with a label, value, and timestamp
template<typename T>
class Metric : public Diagnostics {
public:
    Metric(const char* label, T value, uint32_t timestamp)
        : label_(label), value_(value), timestamp_(timestamp) {}

    Metric() {}

    const char* GetType() const override { return "METRIC"; }
    const char* GetLabel() const { return label_; }
    T GetValue() const { return value_; }
    uint32_t GetTimestamp() const { return timestamp_; }

private:
    const char* label_;
    T value_;
    uint32_t timestamp_;
};


// Class for tracing diagnostic data, storing a label and timestamp
class Trace : public Diagnostics {
public:
    Trace(const char* label, uint32_t timestamp)
        : label_(label), timestamp_(timestamp) {}

    Trace() {}

    const char* GetType() const override { return "TRACE"; }
    const char* GetLabel() const { return label_; }
    uint32_t GetTimestamp() const { return timestamp_; }

private:
    const char* label_;
    uint32_t timestamp_;
};

// Class for counting diagnostic events, storing a label, count, and timestamp
class Counter : public Diagnostics {
public:
    Counter(const char* label, int32_t count, uint32_t timestamp)
        : label_(label), count_(count), timestamp_(timestamp) {}

    Counter() {}

    const char* GetType() const override { return "COUNTER"; }

    const char* GetLabel() const { return label_; }
    int32_t GetCount() const { return count_; }
    uint32_t GetTimestamp() const { return timestamp_; }

private:
    const char* label_;
    int32_t count_;
    uint32_t timestamp_;
};

// Interface for storing and retrieving diagnostics data
class IDiagnosticStorage {
public:
    virtual ~IDiagnosticStorage() = default;

    /**
     * @brief Store a diagnostic record in the storage.
     *
     * @param[in] diagnostic  A reference to the diagnostic object to store.
     * 
     * @return CHIP_NO_ERROR on success, or an appropriate error code on failure.
     */
    virtual CHIP_ERROR Store(Diagnostics& diagnostic) = 0;

    /**
     * @brief Retrieve stored diagnostic data from the storage.
     * 
     * @param[out] payload  A `MutableByteSpan` where the diagnostic data will be copied.
     * 
     * @return CHIP_NO_ERROR on success, or an appropriate error code on failure.
     */
    virtual CHIP_ERROR Retrieve(MutableByteSpan &payload) = 0;
};

} // namespace Tracing
} // namespace chip