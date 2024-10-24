#pragma once

#include "diagnostics.h"
#include <lib/support/CHIPMem.h>
#include <lib/core/CHIPError.h>

#define END_USER_BUFFER_SIZE CONFIG_END_USER_BUFFER_SIZE
#define NETWORK_BUFFER_SIZE CONFIG_NETWORK_BUFFER_SIZE

namespace chip {
namespace Tracing {
using namespace chip::Platform;

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

    CHIP_ERROR Retrieve(MutableByteSpan &payload) override;

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
