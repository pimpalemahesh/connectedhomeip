/*
 *
 *    Copyright (c) 2024 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

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

    // Start outer container
    chip::TLV::TLVType outerContainer;
    err = writer.StartContainer(AnonymousTag(), chip::TLV::kTLVType_Structure, outerContainer);
    VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                        ChipLogError(DeviceLayer, "Failed to start TLV container for diagnostic: %s", chip::ErrorStr(err)));

    // Handle different diagnostic types
    err = StoreDiagnosticData(writer, diagnostic);
    VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                        ChipLogError(DeviceLayer, "Failed to store diagnostic data: %s", chip::ErrorStr(err)));

    // End outer container
    err = writer.EndContainer(outerContainer);
    VerifyOrReturnError(err == CHIP_NO_ERROR, err,
                        ChipLogError(DeviceLayer, "Failed to end TLV container for diagnostic: %s", chip::ErrorStr(err)));

    // Finalize writing process
    err = writer.Finalize();
    VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to finalize TLV writing"));

    LogBufferStats();
    return CHIP_NO_ERROR;
}

CHIP_ERROR InMemoryDiagnosticStorage::StoreDiagnosticData(CircularTLVWriter & writer, Diagnostics & diagnostic)
{
    if (strcmp(diagnostic.GetType(), "METRIC") == 0)
    {
        return StoreMetric(writer, static_cast<const Metric<int32_t> *>(&diagnostic));
    }
    else if (strcmp(diagnostic.GetType(), "TRACE") == 0)
    {
        return StoreTrace(writer, static_cast<const Trace *>(&diagnostic));
    }
    else if (strcmp(diagnostic.GetType(), "COUNTER") == 0)
    {
        return StoreCounter(writer, static_cast<const Counter *>(&diagnostic));
    }
    else
    {
        ChipLogError(DeviceLayer, "Unknown diagnostic type: %s", diagnostic.GetType());
        return CHIP_ERROR_INVALID_ARGUMENT;
    }
}

CHIP_ERROR InMemoryDiagnosticStorage::StoreMetric(CircularTLVWriter & writer, const Metric<int32_t> * metric)
{
    chip::TLV::TLVType metricContainer;
    CHIP_ERROR err = writer.StartContainer(ContextTag(TAG::METRIC), chip::TLV::kTLVType_Structure, metricContainer);
    VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to start METRIC container: %s", chip::ErrorStr(err)));

    // Write LABEL, VALUE, and TIMESTAMP
    err = writer.PutString(ContextTag(TAG::LABEL), metric->GetLabel());
    VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to write LABEL: %s", chip::ErrorStr(err)));
    err = writer.Put(ContextTag(TAG::VALUE), metric->GetValue());
    VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to write VALUE: %s", chip::ErrorStr(err)));
    err = writer.Put(ContextTag(TAG::TIMESTAMP), metric->GetTimestamp());
    VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to write TIMESTAMP: %s", chip::ErrorStr(err)));

    printf("Metric Value written to storage successfully: label : %s value : %ld timestamp : %lu\n", metric->GetLabel(), metric->GetValue(), metric->GetTimestamp());
    return writer.EndContainer(metricContainer);
}

CHIP_ERROR InMemoryDiagnosticStorage::StoreTrace(CircularTLVWriter & writer, const Trace * trace)
{
    chip::TLV::TLVType traceContainer;
    CHIP_ERROR err = writer.StartContainer(ContextTag(TAG::TRACE), chip::TLV::kTLVType_Structure, traceContainer);
    VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to start TRACE container: %s", chip::ErrorStr(err)));

    // Write LABEL and TIMESTAMP
    err = writer.PutString(ContextTag(TAG::LABEL), trace->GetLabel());
    VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to write LABEL: %s", chip::ErrorStr(err)));
    err = writer.Put(ContextTag(TAG::TIMESTAMP), trace->GetTimestamp());
    VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to write TIMESTAMP: %s", chip::ErrorStr(err)));

    printf("Trace Value written to storage successfully: label : %s timestamp : %lu\n", trace->GetLabel(), trace->GetTimestamp());
    return writer.EndContainer(traceContainer);
}

CHIP_ERROR InMemoryDiagnosticStorage::StoreCounter(CircularTLVWriter & writer, const Counter * counter)
{
    chip::TLV::TLVType counterContainer;
    CHIP_ERROR err = writer.StartContainer(ContextTag(TAG::COUNTER), chip::TLV::kTLVType_Structure, counterContainer);
    VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to start COUNTER container: %s", chip::ErrorStr(err)));

    // Write LABEL, COUNT, and TIMESTAMP
    err = writer.PutString(ContextTag(TAG::LABEL), counter->GetLabel());
    VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to write LABEL: %s", chip::ErrorStr(err)));
    err = writer.Put(ContextTag(TAG::COUNTER), counter->GetCount());
    VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to write COUNT: %s", chip::ErrorStr(err)));
    err = writer.Put(ContextTag(TAG::TIMESTAMP), counter->GetTimestamp());
    VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to write TIMESTAMP: %s", chip::ErrorStr(err)));

    printf("Counter Value written to storage successfully: label : %s value : %ld timestamp : %lu\n", counter->GetLabel(), counter->GetCount(), counter->GetTimestamp());
    return writer.EndContainer(counterContainer);
}

void InMemoryDiagnosticStorage::LogBufferStats()
{
    printf("Total Data Length in Buffer: %lu\n Total available length in buffer: %lu\n Total buffer length: %lu\n",
           mEndUserCircularBuffer.DataLength(), mEndUserCircularBuffer.AvailableDataLength(),
           mEndUserCircularBuffer.GetTotalDataLength());
}

CHIP_ERROR InMemoryDiagnosticStorage::Retrieve(MutableByteSpan payload)
{
    printf("***************************************************************************RETRIEVAL STARTED**********************************************************\n");
    CircularTLVReader reader;
    reader.Init(mEndUserCircularBuffer);

    chip::TLV::TLVWriter writer;
    writer.Init(payload);

    // Start writing output container
    chip::TLV::TLVType outWriterContainer;
    CHIP_ERROR err = writer.StartContainer(AnonymousTag(), chip::TLV::kTLVType_Structure, outWriterContainer);
    VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to start container"));

    // Read from buffer and copy to output
    err = ReadAndCopyData(reader, writer);
    VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to read and copy data"));

    // End and finalize the writing
    err = writer.EndContainer(outWriterContainer);
    VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to end TLV container"));
    err = writer.Finalize();
    VerifyOrReturnError(err == CHIP_NO_ERROR, err, ChipLogError(DeviceLayer, "Failed to finalize TLV writing"));

    LogBufferStats();
    ChipLogProgress(DeviceLayer, "Retrieval successful");
    return CHIP_NO_ERROR;
}

CHIP_ERROR InMemoryDiagnosticStorage::ReadAndCopyData(CircularTLVReader & reader, chip::TLV::TLVWriter & writer)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    while (true) {
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

            err = reader.Next();
            VerifyOrReturnError(
                err == CHIP_NO_ERROR, err,
                ChipLogError(DeviceLayer, "Failed to read next TLV element in outer container: %s", chip::ErrorStr(err)));

            // Check if the current element is a METRIC or TRACE or COUNTER container
            if ((reader.GetType() == chip::TLV::kTLVType_Structure) &&
                (reader.GetTag() == ContextTag(TAG::METRIC) || reader.GetTag() == ContextTag(TAG::TRACE) || reader.GetTag() == ContextTag(TAG::COUNTER)))
            {
                err = writer.CopyElement(reader);
                if (err == CHIP_ERROR_BUFFER_TOO_SMALL) {
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
        }

        LogBufferStats();
    }
    return CHIP_NO_ERROR;
}

bool InMemoryDiagnosticStorage::IsEmptyBuffer()
{
    return mEndUserCircularBuffer.DataLength() == 0;
}

} // namespace Tracing
} // namespace chip
