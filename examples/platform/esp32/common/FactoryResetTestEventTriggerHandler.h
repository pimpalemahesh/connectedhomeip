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

#pragma once

#include <app/TestEventTriggerDelegate.h>

namespace chip {
namespace DeviceLayer {

/**
 * @brief ESP32 platform-specific test event trigger handler for factory reset operations
 *
 * This handler provides a test event trigger that can be used to initiate
 * a factory reset operation via the TestEventTrigger command in the
 * General Diagnostics cluster on ESP32 platforms.
 */
class FactoryResetTestEventTriggerHandler : public TestEventTriggerHandler
{
public:
    static constexpr uint64_t kFactoryResetTrigger = CONFIG_FACTORY_RESET_TRIGGER_VALUE;

    /**
     * @brief Handle the test event trigger
     *
     * @param eventTrigger The event trigger value to handle
     * @return CHIP_NO_ERROR if the trigger was handled successfully
     * @return CHIP_ERROR_INVALID_ARGUMENT if the trigger is not recognized
     */
    CHIP_ERROR HandleEventTrigger(uint64_t eventTrigger) override;
};

} // namespace DeviceLayer
} // namespace chip
