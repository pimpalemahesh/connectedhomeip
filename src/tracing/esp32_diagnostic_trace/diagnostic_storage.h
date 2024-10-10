#pragma once

#include <lib/core/TLVCircularBuffer.h>
#include <lib/support/CHIPMem.h>
#include <lib/core/TLVWriter.h>
#include <lib/core/TLVReader.h>
#include <tracing/metric_event.h>

#define TRACE_BUFFER_SIZE 4096
#define RETRIEVE_DATA_CHUNK_NUMBER 30

namespace chip {
namespace Tracing {
using namespace chip::Platform;
using namespace chip::TLV;
using chip::Tracing::MetricEvent;

using ValueType = MetricEvent::Value;

class DiagnosticStorage {
public:
    static DiagnosticStorage & GetInstance()
    {
        static DiagnosticStorage instance;
        return instance;
    }

    // Deleted copy constructor and assignment operator to prevent copying
    DiagnosticStorage(const DiagnosticStorage &) = delete;
    DiagnosticStorage & operator=(const DiagnosticStorage &) = delete;

    CHIP_ERROR Serialize();
    CHIP_ERROR Deserialize();
    CHIP_ERROR StoreData(const char *key, uint16_t value = 0);
    CHIP_ERROR RetrieveData(ByteSpan *payload);
    bool IsEmptyBuffer();

private:
    DiagnosticStorage();
    ~DiagnosticStorage();

    TLVCircularBuffer mCircularBuffer;
    uint8_t mBuffer[TRACE_BUFFER_SIZE];

    CHIP_ERROR StoreMetric(const char* key, ValueType value);
    CHIP_ERROR StoreTrace(const char* label);

};

} // namespace Tracing
} // namespace chip
