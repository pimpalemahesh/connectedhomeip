#include <lib/core/CHIPError.h>
#include <tracing/backend.h>
#include <tracing/metric_event.h>
#include <tracing/esp32_diagnostic_trace/diagnostic_storage.h>


#include <memory>
namespace chip {
namespace Tracing {
namespace Insights {

extern DiagnosticStorage gDiagnosticStorage;

/// A Backend that outputs data to chip logging.
///
/// Structured data is formatted as json strings.
class ESP32Diagnostics : public ::chip::Tracing::Backend
{
public:
    ESP32Diagnostics()
    {
        // Additional initialization if necessary
    }

    // Deleted copy constructor and assignment operator to prevent copying
    ESP32Diagnostics(const ESP32Diagnostics&) = delete;
    ESP32Diagnostics& operator=(const ESP32Diagnostics&) = delete;

    void TraceBegin(const char * label, const char * group) override;

    void TraceEnd(const char * label, const char * group) override;

    /// Trace a zero-sized event
    void TraceInstant(const char * label, const char * group) override;

    void TraceCounter(const char * label) override;

    void LogMessageSend(MessageSendInfo &) override;
    void LogMessageReceived(MessageReceivedInfo &) override;

    void LogNodeLookup(NodeLookupInfo &) override;
    void LogNodeDiscovered(NodeDiscoveredInfo &) override;
    void LogNodeDiscoveryFailed(NodeDiscoveryFailedInfo &) override;
    void LogMetricEvent(const MetricEvent &) override;

private:
    using ValueType = MetricEvent::Value::Type;
    uint16_t value = 0;
};

} // namespace Insights
} // namespace Tracing
} // namespace chip