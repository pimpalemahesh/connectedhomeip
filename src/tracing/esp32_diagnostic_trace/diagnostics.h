#pragma once

#include <lib/core/CHIPError.h>
#include <lib/support/Span.h>
#include <lib/core/TLVCircularBuffer.h>

namespace chip {
namespace Tracing {

using namespace chip::TLV;

enum class DIAGNOSTICS_TAG
{
    METRIC     = 0,
    TRACE      = 1,
    COUNTER    = 2,
    LABEL      = 3,
    GROUP      = 4,
    VALUE      = 5,
    TIMESTAMP  = 6
};

class Diagnostics {
public:
    virtual ~Diagnostics() = default;
    virtual CHIP_ERROR Encode(CircularTLVWriter &writer) = 0;
};

template<typename T>
class Metric : public Diagnostics {
public:
    Metric(const char* label, T value, uint32_t timestamp)
        : label_(label), value_(value), timestamp_(timestamp) {}

    Metric() {}

    const char* GetLabel() const { return label_; }
    T GetValue() const { return value_; }
    uint32_t GetTimestamp() const { return timestamp_; }

    CHIP_ERROR Encode(CircularTLVWriter &writer) override {
        CHIP_ERROR err = CHIP_NO_ERROR;
        chip::TLV::TLVType metricContainer;
        err = writer.StartContainer(ContextTag(DIAGNOSTICS_TAG::METRIC), chip::TLV::TLVType::kTLVType_Structure, metricContainer);
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to start TLV container for metric : %s", chip::ErrorStr(err)));

        // LABEL
        err = writer.PutString(ContextTag(DIAGNOSTICS_TAG::LABEL), label_);
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to write LABEL for METRIC : %s", chip::ErrorStr(err)));

        // VALUE
        err = writer.Put(ContextTag(DIAGNOSTICS_TAG::VALUE), value_);
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to write VALUE for METRIC : %s", chip::ErrorStr(err)));

        // TIMESTAMP
        err = writer.Put(ContextTag(DIAGNOSTICS_TAG::TIMESTAMP), timestamp_);
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to write TIMESTAMP for METRIC : %s", chip::ErrorStr(err)));

        printf("Metric Value written to storage successfully : %s\n", label_);
        err = writer.EndContainer(metricContainer);
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to end TLV container for metric : %s", chip::ErrorStr(err)));
        return err;
    }

private:
    const char* label_;
    T value_;
    uint32_t timestamp_;
};

class Trace : public Diagnostics {
public:
    Trace(const char* label, const char* group,  uint32_t timestamp)
        : label_(label), group_(group), timestamp_(timestamp) {}

    Trace() {}

    const char* GetLabel() const { return label_; }
    uint32_t GetTimestamp() const { return timestamp_; }
    const char* GetGroup() const { return group_; }

    CHIP_ERROR Encode(CircularTLVWriter &writer) override {
        CHIP_ERROR err = CHIP_NO_ERROR;
        chip::TLV::TLVType traceContainer;
        err = writer.StartContainer(ContextTag(DIAGNOSTICS_TAG::TRACE), chip::TLV::TLVType::kTLVType_Structure, traceContainer);
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to start TLV container for Trace: %s", chip::ErrorStr(err)));

        // LABEL
        err = writer.PutString(ContextTag(DIAGNOSTICS_TAG::LABEL), label_);
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to write LABEL for TRACE : %s", chip::ErrorStr(err)));

        // GROUP
        err = writer.PutString(ContextTag(DIAGNOSTICS_TAG::GROUP), group_);
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to write GROUP for TRACE : %s", chip::ErrorStr(err)));

        // TIMESTAMP
        err = writer.Put(ContextTag(DIAGNOSTICS_TAG::TIMESTAMP), timestamp_);
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to write TIMESTAMP for METRIC : %s", chip::ErrorStr(err)));

        printf("Trace Value written to storage successfully : %s\n", label_);
        err = writer.EndContainer(traceContainer);
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to end TLV container for Trace : %s", chip::ErrorStr(err)));
        return err;
    }

private:
    const char* label_;
    const char* group_;
    uint32_t timestamp_;
};

class Counter : public Diagnostics {
public:
    Counter(const char* label, uint32_t count, uint32_t timestamp)
        : label_(label), count_(count), timestamp_(timestamp) {}

    Counter() {}

    uint32_t GetCount() const { return count_; }

    uint32_t GetTimestamp() const { return timestamp_; }

    CHIP_ERROR Encode(CircularTLVWriter &writer) override {
        CHIP_ERROR err = CHIP_NO_ERROR;
        chip::TLV::TLVType counterContainer;
        err = writer.StartContainer(ContextTag(DIAGNOSTICS_TAG::COUNTER), chip::TLV::TLVType::kTLVType_Structure, counterContainer);
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to start TLV container for Counter: %s", chip::ErrorStr(err)));

        // LABEL
        err = writer.PutString(ContextTag(DIAGNOSTICS_TAG::LABEL), label_);
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to write LABEL for COUNTER : %s", chip::ErrorStr(err)));

        // COUNT
        err = writer.Put(ContextTag(DIAGNOSTICS_TAG::COUNTER), count_);
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to write VALUE for COUNTER : %s", chip::ErrorStr(err)));

        // TIMESTAMP
        err = writer.Put(ContextTag(DIAGNOSTICS_TAG::TIMESTAMP), timestamp_);
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to write TIMESTAMP for COUNTER : %s", chip::ErrorStr(err)));

        printf("Counter Value written to storage successfully : %s\n", label_);
        err = writer.EndContainer(counterContainer);
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to end TLV container for counter : %s", chip::ErrorStr(err)));
        return err;
    }

private:
    const char* label_;
    uint32_t count_;
    uint32_t timestamp_;
};

class IDiagnosticStorage {
public:
    virtual ~IDiagnosticStorage() = default;

    virtual CHIP_ERROR Store(Diagnostics& diagnostic) = 0;

    virtual CHIP_ERROR Retrieve(MutableByteSpan &payload) = 0;
};

} // namespace Tracing
} // namespace chip
