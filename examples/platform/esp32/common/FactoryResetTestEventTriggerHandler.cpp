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

#include "FactoryResetTestEventTriggerHandler.h"

#include <app/server/Server.h>
#include <platform/ESP32/ESP32Utils.h>
#include <platform/PlatformManager.h>

using namespace chip;
using namespace chip::DeviceLayer;

static const char TAG[] = "FactoryResetHandler";

CHIP_ERROR FactoryResetTestEventTriggerHandler::HandleEventTrigger(uint64_t eventTrigger)
{
    eventTrigger = clearEndpointInEventTrigger(eventTrigger);

    if (eventTrigger == kFactoryResetTrigger)
    {
        chip::Server::GetInstance().ScheduleFactoryReset();

        return CHIP_NO_ERROR;
    }

    return CHIP_ERROR_INVALID_ARGUMENT;
}
