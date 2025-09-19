/*
 *
 *    Copyright (c) 2025 Project CHIP Authors
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

#include <tracing/esp32_diagnostic_trace/ESPFlashStorage.h>
#include <tracing/esp32_diagnostic_trace/DiagnosticEntry.h>

#include <esp_partition.h>
#include <esp_flash.h>
#include <lib/core/CHIPError.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <lib/core/TLVWriter.h>
#include <lib/core/TLVReader.h>
#include <lib/support/CHIPMem.h>

using namespace chip::TLV;

namespace chip {
namespace Tracing {
namespace Diagnostics {

// Flash storage header with wear leveling support
struct FlashStorageHeader
{
    uint32_t magic;           // Magic number to identify valid data
    uint32_t sequence;        // Sequence number for wear leveling (higher = newer)
    uint32_t writeOffset;     // Current write position in partition
    uint32_t entryCount;      // Number of diagnostic entries stored
    uint32_t generation;      // Generation counter for wrap-around detection
    uint32_t oldestOffset;    // Offset of oldest entry (for circular buffer)
    uint32_t reserved[2];     // Reserved for future use
    uint32_t crc32;          // CRC32 of header (excluding this field)
};

// Entry header for each diagnostic entry
struct EntryHeader
{
    uint32_t magic;          // Entry magic number
    uint32_t length;         // Length of TLV data following this header (unaligned)
    uint32_t alignedLength;  // Aligned length including padding
    uint32_t dataCrc32;     // CRC32 of aligned data (including padding)
    uint32_t sequence;      // Entry sequence number
    uint32_t reserved[2];   // Reserved for future use - set to 0xFFFFFFFF
    uint32_t headerCrc32;   // CRC32 of this header (excluding this field)
};

static constexpr uint32_t kFlashStorageMagic = 0xDEADBEEF;
static constexpr uint32_t kEntryMagic = 0xCAFEBABE;
static constexpr uint32_t kEntryPending = 0xEEEEEEEE;
static constexpr uint32_t kInvalidMagic = 0x00000000;
static constexpr size_t kFlashStorageHeaderSize = sizeof(FlashStorageHeader);
static constexpr size_t kEntryHeaderSize = sizeof(EntryHeader);
static constexpr size_t kSectorSize = 4096; // ESP32 flash sector size
static constexpr size_t kMaxEntrySize = 1024; // Maximum size for a single entry
static constexpr size_t kWriteAlignment = 4; // ESP32 requires 4-byte aligned writes
static constexpr size_t kHeaderCopies = 4; // Number of header copies for wear leveling
static constexpr size_t kHeaderSectorSize = kSectorSize; // Reserve one sector for headers
static constexpr size_t kMinPartitionSize = kHeaderSectorSize + kSectorSize; // Minimum viable partition
static constexpr size_t kStreamBufferSize = 512; // Buffer size for streaming operations
static constexpr bool kUseStaticBuffer = true; // Use static buffer to avoid malloc fragmentation

// Static buffer for entry data to avoid heap fragmentation
static uint8_t s_entryBuffer[kMaxEntrySize + kWriteAlignment] __attribute__((aligned(4)));

// CRC32 calculation (simple implementation)
static uint32_t CalculateCRC32(const uint8_t * data, size_t length)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
        {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

// Align size to write alignment boundary
static size_t AlignSize(size_t size)
{
    return (size + kWriteAlignment - 1) & ~(kWriteAlignment - 1);
}

ESPFlashDiagnosticStorage::ESPFlashDiagnosticStorage(const char * partitionName) : mPartitionName(partitionName)
{
    // Constructor - validation happens on first use
}

// Find the latest valid header among multiple copies
static CHIP_ERROR FindLatestHeader(const esp_partition_t * partition, FlashStorageHeader & header, size_t & headerOffset)
{
    FlashStorageHeader bestHeader = {};
    size_t bestOffset = 0;
    uint32_t bestSequence = 0;
    bool foundValid = false;
    
    // Check all header copies
    for (size_t i = 0; i < kHeaderCopies; i++)
    {
        size_t offset = i * kFlashStorageHeaderSize;
        if (offset + kFlashStorageHeaderSize > kHeaderSectorSize)
            break;
            
        FlashStorageHeader candidateHeader = {};
        esp_err_t esp_err = esp_partition_read(partition, offset, &candidateHeader, kFlashStorageHeaderSize);
        if (esp_err != ESP_OK)
            continue;
            
        // Validate header
        if (candidateHeader.magic != kFlashStorageMagic)
            continue;
            
        // Verify CRC
        uint32_t headerCrc = CalculateCRC32((const uint8_t*)&candidateHeader, kFlashStorageHeaderSize - sizeof(uint32_t));
        if (headerCrc != candidateHeader.crc32)
            continue;
            
        // Check if this is the newest valid header
        if (!foundValid || candidateHeader.sequence > bestSequence)
        {
            bestHeader = candidateHeader;
            bestOffset = offset;
            bestSequence = candidateHeader.sequence;
            foundValid = true;
        }
    }
    
    if (!foundValid)
    {
        return CHIP_ERROR_NOT_FOUND;
    }
    
    header = bestHeader;
    headerOffset = bestOffset;
    return CHIP_NO_ERROR;
}

// Write a new header copy with wear leveling
static CHIP_ERROR WriteNewHeader(const esp_partition_t * partition, const FlashStorageHeader & header)
{
    // Find next header slot to use
    size_t nextOffset = 0;
    uint32_t nextSequence = header.sequence + 1;
    bool foundEmptySlot = false;
    uint32_t oldestSequence = UINT32_MAX;
    size_t oldestOffset = 0;
    
    // Find an empty slot or track the oldest one
    for (size_t i = 0; i < kHeaderCopies; i++)
    {
        size_t offset = i * kFlashStorageHeaderSize;
        if (offset + kFlashStorageHeaderSize > kHeaderSectorSize)
            break;
            
        FlashStorageHeader existingHeader = {};
        esp_err_t esp_err = esp_partition_read(partition, offset, &existingHeader, kFlashStorageHeaderSize);
        if (esp_err != ESP_OK || existingHeader.magic != kFlashStorageMagic)
        {
            // Found empty slot
            nextOffset = offset;
            foundEmptySlot = true;
            break;
        }
        
        // Track oldest header for potential overwrite
        if (existingHeader.sequence < oldestSequence)
        {
            oldestSequence = existingHeader.sequence;
            oldestOffset = offset;
        }
    }
    
    // If no empty slot found, we need to erase and restart
    if (!foundEmptySlot)
    {
        ChipLogProgress(DeviceLayer, "All header slots full, erasing header sector");
        
        // Erase the entire header sector
        esp_err_t esp_err = esp_partition_erase_range(partition, 0, kHeaderSectorSize);
        if (esp_err != ESP_OK)
        {
            ChipLogError(DeviceLayer, "Failed to erase header sector: %s", esp_err_to_name(esp_err));
            return CHIP_ERROR_WRITE_FAILED;
        }
        
        // Reset to first slot and increment sequence significantly to avoid confusion
        nextOffset = 0;
        nextSequence = header.sequence + kHeaderCopies + 1;
    }
    
    // Create new header with incremented sequence
    FlashStorageHeader newHeader = header;
    newHeader.sequence = nextSequence;
    newHeader.crc32 = CalculateCRC32((const uint8_t*)&newHeader, kFlashStorageHeaderSize - sizeof(uint32_t));
    
    // Write new header
    esp_err_t esp_err = esp_partition_write(partition, nextOffset, &newHeader, kFlashStorageHeaderSize);
    if (esp_err != ESP_OK)
    {
        ChipLogError(DeviceLayer, "Failed to write new header: %s", esp_err_to_name(esp_err));
        return CHIP_ERROR_WRITE_FAILED;
    }
    
    return CHIP_NO_ERROR;
}

// Initialize partition with first header
static CHIP_ERROR InitializePartition(const esp_partition_t * partition)
{
    // Erase header sector
    esp_err_t esp_err = esp_partition_erase_range(partition, 0, kHeaderSectorSize);
    if (esp_err != ESP_OK)
    {
        ChipLogError(DeviceLayer, "Failed to erase header sector: %s", esp_err_to_name(esp_err));
        return CHIP_ERROR_WRITE_FAILED;
    }
    
    // Create initial header
    FlashStorageHeader header = {};
    header.magic = kFlashStorageMagic;
    header.sequence = 1;
    header.writeOffset = kHeaderSectorSize;
    header.entryCount = 0;
    header.generation = 1;
    header.oldestOffset = kHeaderSectorSize;
    header.reserved[0] = 0xFFFFFFFF; // Set to detect corruption
    header.reserved[1] = 0xFFFFFFFF;
    header.crc32 = CalculateCRC32((const uint8_t*)&header, kFlashStorageHeaderSize - sizeof(uint32_t));
    
    // Write initial header
    esp_err = esp_partition_write(partition, 0, &header, kFlashStorageHeaderSize);
    if (esp_err != ESP_OK)
    {
        ChipLogError(DeviceLayer, "Failed to write initial header: %s", esp_err_to_name(esp_err));
        return CHIP_ERROR_WRITE_FAILED;
    }
    
    return CHIP_NO_ERROR;
}

// Mark an entry as invalid (for circular buffer) - robust invalidation
static CHIP_ERROR InvalidateEntry(const esp_partition_t * partition, uint32_t offset)
{
    // Create invalid entry header with multiple markers
    EntryHeader invalidHeader = {};
    invalidHeader.magic = kInvalidMagic;
    invalidHeader.length = 0;
    invalidHeader.alignedLength = 0;
    invalidHeader.dataCrc32 = 0;
    invalidHeader.sequence = 0;
    invalidHeader.reserved[0] = 0;
    invalidHeader.reserved[1] = 0;
    invalidHeader.headerCrc32 = 0;
    
    // Write the entire invalid header to ensure robust invalidation
    esp_err_t esp_err = esp_partition_write(partition, offset, &invalidHeader, kEntryHeaderSize);
    if (esp_err != ESP_OK)
    {
        ChipLogError(DeviceLayer, "Failed to invalidate entry at offset %" PRIu32 ": %s", offset, esp_err_to_name(esp_err));
        return CHIP_ERROR_WRITE_FAILED;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR ESPFlashDiagnosticStorage::Store(const DiagnosticEntry & entry)
{
    // Find the partition
    const esp_partition_t * partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, mPartitionName);
    if (partition == nullptr)
    {
        ChipLogError(DeviceLayer, "Flash partition '%s' not found", mPartitionName);
        return CHIP_ERROR_NOT_FOUND;
    }
    
    // Validate partition size - ensure space for headers plus several max-size entries
    uint32_t minRequiredSize = kHeaderSectorSize + (4 * (kEntryHeaderSize + kMaxEntrySize));
    if (partition->size < minRequiredSize)
    {
        ChipLogError(DeviceLayer, "Partition too small: %" PRIu32 " bytes (minimum %" PRIu32 " for reliable operation)", 
                     partition->size, minRequiredSize);
        return CHIP_ERROR_INVALID_ARGUMENT;
    }
    
    // Find current header
    FlashStorageHeader header = {};
    size_t headerOffset = 0;
    CHIP_ERROR err = FindLatestHeader(partition, header, headerOffset);
    if (err != CHIP_NO_ERROR)
    {
        // No valid header found, initialize partition
        ChipLogProgress(DeviceLayer, "Initializing flash partition '%s'", mPartitionName);
        ReturnErrorOnFailure(InitializePartition(partition));
        ReturnErrorOnFailure(FindLatestHeader(partition, header, headerOffset));
    }
    
    // Two-pass encoding to determine size
    uint32_t encodedSize = 0;
    {
        TLVWriter sizeWriter;
        uint8_t dummyBuffer[1];
        sizeWriter.Init(dummyBuffer, 0);
        
        CHIP_ERROR encodeErr = Encode(sizeWriter, entry);
        if (encodeErr != CHIP_ERROR_BUFFER_TOO_SMALL && encodeErr != CHIP_NO_ERROR)
        {
            return encodeErr;
        }
        encodedSize = sizeWriter.GetLengthWritten();
    }
    
    if (encodedSize == 0 || encodedSize > kMaxEntrySize)
    {
        ChipLogError(DeviceLayer, "Entry size invalid: %" PRIu32, encodedSize);
        return CHIP_ERROR_INVALID_ARGUMENT;
    }
    
    // Calculate aligned size and total entry size
    size_t alignedSize = AlignSize(encodedSize);
    size_t totalEntrySize = kEntryHeaderSize + alignedSize;
    
    // Check if we need to make space (circular buffer behavior)
    uint32_t availableSpace = partition->size - header.writeOffset;
    if (totalEntrySize > availableSpace)
    {
        // Need to wrap around - implement circular buffer
        ChipLogProgress(DeviceLayer, "Partition full, wrapping around (generation %" PRIu32 ")", header.generation + 1);
        
        // Invalidate entries from the oldest until we have enough space
        uint32_t spaceNeeded = totalEntrySize;
        uint32_t currentOffset = header.oldestOffset;
        uint32_t freedSpace = 0;
        uint32_t entriesInvalidated = 0;
        
        // Ensure we free enough space by adding some margin
        uint32_t targetSpace = spaceNeeded + (spaceNeeded / 4); // 25% margin
        
        while (freedSpace < targetSpace && currentOffset < header.writeOffset)
        {
            // Read entry header
            EntryHeader entryHeader = {};
            esp_err_t esp_err = esp_partition_read(partition, currentOffset, &entryHeader, kEntryHeaderSize);
            if (esp_err != ESP_OK)
            {
                ChipLogError(DeviceLayer, "Failed to read entry at offset %" PRIu32 " during wrap", currentOffset);
                break;
            }
            
            // Skip already invalid entries
            if (entryHeader.magic == kInvalidMagic)
            {
                size_t entrySize = kEntryHeaderSize + entryHeader.alignedLength;
                freedSpace += entrySize;
                currentOffset += entrySize;
                continue;
            }
            
            // Validate entry header before processing
            if (entryHeader.magic != kEntryMagic || entryHeader.alignedLength > kMaxEntrySize)
            {
                ChipLogError(DeviceLayer, "Corrupted entry at offset %" PRIu32 " during wrap", currentOffset);
                // Skip to next sector boundary to find next valid entry
                currentOffset = ((currentOffset / kSectorSize) + 1) * kSectorSize;
                continue;
            }
            
            // Invalidate this valid entry
            ReturnErrorOnFailure(InvalidateEntry(partition, currentOffset));
            entriesInvalidated++;
            
            size_t entrySize = kEntryHeaderSize + entryHeader.alignedLength;
            freedSpace += entrySize;
            currentOffset += entrySize;
        }
        
        // Update header for wrap-around
        header.writeOffset = kHeaderSectorSize; // Reset to beginning of data area
        header.generation++;
        header.oldestOffset = currentOffset; // Update to point past invalidated entries
        header.entryCount -= entriesInvalidated;
        
        ChipLogProgress(DeviceLayer, "Invalidated %" PRIu32 " entries, freed %" PRIu32 " bytes", 
                        entriesInvalidated, freedSpace);
    }
    
    // Allocate entry data buffer (static or dynamic)
    uint8_t * entryData = nullptr;
    bool usingStaticBuffer = false;
    
    if (kUseStaticBuffer && alignedSize <= sizeof(s_entryBuffer))
    {
        entryData = s_entryBuffer;
        memset(entryData, 0, alignedSize);
        usingStaticBuffer = true;
    }
    else
    {
        entryData = (uint8_t *) calloc(1, alignedSize);
        if (entryData == nullptr)
        {
            return CHIP_ERROR_NO_MEMORY;
        }
    }
    
    // Encode the entry
    {
        TLVWriter writer;
        writer.Init(entryData, encodedSize);
        
        err = Encode(writer, entry);
        if (err == CHIP_NO_ERROR)
        {
            err = writer.Finalize();
        }
        
        if (err != CHIP_NO_ERROR || writer.GetLengthWritten() != encodedSize)
        {
            if (!usingStaticBuffer) free(entryData);
            ChipLogError(DeviceLayer, "Entry encoding failed");
            return (err != CHIP_NO_ERROR) ? err : CHIP_ERROR_INTERNAL;
        }
    }
    
    // Create entry header with pending status for atomic writes
    EntryHeader entryHeader = {};
    entryHeader.magic = kEntryPending; // Start with pending status
    entryHeader.length = encodedSize;
    entryHeader.alignedLength = alignedSize;
    entryHeader.dataCrc32 = CalculateCRC32(entryData, alignedSize); // CRC includes padding
    // Use bit-shifted generation-aware sequence numbering
    entryHeader.sequence = (header.generation << 20) | ((header.entryCount + 1) & 0xFFFFF);
    entryHeader.reserved[0] = 0xFFFFFFFF; // Set to detect corruption
    entryHeader.reserved[1] = 0xFFFFFFFF;
    // Calculate header CRC (excluding the headerCrc32 field itself)
    entryHeader.headerCrc32 = CalculateCRC32((const uint8_t*)&entryHeader, kEntryHeaderSize - sizeof(uint32_t));
    
    // Write entry atomically: pending header → data → commit header
    esp_err_t esp_err = esp_partition_write(partition, header.writeOffset, &entryHeader, kEntryHeaderSize);
    if (esp_err == ESP_OK)
    {
        // Write data
        esp_err = esp_partition_write(partition, header.writeOffset + kEntryHeaderSize, entryData, alignedSize);
        if (esp_err == ESP_OK)
        {
            // Commit the entry by updating magic to valid
            uint32_t validMagic = kEntryMagic;
            esp_err = esp_partition_write(partition, header.writeOffset, &validMagic, sizeof(validMagic));
        }
    }
    
    if (!usingStaticBuffer) free(entryData);
    
    if (esp_err != ESP_OK)
    {
        ChipLogError(DeviceLayer, "Failed to write entry: %s", esp_err_to_name(esp_err));
        return CHIP_ERROR_WRITE_FAILED;
    }
    
    // Update header and write new copy (atomic header update)
    header.writeOffset += totalEntrySize;
    header.entryCount++;
    
    err = WriteNewHeader(partition, header);
    if (err != CHIP_NO_ERROR)
    {
        return err;
    }
    
    ChipLogProgress(DeviceLayer, "Stored entry to partition '%s', total entries: %" PRIu32 " (gen %" PRIu32 ")", 
                    mPartitionName, header.entryCount, header.generation);
    
    return CHIP_NO_ERROR;
}

CHIP_ERROR ESPFlashDiagnosticStorage::Retrieve(MutableByteSpan & span, uint32_t & read_entries)
{
    read_entries = 0;
    
    // Find the partition
    const esp_partition_t * partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, mPartitionName);
    if (partition == nullptr)
    {
        ChipLogError(DeviceLayer, "Flash partition '%s' not found", mPartitionName);
        return CHIP_ERROR_NOT_FOUND;
    }
    
    // Find current header
    FlashStorageHeader header = {};
    size_t headerOffset = 0;
    CHIP_ERROR err = FindLatestHeader(partition, header, headerOffset);
    if (err != CHIP_NO_ERROR)
    {
        // No valid data
        span.reduce_size(0);
        return CHIP_NO_ERROR;
    }
    
    if (header.entryCount == 0)
    {
        // No entries
        span.reduce_size(0);
        return CHIP_NO_ERROR;
    }
    
    // Stream entries into output buffer
    TLVWriter writer;
    writer.Init(span.data(), span.size());
    
    uint32_t currentOffset = header.oldestOffset; // Start from oldest valid entry
    uint32_t successful_written_bytes = 0;
    uint32_t entriesProcessed = 0;
    bool wrappedAround = false;
    
    // Scan from oldestOffset, wrapping around if needed
    uint32_t maxIterations = header.entryCount * 2; // Safety limit to prevent infinite loops
    uint32_t iterations = 0;
    
    while (entriesProcessed < header.entryCount && iterations < maxIterations)
    {
        iterations++;
        
        // Handle wrap-around at partition boundary
        if (currentOffset >= partition->size)
        {
            if (wrappedAround)
            {
                // Already wrapped once, stop to avoid infinite loop
                ChipLogError(DeviceLayer, "Wrapped around twice, stopping to prevent infinite loop");
                break;
            }
            currentOffset = kHeaderSectorSize;
            wrappedAround = true;
            continue;
        }
        
        // Read entry header
        EntryHeader entryHeader = {};
        esp_err_t esp_err = esp_partition_read(partition, currentOffset, &entryHeader, kEntryHeaderSize);
        if (esp_err != ESP_OK)
        {
            ChipLogError(DeviceLayer, "Failed to read entry header at offset %" PRIu32, currentOffset);
            break;
        }
        
        // Skip invalid entries
        if (entryHeader.magic == kInvalidMagic)
        {
            // Skip to next sector boundary to find next entry
            currentOffset = ((currentOffset / kSectorSize) + 1) * kSectorSize;
            continue;
        }
        
        // Skip pending entries (incomplete writes)
        if (entryHeader.magic == kEntryPending)
        {
            ChipLogProgress(DeviceLayer, "Skipping pending entry at offset %" PRIu32 " (incomplete write)", currentOffset);
            currentOffset += kEntryHeaderSize + (entryHeader.alignedLength > 0 ? entryHeader.alignedLength : kWriteAlignment);
            continue;
        }
        
        // Validate entry header
        if (entryHeader.magic != kEntryMagic || entryHeader.length == 0 || 
            entryHeader.length > kMaxEntrySize || entryHeader.alignedLength < entryHeader.length)
        {
            ChipLogError(DeviceLayer, "Invalid entry header at offset %" PRIu32, currentOffset);
            currentOffset += kEntryHeaderSize; // Skip this header and continue
            continue;
        }
        
        // Validate header CRC
        uint32_t expectedHeaderCrc = CalculateCRC32((const uint8_t*)&entryHeader, kEntryHeaderSize - sizeof(uint32_t));
        if (expectedHeaderCrc != entryHeader.headerCrc32)
        {
            ChipLogError(DeviceLayer, "Entry header CRC mismatch at offset %" PRIu32, currentOffset);
            currentOffset += kEntryHeaderSize; // Skip this corrupted header
            continue;
        }
        
        // Read entry data
        uint8_t * entryData = (uint8_t *) malloc(entryHeader.alignedLength);
        if (entryData == nullptr)
        {
            err = CHIP_ERROR_NO_MEMORY;
            break;
        }
        
        esp_err = esp_partition_read(partition, currentOffset + kEntryHeaderSize, entryData, entryHeader.alignedLength);
        if (esp_err != ESP_OK)
        {
            free(entryData);
            ChipLogError(DeviceLayer, "Failed to read entry data at offset %" PRIu32, currentOffset);
            currentOffset += kEntryHeaderSize + entryHeader.alignedLength;
            continue;
        }
        
        // Verify entry data CRC
        uint32_t dataCrc = CalculateCRC32(entryData, entryHeader.alignedLength);
        if (dataCrc != entryHeader.dataCrc32)
        {
            free(entryData);
            ChipLogError(DeviceLayer, "Entry data CRC mismatch at offset %" PRIu32, currentOffset);
            currentOffset += kEntryHeaderSize + entryHeader.alignedLength;
            continue;
        }
        
        // Parse and copy TLV entry
        TLVReader reader;
        reader.Init(entryData, entryHeader.length); // Use unaligned length for TLV
        
        CHIP_ERROR copyErr = reader.Next();
        if (copyErr == CHIP_NO_ERROR && reader.GetType() == kTLVType_Structure && reader.GetTag() == AnonymousTag())
        {
            copyErr = writer.CopyElement(reader);
            if (copyErr == CHIP_NO_ERROR)
            {
                successful_written_bytes = writer.GetLengthWritten();
                read_entries++;
                entriesProcessed++;
            }
            else if (copyErr == CHIP_ERROR_BUFFER_TOO_SMALL)
            {
                // Output buffer full - this is expected
                free(entryData);
                ChipLogProgress(DeviceLayer, "Output buffer full after %" PRIu32 " entries", read_entries);
                break;
            }
            else
            {
                free(entryData);
                ChipLogError(DeviceLayer, "Error copying TLV element: %" CHIP_ERROR_FORMAT, copyErr.Format());
                err = copyErr;
                break;
            }
        }
        else
        {
            ChipLogError(DeviceLayer, "Invalid TLV structure in entry at offset %" PRIu32, currentOffset);
            // Continue to next entry instead of failing
        }
        
        free(entryData);
        currentOffset += kEntryHeaderSize + entryHeader.alignedLength;
    }
    
    // Finalize output if we have entries
    if (read_entries > 0 && err == CHIP_NO_ERROR)
    {
        err = writer.Finalize();
        if (err != CHIP_NO_ERROR)
        {
            return err;
        }
    }
    
    span.reduce_size(successful_written_bytes);
    ChipLogProgress(DeviceLayer, "Retrieved %" PRIu32 " bytes (%" PRIu32 " entries) from partition '%s'", 
                    successful_written_bytes, read_entries, mPartitionName);
    
    return err;
}

bool ESPFlashDiagnosticStorage::IsBufferEmpty()
{
    // Find the partition
    const esp_partition_t * partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, mPartitionName);
    if (partition == nullptr)
    {
        return true; // No partition means empty
    }
    
    // Find current header
    FlashStorageHeader header = {};
    size_t headerOffset = 0;
    CHIP_ERROR err = FindLatestHeader(partition, header, headerOffset);
    if (err != CHIP_NO_ERROR)
    {
        return true; // No valid header means empty
    }
    
    return (header.entryCount == 0);
}

uint32_t ESPFlashDiagnosticStorage::GetDataSize()
{
    // Find the partition
    const esp_partition_t * partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, mPartitionName);
    if (partition == nullptr)
    {
        return 0;
    }
    
    // Find current header
    FlashStorageHeader header = {};
    size_t headerOffset = 0;
    CHIP_ERROR err = FindLatestHeader(partition, header, headerOffset);
    if (err != CHIP_NO_ERROR)
    {
        return 0;
    }
    
    // Return actual data usage (not including header sector)
    if (header.writeOffset > kHeaderSectorSize)
    {
        return header.writeOffset - kHeaderSectorSize;
    }
    return 0;
}

CHIP_ERROR ESPFlashDiagnosticStorage::ClearBuffer()
{
    // Find the partition
    const esp_partition_t * partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, mPartitionName);
    if (partition == nullptr)
    {
        ChipLogError(DeviceLayer, "Flash partition '%s' not found", mPartitionName);
        return CHIP_ERROR_NOT_FOUND;
    }
    
    // Simply reinitialize the partition
    CHIP_ERROR err = InitializePartition(partition);
    if (err == CHIP_NO_ERROR)
    {
        ChipLogProgress(DeviceLayer, "Cleared flash partition '%s'", mPartitionName);
    }
    return err;
}

CHIP_ERROR ESPFlashDiagnosticStorage::ClearBuffer(uint32_t entries)
{
    if (entries == 0)
    {
        return CHIP_NO_ERROR;
    }
    
    // Find the partition
    const esp_partition_t * partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, mPartitionName);
    if (partition == nullptr)
    {
        ChipLogError(DeviceLayer, "Flash partition '%s' not found", mPartitionName);
        return CHIP_ERROR_NOT_FOUND;
    }
    
    // Find current header
    FlashStorageHeader header = {};
    size_t headerOffset = 0;
    CHIP_ERROR err = FindLatestHeader(partition, header, headerOffset);
    if (err != CHIP_NO_ERROR)
    {
        // No data to clear
        return CHIP_NO_ERROR;
    }
    
    if (header.entryCount == 0)
    {
        // No entries to clear
        return CHIP_NO_ERROR;
    }
    
    if (entries >= header.entryCount)
    {
        // Clear all entries
        return ClearBuffer();
    }
    
    // Invalidate the specified number of oldest entries starting from oldestOffset
    uint32_t currentOffset = header.oldestOffset;
    uint32_t entriesInvalidated = 0;
    uint32_t newOldestOffset = header.oldestOffset;
    bool wrappedAround = false;
    
    // Scan for valid entries and invalidate the oldest ones
    while (entriesInvalidated < entries)
    {
        // Handle wrap-around at partition boundary
        if (currentOffset >= partition->size)
        {
            if (wrappedAround)
            {
                // Already wrapped once, stop to avoid infinite loop
                break;
            }
            currentOffset = kHeaderSectorSize;
            wrappedAround = true;
        }
        
        // Read entry header
        EntryHeader entryHeader = {};
        esp_err_t esp_err = esp_partition_read(partition, currentOffset, &entryHeader, kEntryHeaderSize);
        if (esp_err != ESP_OK)
        {
            break;
        }
        
        if (entryHeader.magic == kInvalidMagic)
        {
            // Skip invalid entry - move to next entry
            // Ensure we skip at least the header size to avoid infinite loop
            size_t entrySize = kEntryHeaderSize + (entryHeader.alignedLength > 0 ? entryHeader.alignedLength : kWriteAlignment);
            currentOffset += entrySize;
            continue;
        }
        
        if (entryHeader.magic == kEntryMagic)
        {
            // Invalidate this entry
            err = InvalidateEntry(partition, currentOffset);
            if (err != CHIP_NO_ERROR)
            {
                return err;
            }
            entriesInvalidated++;
            
            // Move to next entry and update newOldestOffset
            size_t entrySize = kEntryHeaderSize + entryHeader.alignedLength;
            currentOffset += entrySize;
            newOldestOffset = currentOffset;
        }
        else
        {
            // Corrupted entry, skip to next sector boundary
            currentOffset = ((currentOffset / kSectorSize) + 1) * kSectorSize;
        }
    }
    
    if (entriesInvalidated > 0)
    {
        // Update header with new oldest offset
        header.entryCount -= entriesInvalidated;
        header.oldestOffset = newOldestOffset;
        err = WriteNewHeader(partition, header);
        if (err != CHIP_NO_ERROR)
        {
            return err;
        }
        
        ChipLogProgress(DeviceLayer, "Invalidated %" PRIu32 " entries from partition '%s', remaining: %" PRIu32 ", new oldest offset: %" PRIu32, 
                        entriesInvalidated, mPartitionName, header.entryCount, newOldestOffset);
    }
    
    return CHIP_NO_ERROR;
}

} // namespace Diagnostics
} // namespace Tracing
} // namespace chip
