#pragma once

#include <lib/support/Span.h>
#include <tracing/esp32_diagnostic_trace/DiagnosticEntry.h>

namespace chip {
namespace Tracing {
namespace Diagnostics {

class DiagnosticStorageInterface
{
public:
    virtual ~DiagnosticStorageInterface() = default;
    /**
     * @brief Stores a diagnostic entry in the diagnostic storage buffer.
     * @param diagnostic  A reference to a `DiagnosticEntry` object that contains
     *                    the diagnostic data to be stored.
     * @return CHIP_ERROR  Returns `CHIP_NO_ERROR` if the data is successfully stored,
     *                     or an appropriate error code in case of failure.
     */
    virtual CHIP_ERROR Store(const DiagnosticEntry & diagnostic) = 0;

    /**
     * @brief Copies diagnostic data from the storage buffer to a payload.
     *
     * This method retrieves the stored diagnostic data and copies it into the
     * provided `payload` buffer. If the buffer is too small to hold all the data,
     * still method returns the successfully copied entries along with an error code
     * indicating that the buffer was insufficient.
     *
     * @param payload       A reference to a `MutableByteSpan` where the retrieved
     *                      diagnostic data will be copied.
     * @param read_entries  A reference to an integer that will hold the total
     *                      number of successfully read diagnostic entries.
     *
     * @retval CHIP_NO_ERROR             If the operation succeeded and all data was copied.
     * @retval CHIP_ERROR                 If any other failure occurred during the operation.
     */
    virtual CHIP_ERROR Retrieve(MutableByteSpan & payload, uint32_t & read_entries) = 0;

    /**
     * @brief Checks if the diagnostic storage buffer is empty.
     *
     * This method checks whether the buffer contains any stored diagnostic data.
     *
     * @return bool  Returns `true` if the buffer contains no stored data,
     *               or `false` if the buffer has data.
     */
    virtual bool IsBufferEmpty() = 0;

    /**
     * @brief Retrieves the size of the data currently stored in the diagnostic buffer.
     *
     * This method returns the total size (in bytes) of all diagnostic data that is
     * currently stored in the buffer.
     *
     * @return uint32_t  The size (in bytes) of the stored diagnostic data.
     */
    virtual uint32_t GetDataSize() = 0;

    /**
     * @brief Clears entire buffer
     */
    virtual CHIP_ERROR ClearBuffer() = 0;

    /**
     * @brief Clears buffer up to the specified number of entries
     */
    virtual CHIP_ERROR ClearBuffer(uint32_t entries) = 0;
};

} // namespace Diagnostics
} // namespace Tracing
} // namespace chip