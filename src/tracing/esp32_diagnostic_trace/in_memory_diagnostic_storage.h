#pragma once

#include "diagnostics.h"
#include <lib/core/TLVCircularBuffer.h>
#include <lib/support/CHIPMem.h>
#include <lib/core/CHIPError.h>

#define END_USER_BUFFER_SIZE 2048
#define NETWORK_BUFFER_SIZE 1024
#define RETRIEVE_DATA_CHUNK_NUMBER 10

namespace chip {
namespace Tracing {
using namespace chip::Platform;
using namespace chip::TLV;

enum TAG
{
    METRIC     = 0,
    TRACE      = 1,
    LABEL      = 2,
    VALUE      = 3,
    COUNTER    = 4,
    TIMESTAMP  = 5
};

class InMemoryDiagnosticStorage : public IDiagnosticStorage
{
public:

    static InMemoryDiagnosticStorage& GetInstance()
    {
        static InMemoryDiagnosticStorage instance;
        return instance;
    }

    InMemoryDiagnosticStorage(const InMemoryDiagnosticStorage &) = delete;
    InMemoryDiagnosticStorage & operator=(const InMemoryDiagnosticStorage &) = delete;

    CHIP_ERROR Store(Diagnostics & diagnostic) override;

    CHIP_ERROR Retrieve(MutableByteSpan payload) override;

    bool IsEmptyBuffer();

private:
    InMemoryDiagnosticStorage();
    ~InMemoryDiagnosticStorage();

    TLVCircularBuffer mEndUserCircularBuffer;
    TLVCircularBuffer mNetworkCircularBuffer;
    uint8_t mEndUserBuffer[END_USER_BUFFER_SIZE];
    uint8_t mNetworkBuffer[NETWORK_BUFFER_SIZE];
};

} // namespace Tracing
} // namespace chip
