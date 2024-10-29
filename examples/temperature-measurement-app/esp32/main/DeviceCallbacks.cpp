/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
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

/**
 * @file DeviceCallbacks.cpp
 *
 * Implements all the callbacks to the application from the CHIP Stack
 *
 **/
#include "DeviceCallbacks.h"
#include <esp_log.h>
#include <tracing/macros.h>
#include <tracing/metric_event.h>

int32_t current_temperature = 20;
int32_t average_temperature = 20;
int32_t peak_temperature = 20;
uint32_t uptime = 0;
uint32_t error_rate = 0;

static const char TAG[] = "echo-devicecallbacks";

using namespace ::chip;
using namespace ::chip::Inet;
using namespace ::chip::System;

void update_diagnostics(uint32_t time) {
    current_temperature = 15 + (rand() % 20);
    
    average_temperature = (average_temperature * uptime + current_temperature) / (uptime + 1);
    
    if (current_temperature > peak_temperature) {
        peak_temperature = current_temperature;
    }

    uptime = time;

    // Randomize error rate by incrementing randoml
    error_rate += rand() % 2;
  }
  

void diagnostics_timer_callback(uint32_t timestamp) {
    update_diagnostics(timestamp);

    MATTER_TRACE_COUNTER("TemperatureUpdateCount");

    MATTER_LOG_METRIC(chip::Tracing::kMetricCurrentTemp, current_temperature);
    MATTER_LOG_METRIC(chip::Tracing::kMetricAverageTemp, average_temperature);
    MATTER_LOG_METRIC(chip::Tracing::kMetricPeakTemp, peak_temperature);
}

void AppDeviceCallbacks::PostAttributeChangeCallback(EndpointId endpointId, ClusterId clusterId, AttributeId attributeId,
                                                     uint8_t type, uint16_t size, uint8_t * value)
{
    ESP_LOGI(TAG, "PostAttributeChangeCallback - Cluster ID: '0x%" PRIx32 "', EndPoint ID: '0x%x', Attribute ID: '0x%" PRIx32 "'",
             clusterId, endpointId, attributeId);

    // TODO handle this callback in switch statement
    ESP_LOGI(TAG, "Unhandled cluster ID: %" PRIu32, clusterId);

    ESP_LOGI(TAG, "Current free heap: %d\n", heap_caps_get_free_size(MALLOC_CAP_8BIT));

    diagnostics_timer_callback(esp_log_timestamp());
}
