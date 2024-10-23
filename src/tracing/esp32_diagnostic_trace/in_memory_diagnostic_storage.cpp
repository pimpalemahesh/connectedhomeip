#include "in_memory_diagnostic_storage.h"
#include "diagnostics.h"
#include <cstring> // For strcmp
#include <esp_log.h>
#include <lib/core/CHIPError.h>
#include <lib/support/logging/CHIPLogging.h>
namespace chip {
namespace Tracing {

InMemoryDiagnosticStorage::InMemoryDiagnosticStorage() :
    mEndUserCircularBuffer(mEndUserBuffer, END_USER_BUFFER_SIZE), mNetworkCircularBuffer(mNetworkBuffer, NETWORK_BUFFER_SIZE)
{}

InMemoryDiagnosticStorage::~InMemoryDiagnosticStorage() {}

CHIP_ERROR InMemoryDiagnosticStorage::Store(Diagnostics & diagnostic)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    CircularTLVWriter writer;
    writer.Init(mEndUserCircularBuffer);

    // Start a TLV structure container (Anonymous)
    chip::TLV::TLVType outerContainer;
    err = writer.StartContainer(AnonymousTag(), chip::TLV::TLVType::kTLVType_Structure, outerContainer);
    VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                        ChipLogError(DeviceLayer, "Failed to start TLV container for metric : %s", chip::ErrorStr(err)));

    err = diagnostic.Encode(writer);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "Failed to encode diagnostic data : %s", ErrorStr(err));
        err = CHIP_ERROR_INVALID_ARGUMENT;
        writer.EndContainer(outerContainer);
        writer.Finalize();
        return err;
    }
    err = writer.EndContainer(outerContainer);
    VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                        ChipLogError(DeviceLayer, "Failed to end TLV container for metric : %s", chip::ErrorStr(err)));

    err = writer.Finalize();
    VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to finalize TLV writing"));

    printf("Total Data Length in Buffer : %lu\n Total available length in buffer : %lu\nTotal buffer length : %lu\n",
           mEndUserCircularBuffer.DataLength(), mEndUserCircularBuffer.AvailableDataLength(),
           mEndUserCircularBuffer.GetTotalDataLength());
    return CHIP_NO_ERROR;
}

CHIP_ERROR InMemoryDiagnosticStorage::Retrieve(MutableByteSpan & payload)
{
    printf("***************************************************************************RETRIEVAL "
           "STARTED**********************************************************\n");
    CHIP_ERROR err = CHIP_NO_ERROR;
    chip::TLV::TLVReader reader;
    reader.Init(mEndUserCircularBuffer);

    chip::TLV::TLVWriter writer;
    writer.Init(payload);

    uint32_t totalBufferSize = 0;

    chip::TLV::TLVType outWriterContainer;
    err = writer.StartContainer(AnonymousTag(), chip::TLV::TLVType::kTLVType_List, outWriterContainer);
    VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to start container"));

    while (true)
    {
        err = reader.Next();
        if (err == CHIP_ERROR_END_OF_TLV)
        {
            ChipLogProgress(DeviceLayer, "No more data to read");
            break;
        }
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to read next TLV element: %s", chip::ErrorStr(err)));

        if (reader.GetType() == chip::TLV::TLVType::kTLVType_Structure && reader.GetTag() == chip::TLV::AnonymousTag())
        {
            chip::TLV::TLVType outerReaderContainer;
            err = reader.EnterContainer(outerReaderContainer);
            VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                                ChipLogError(DeviceLayer, "Failed to enter outer TLV container: %s", chip::ErrorStr(err)));

            err = reader.Next();
            VerifyOrReturnError(
                err == CHIP_NO_ERROR, err,
                ChipLogError(DeviceLayer, "Failed to read next TLV element in outer container: %s", chip::ErrorStr(err)));

            // Check if the current element is a METRIC or TRACE container
            if ((reader.GetType() == chip::TLV::kTLVType_Structure) &&
                (reader.GetTag() == ContextTag(TAG::METRIC) || reader.GetTag() == ContextTag(TAG::TRACE)))
            {
                ESP_LOGW("SIZE", "Total read till now: %ld Total write till now: %ld", reader.GetLengthRead(), writer.GetLengthWritten());

                if ((reader.GetLengthRead() - writer.GetLengthWritten()) < writer.GetRemainingFreeLength()) {
                    err = writer.CopyElement(reader);
                    if (err == CHIP_ERROR_BUFFER_TOO_SMALL) {
                        ChipLogProgress(DeviceLayer, "Buffer too small to occupy current element");
                        break;
                    }
                    VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to copy TLV element"));
                    ChipLogProgress(DeviceLayer, "Read metric container successfully");
                    mEndUserCircularBuffer.EvictHead();
                }
                else {
                    ChipLogProgress(DeviceLayer, "Buffer too small to occupy current TLV");
                    break;
                }
            }
            else
            {
                ChipLogError(DeviceLayer, "Unexpected TLV element in outer container");
                reader.ExitContainer(outerReaderContainer);
                return CHIP_ERROR_WRONG_TLV_TYPE;
            }
            // Exit the outer container
            err = reader.ExitContainer(outerReaderContainer);
            VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                                ChipLogError(DeviceLayer, "Failed to exit outer TLV container: %s", chip::ErrorStr(err)));

            
        }
        else
        {
            ChipLogError(DeviceLayer, "Unexpected TLV element type or tag in outer container");
        }

        printf("Total Data Length in Buffer: %lu\n Total available length in buffer: %lu\nTotal buffer length: %lu\n",
               mEndUserCircularBuffer.DataLength(), mEndUserCircularBuffer.AvailableDataLength(),
               mEndUserCircularBuffer.GetTotalDataLength());
    }

    err = writer.EndContainer(outWriterContainer);
    VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to close outer container"));
    // Finalize the writing process
    err = writer.Finalize();
    VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to finalize TLV writing"));
    payload.reduce_size(writer.GetLengthWritten());
    printf("---------------Total written bytes successfully : %ld----------------\n", writer.GetLengthWritten());
    ChipLogProgress(DeviceLayer, "Retrieval successful");
    return CHIP_NO_ERROR;
}

bool InMemoryDiagnosticStorage::IsEmptyBuffer()
{
    return mEndUserCircularBuffer.DataLength() == 0;
}

} // namespace Tracing
} // namespace chip
