#pragma once

#include <lib/core/CHIPError.h>
#include <lib/support/Span.h>

namespace chip {
namespace Tracing {

class Diagnostics {
public:
    virtual ~Diagnostics() = default;
    virtual const char* GetType() const = 0;
};

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

class IDiagnosticStorage {
public:
    virtual ~IDiagnosticStorage() = default;

    virtual CHIP_ERROR Store(Diagnostics& diagnostic) = 0;

    virtual CHIP_ERROR Retrieve(MutableByteSpan payload) = 0;
};

} // namespace Tracing
} // namespace chip
