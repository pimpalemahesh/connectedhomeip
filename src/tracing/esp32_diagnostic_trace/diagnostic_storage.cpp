#include "diagnostic_storage.h"

namespace chip {
namespace Tracing {

DiagnosticStorage::DiagnosticStorage() : mCircularBuffer(mBuffer, TRACE_BUFFER_SIZE) {}

DiagnosticStorage::~DiagnosticStorage() {}

CHIP_ERROR DiagnosticStorage::Serialize()
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    printf("%s\n", __func__);
    return err;
}

// Deserialize Method
CHIP_ERROR DiagnosticStorage::Deserialize()
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    printf("%s\n", __func__);
    return err;
}

CHIP_ERROR DiagnosticStorage::StoreData(const char* key, uint16_t value)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    CircularTLVWriter writer;
    writer.Init(mCircularBuffer);

    // Start a TLV structure container (Anonymous)
    chip::TLV::TLVType outerContainer;
    err = writer.StartContainer(chip::TLV::AnonymousTag(), chip::TLV::kTLVType_Structure, outerContainer);
    if (err != CHIP_NO_ERROR) {
        ChipLogError(DeviceLayer, "Failed to start TLV container");
        return err;
    }

    // Write the key string with a context tag
    err = writer.PutString(chip::TLV::ContextTag(1), key);
    if (err != CHIP_NO_ERROR) {
        ChipLogError(DeviceLayer, "Failed to write key to TLV");
        return err;
    }

    // Write the uint16_t value with a context tag
    err = writer.Put(ContextTag(2), value);
    if (err != CHIP_NO_ERROR) {
        ChipLogError(DeviceLayer, "Failed to write value to TLV");
        return err;
    }

    // End the TLV structure container
    err = writer.EndContainer(outerContainer);
    if (err != CHIP_NO_ERROR) {
        ChipLogError(DeviceLayer, "Failed to end TLV container");
        return err;
    }

    // Finalize the writing process
    err = writer.Finalize();
    if (err != CHIP_NO_ERROR) {
        ChipLogError(DeviceLayer, "Failed to finalize TLV writing");
        return err;
    }

    printf("---------------Written Key: %s, Value: %u successfully---------------\n", key, value);

    return CHIP_NO_ERROR;
}

CHIP_ERROR DiagnosticStorage::RetrieveData(ByteSpan payload)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    CircularTLVReader reader;
    reader.Init(mCircularBuffer);

    size_t dataSize = 0;
    size_t maxSize = payload.size();
    uint8_t* buffer = const_cast<uint8_t*>(payload.data());

    // Retrieve elements up to RETRIEVE_DATA_CHUNK_NUMBER entries
    for (size_t count = 0; count < RETRIEVE_DATA_CHUNK_NUMBER; ++count) {
        err = reader.Next();
        if (err == CHIP_ERROR_END_OF_TLV) {
            ChipLogError(DeviceLayer, "No more data to read");
            break;
        }
        if (err != CHIP_NO_ERROR) {
            ChipLogError(DeviceLayer, "Failed to read TLV element: %s", chip::ErrorStr(err));
            return err;
        }

        // Check if the current element is a structure with an anonymous tag
        if (reader.GetType() == chip::TLV::kTLVType_Structure && reader.GetTag() == chip::TLV::AnonymousTag()) {
            chip::TLV::TLVType outerContainer;
            err = reader.EnterContainer(outerContainer);
            if (err != CHIP_NO_ERROR) {
                ChipLogError(DeviceLayer, "Failed to enter TLV container: %s", chip::ErrorStr(err));
                return err;
            }

            // Read the key string (ContextTag(1))
            err = reader.Next();
            if (err != CHIP_NO_ERROR) {
                ChipLogError(DeviceLayer, "Failed to move to key element: %s", chip::ErrorStr(err));
                reader.ExitContainer(outerContainer);
                return err;
            }

            char *key = nullptr;

            uint32_t keyLength = reader.GetLength() + 1;
            key = (char *) chip::Platform::MemoryAlloc(keyLength);

            if (reader.GetTag() == chip::TLV::ContextTag(1)) {
                err = reader.Expect(chip::TLV::TLVType::kTLVType_UTF8String, chip::TLV::ContextTag(1));
                if (err != CHIP_NO_ERROR) {
                    ChipLogError(DeviceLayer, "UnExpected TLV element. Expected String type");
                    return err;
                }
                
                err = reader.GetString(key, keyLength); 
                if (err != CHIP_NO_ERROR) {
                    ChipLogError(DeviceLayer, "Failed to read key string from TLV: %s", chip::ErrorStr(err));
                    reader.ExitContainer(outerContainer);
                    return err;
                }
            } else {
                ChipLogError(DeviceLayer, "Unexpected Context Tag for key");
                reader.ExitContainer(outerContainer);
                return CHIP_ERROR_WRONG_TLV_TYPE;
            }

            err = reader.Next();
            if (err != CHIP_NO_ERROR) {
                ChipLogError(DeviceLayer, "Failed to move to value element: %s", chip::ErrorStr(err));
                reader.ExitContainer(outerContainer);
                return err;
            }

            uint32_t value;
            if (reader.GetTag() == chip::TLV::ContextTag(2)) {
                err = reader.Get(value);
                if (err != CHIP_NO_ERROR) {
                    ChipLogError(DeviceLayer, "Failed to read value from TLV: %s", chip::ErrorStr(err));
                    reader.ExitContainer(outerContainer);
                    return err;
                }
            } else {
                ChipLogError(DeviceLayer, "Unexpected Context Tag for value");
                reader.ExitContainer(outerContainer);
                return CHIP_ERROR_WRONG_TLV_TYPE;
            }

            err = reader.ExitContainer(outerContainer);
            if (err != CHIP_NO_ERROR) {
                ChipLogError(DeviceLayer, "Failed to exit TLV container: %s", chip::ErrorStr(err));
                return err;
            }

            // Serialize the key and value into the payload buffer
            size_t keyLen = strlen(key);
            size_t valueSize = sizeof(value);
            // Calculate required space: key + ':' + value + '\n'
            size_t requiredSize = keyLen + 1 + valueSize + 1;

            if (dataSize + requiredSize > maxSize) {
                ChipLogError(DeviceLayer, "Payload buffer too small to hold all retrieved data");
                return CHIP_ERROR_BUFFER_TOO_SMALL;
            }

            memcpy(buffer + dataSize, key, keyLen);
            dataSize += keyLen;

            buffer[dataSize++] = ':';

            memcpy(buffer + dataSize, &value, valueSize);
            dataSize += valueSize;

            buffer[dataSize++] = '\n';

            printf("--------------- Key: %s, Value: %lu into payload---------------\n", key, value);
            chip::Platform::MemoryFree(key);
            mCircularBuffer.EvictHead();
        }
        else {
            ChipLogError(DeviceLayer, "Unexpected TLV type or tag: Type, Tag");
            return CHIP_ERROR_WRONG_TLV_TYPE;
        }
    }

    // Assign the payload with the size of the data written
    payload = ByteSpan(buffer, dataSize);

    printf("Retrieved data size: %d bytes\n", static_cast<int>(dataSize));

    return CHIP_NO_ERROR;
}

bool DiagnosticStorage::IsEmptyBuffer()
{
    return false;
}


} // namespace Tracing
} // namespace chip
