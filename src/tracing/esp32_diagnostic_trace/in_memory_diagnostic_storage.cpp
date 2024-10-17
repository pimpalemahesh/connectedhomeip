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
    err = writer.StartContainer(AnonymousTag(), chip::TLV::kTLVType_Structure, outerContainer);
    VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                        ChipLogError(DeviceLayer, "Failed to start TLV container for metric : %s", chip::ErrorStr(err)));

    // Serialize based on diagnostic type
    if (strcmp(diagnostic.GetType(), "METRIC") == 0)
    {
        chip::TLV::TLVType metricContainer;
        const Metric<int32_t> * metric = static_cast<const Metric<int32_t> *>(&diagnostic);
        err = writer.StartContainer(ContextTag(TAG::METRIC), chip::TLV::kTLVType_Structure, metricContainer);
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to start TLV container for metric : %s", chip::ErrorStr(err)));

        // LABEL
        err = writer.PutString(ContextTag(TAG::LABEL), metric->GetLabel());
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to write LABEL for METRIC : %s", chip::ErrorStr(err)));

        // VALUE
        err = writer.Put(ContextTag(TAG::VALUE), metric->GetValue());
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to write VALUE for METRIC : %s", chip::ErrorStr(err)));

        // TIMESTAMP
        err = writer.Put(ContextTag(TAG::TIMESTAMP), metric->GetTimestamp());
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to write TIMESTAMP for METRIC : %s", chip::ErrorStr(err)));

        printf("Metric Value written to storage successfully : %s\n", metric->GetLabel());
        // End the TLV structure container
        err = writer.EndContainer(metricContainer);
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to end TLV container for metric : %s", chip::ErrorStr(err)));
    }
    else if (strcmp(diagnostic.GetType(), "TRACE") == 0)
    {
        chip::TLV::TLVType traceContainer;
        const Trace * trace = static_cast<const Trace *>(&diagnostic);

        // Starting a TLV structure container for TRACE
        err = writer.StartContainer(ContextTag(TAG::TRACE), chip::TLV::kTLVType_Structure, traceContainer);
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to start TLV container for Trace: %s", chip::ErrorStr(err)));

        // LABEL
        err = writer.PutString(ContextTag(TAG::LABEL), trace->GetLabel());
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to write LABEL for TRACE : %s", chip::ErrorStr(err)));

        // TIMESTAMP
        err = writer.Put(ContextTag(TAG::TIMESTAMP), trace->GetTimestamp());
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to write TIMESTAMP for METRIC : %s", chip::ErrorStr(err)));

        printf("Trace Value written to storage successfully : %s\n", trace->GetLabel());
        // End the TLV structure container
        err = writer.EndContainer(traceContainer);
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to end TLV container for Trace : %s", chip::ErrorStr(err)));
    }
    else if (strcmp(diagnostic.GetType(), "COUNTER") == 0)
    {
        chip::TLV::TLVType counterContainer;
        const Counter * counter = static_cast<const Counter *>(&diagnostic);

        // Starting a TLV structure container for COUNTER
        err = writer.StartContainer(ContextTag(TAG::COUNTER), chip::TLV::kTLVType_Structure, counterContainer);
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to start TLV container for Counter: %s", chip::ErrorStr(err)));

        // LABEL
        err = writer.PutString(ContextTag(TAG::LABEL), counter->GetLabel());
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to write LABEL for COUNTER : %s", chip::ErrorStr(err)));

        // COUNT
        err = writer.Put(ContextTag(TAG::COUNTER), counter->GetCount());
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to write VALUE for COUNTER : %s", chip::ErrorStr(err)));

        // TIMESTAMP
        err = writer.Put(ContextTag(TAG::TIMESTAMP), counter->GetTimestamp());
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to write TIMESTAMP for COUNTER : %s", chip::ErrorStr(err)));

        printf("Counter Value written to storage successfully : %s\n", counter->GetLabel());
        // End the TLV structure container
        err = writer.EndContainer(counterContainer);
        VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                            ChipLogError(DeviceLayer, "Failed to end TLV container for counter : %s", chip::ErrorStr(err)));
    }
    else
    {
        ChipLogError(DeviceLayer, "Unknown diagnostic type: %s", diagnostic.GetType());
        err = CHIP_ERROR_INVALID_ARGUMENT;
        writer.EndContainer(outerContainer); // Ensure container is ended
        writer.Finalize();
        return err;
    }
    err = writer.EndContainer(outerContainer);
    VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                        ChipLogError(DeviceLayer, "Failed to end TLV container for metric : %s", chip::ErrorStr(err)));

    // Finalize the writing process
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
    CircularTLVReader reader;
    reader.Init(mEndUserCircularBuffer);

    chip::TLV::TLVWriter writer;
    writer.Init(payload);

    uint32_t totalBufferSize = mEndUserCircularBuffer.DataLength();

    chip::TLV::TLVType outWriterContainer;
    err = writer.StartContainer(AnonymousTag(), chip::TLV::kTLVType_List, outWriterContainer);
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

        // Check if the current element is a structure with an anonymous tag
        if (reader.GetType() == chip::TLV::kTLVType_Structure && reader.GetTag() == chip::TLV::AnonymousTag())
        {
            chip::TLV::TLVType outerReaderContainer;
            err = reader.EnterContainer(outerReaderContainer);
            VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                                ChipLogError(DeviceLayer, "Failed to enter outer TLV container: %s", chip::ErrorStr(err)));
            if (reader.GetLength() > writer.GetRemainingFreeLength())
            {
                err = writer.EndContainer(outWriterContainer);
                err = writer.Finalize();
                VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to finalize TLV writing"));

                payload.reduce_size(writer.GetLengthWritten());
                return CHIP_NO_ERROR;
            }

            err = reader.Next();
            VerifyOrReturnError(
                err == CHIP_NO_ERROR, err,
                ChipLogError(DeviceLayer, "Failed to read next TLV element in outer container: %s", chip::ErrorStr(err)));

            // Check if the current element is a METRIC or TRACE container
            if ((reader.GetType() == chip::TLV::kTLVType_Structure) &&
                (reader.GetTag() == ContextTag(TAG::METRIC) || reader.GetTag() == ContextTag(TAG::TRACE)))
            {
                err = writer.CopyElement(reader);
                if (err == CHIP_ERROR_BUFFER_TOO_SMALL)
                {
                    ChipLogProgress(DeviceLayer, "Buffer too small to occupy current element");
                    break;
                }
                VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to copy TLV element"));
                printf("Read metric container successfully\n");
                mEndUserCircularBuffer.EvictHead();
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
            // Optionally, you might want to skip or handle unexpected elements differently
        }

        printf("Total Data Length in Buffer: %lu\n Total available length in buffer: %lu\nTotal buffer length: %lu\n",
               mEndUserCircularBuffer.DataLength(), mEndUserCircularBuffer.AvailableDataLength(),
               mEndUserCircularBuffer.GetTotalDataLength());
    }

    err = writer.EndContainer(outWriterContainer);
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
