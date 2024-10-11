#pragma once

#include <lib/core/TLVCircularBuffer.h>
#include <lib/support/CHIPMem.h>
#include <tracing/metric_event.h>

#define END_USER_BUFFER_SIZE 2048
#define NETWORK_BUFFER_SIZE 1024
#define RETRIEVE_DATA_CHUNK_NUMBER 10

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

    CHIP_ERROR EncodeData();
    CHIP_ERROR DecodeData();
    CHIP_ERROR StoreData(const char *key, int16_t value = 0);
    CHIP_ERROR RetrieveData(ByteSpan payload);
    bool IsEmptyBuffer();

private:
    DiagnosticStorage();
    ~DiagnosticStorage();

    TLVCircularBuffer mEndUserCircularBuffer, mNetworkCircularBuffer;
    uint8_t mEndUserBuffer[END_USER_BUFFER_SIZE];
    uint8_t mNetworkBuffer[NETWORK_BUFFER_SIZE];
};

} // namespace Tracing
} // namespace chip
