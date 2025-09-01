#pragma once
#include <nvs_flash.h>
#include <string>
#include <tracing/esp32_diagnostic_trace/StorageInterface.h>

namespace chip {
namespace Tracing {
namespace Diagnostics {

class NVSDiagnosticStorage : public DiagnosticStorageInterface
{
public:
    NVSDiagnosticStorage(const char * nvs_namespace);
    ~NVSDiagnosticStorage();

    CHIP_ERROR Store(const DiagnosticEntry & entry) override;
    CHIP_ERROR Retrieve(MutableByteSpan & span, uint32_t & read_entries) override;
    bool IsBufferEmpty() override;
    uint32_t GetDataSize() override;
    CHIP_ERROR ClearBuffer() override;
    CHIP_ERROR ClearBuffer(uint32_t entries) override;

private:
    bool mInitialized          = false;
    const char * mNvsNamespace = nullptr;
    uint32_t mStartIndex       = 0;
    uint32_t mLastIndex        = 0;
    std::string mIndexKeyBuffer; // Buffer to store the index key string

    const char * getIndexKey(uint32_t index);
};

} // namespace Diagnostics
} // namespace Tracing
} // namespace chip
