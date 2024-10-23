#include <algorithm>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <memory>
#include <tracing/backend.h>
#include <tracing/esp32_diagnostic_trace/counter.h>
#include <tracing/esp32_diagnostic_trace/diagnostic_tracing.h>
#include <tracing/metric_event.h>

namespace chip {
namespace Tracing {
namespace Insights {

#define LOG_HEAP_INFO(label, group, entry_exit)                                                                                    \
    do                                                                                                                             \
    {                                                                                                                              \
        ESP_DIAG_EVENT("MTR_TRC", "%s - %s - %s", entry_exit, label, group);                                                       \
    } while (0)

constexpr size_t kPermitListMaxSize = CONFIG_MAX_PERMIT_LIST_SIZE;
using HashValue                     = uint32_t;

// Implements a murmurhash with 0 seed.
uint32_t MurmurHash(const void * key)
{
    const uint32_t kMultiplier = 0x5bd1e995;
    const uint32_t kShift      = 24;
    const unsigned char * data = (const unsigned char *) key;
    uint32_t hash              = 0;

    while (*data)
    {
        uint32_t value = *data++;
        value *= kMultiplier;
        value ^= value >> kShift;
        value *= kMultiplier;
        hash *= kMultiplier;
        hash ^= value;
    }

    hash ^= hash >> 13;
    hash *= kMultiplier;
    hash ^= hash >> 15;

    if (hash == 0)
    {
        ESP_LOGW("Tracing", "MurmurHash resulted in a hash value of 0");
    }

    return hash;
}

HashValue gPermitList[kPermitListMaxSize] = { MurmurHash("PASESession"),
                                              MurmurHash("CASESession"),
                                              MurmurHash("NetworkCommissioning"),
                                              MurmurHash("GeneralCommissioning"),
                                              MurmurHash("OperationalCredentials"),
                                              MurmurHash("CASEServer"),
                                              MurmurHash("Fabric") }; // namespace

bool IsPermitted(HashValue hashValue)
{
    for (HashValue permitted : gPermitList)
    {
        if (permitted == 0)
        {
            break;
        }
        if (hashValue == permitted)
        {
            return true;
        }
    }
    return false;
}

void ESP32Diagnostics::LogMessageReceived(MessageReceivedInfo & info) {}

void ESP32Diagnostics::LogMessageSend(MessageSendInfo & info) {}

void ESP32Diagnostics::LogNodeLookup(NodeLookupInfo & info) {}

void ESP32Diagnostics::LogNodeDiscovered(NodeDiscoveredInfo & info) {}

void ESP32Diagnostics::LogNodeDiscoveryFailed(NodeDiscoveryFailedInfo & info) {}

void ESP32Diagnostics::LogMetricEvent(const MetricEvent & event) {
    InMemoryDiagnosticStorage & diagnosticStorage = InMemoryDiagnosticStorage::GetInstance();
    CHIP_ERROR error = CHIP_NO_ERROR;

    printf("LOG MATRIC EVENT CALLED\n");

    Metric<int32_t> metric; // Declare metric before the switch statement

    switch (event.ValueType()) {
    case ValueType::kInt32:
        ESP_LOGI("mtr", "The value of %s is %ld ", event.key(), event.ValueInt32());
        metric = Metric<int32_t>(event.key(), event.ValueInt32(), esp_log_timestamp());
        error = diagnosticStorage.Store(metric);
        break;

    case ValueType::kUInt32:
        ESP_LOGI("mtr", "The value of %s is %lu ", event.key(), event.ValueUInt32());
        break;

    case ValueType::kChipErrorCode:
        ESP_LOGI("mtr", "The value of %s is error with code %lu ", event.key(), event.ValueErrorCode());
        break;

    case ValueType::kUndefined:
        ESP_LOGI("mtr", "The value of %s is undefined", event.key());
        break;

    default:
        ESP_LOGI("mtr", "The value of %s is of an UNKNOWN TYPE", event.key());
        break;
    }

    if (error != CHIP_NO_ERROR) {
        ChipLogError(DeviceLayer, "Failed to store trace matric data");
    }
}

void ESP32Diagnostics::TraceCounter(const char * label)
{
    ::Insights::ESPDiagnosticCounter::GetInstance(label)->ReportMetrics();
}

void ESP32Diagnostics::TraceBegin(const char * label, const char * group)
{
    CHIP_ERROR error;
    HashValue hashValue = MurmurHash(group);
    InMemoryDiagnosticStorage & diagnosticStorage = InMemoryDiagnosticStorage::GetInstance();
    if (IsPermitted(hashValue))
    {
        Trace trace(label, group, esp_log_timestamp());
        diagnosticStorage.Store(trace);
        value++;
    }

}

void ESP32Diagnostics::TraceEnd(const char * label, const char * group) {}

void ESP32Diagnostics::TraceInstant(const char * label, const char * group) {}

} // namespace Insights
} // namespace Tracing
} // namespace chip
