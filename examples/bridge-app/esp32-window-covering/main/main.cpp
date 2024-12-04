/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
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

#include "Device.h"
#include "DeviceCallbacks.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/ConcreteAttributePath.h>
#include <app/clusters/identify-server/identify-server.h>
#include <app/reporting/reporting.h>
#include <app/server/OnboardingCodesUtil.h>
#include <app/util/attribute-storage.h>
#include <app/util/endpoint-config-api.h>
#include <common/Esp32AppServer.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <lib/core/CHIPError.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/CHIPMemString.h>
#include <lib/support/ZclString.h>
#include <platform/ESP32/ESP32Utils.h>

#include <app/InteractionModelEngine.h>
#include <app/server/Server.h>

#if CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER
#include <platform/ESP32/ESP32FactoryDataProvider.h>
#endif // CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER

#if CONFIG_ENABLE_ESP32_DEVICE_INFO_PROVIDER
#include <platform/ESP32/ESP32DeviceInfoProvider.h>
#else
#include <DeviceInfoProviderImpl.h>
#endif // CONFIG_ENABLE_ESP32_DEVICE_INFO_PROVIDER

namespace {
#if CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER
chip::DeviceLayer::ESP32FactoryDataProvider sFactoryDataProvider;
#endif // CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER

#if CONFIG_ENABLE_ESP32_DEVICE_INFO_PROVIDER
chip::DeviceLayer::ESP32DeviceInfoProvider gExampleDeviceInfoProvider;
#else
chip::DeviceLayer::DeviceInfoProviderImpl gExampleDeviceInfoProvider;
#endif // CONFIG_ENABLE_ESP32_DEVICE_INFO_PROVIDER
} // namespace

extern const char TAG[] = "bridge-app";

using namespace ::chip;
using namespace ::chip::DeviceManager;
using namespace ::chip::Platform;
using namespace ::chip::Credentials;
using namespace ::chip::app::Clusters;
using namespace chip::app::Clusters::WindowCovering;

static AppDeviceCallbacks AppCallback;

static const int kNodeLabelSize = 32;
// Current ZCL implementation of Struct uses a max-size array of 254 bytes
static const int kDescriptorAttributeArraySize = 254;

static EndpointId gCurrentEndpointId;
static EndpointId gFirstDynamicEndpointId;
static Device * gDevices[CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT]; // number of dynamic endpoints count
static Device gWindows("Windows1", "Office");

uint8_t restart_timers;
uint16_t window_data;
bool window_up = false;

// (taken from chip-devices.xml)
#define DEVICE_TYPE_BRIDGED_NODE 0x0013

#define DEVICE_TYPE_WINCOVER 0x0202

// (taken from chip-devices.xml)
#define DEVICE_TYPE_ROOT_NODE 0x0016
// (taken from chip-devices.xml)
#define DEVICE_TYPE_BRIDGE 0x000e

// Device Version for dynamic endpoints:
#define DEVICE_VERSION_DEFAULT 1

/* BRIDGED DEVICE ENDPOINT: contains the following clusters:
   - On/Off
   - Descriptor
   - Bridged Device Basic Information
*/

DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(WindowsAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(WindowCovering::Attributes::Type::Id, INT8U, 1, ATTRIBUTE_MASK_WRITABLE),
    DECLARE_DYNAMIC_ATTRIBUTE(WindowCovering::Attributes::TargetPositionTiltPercent100ths::Id, INT16U, 2, ATTRIBUTE_MASK_WRITABLE),
    DECLARE_DYNAMIC_ATTRIBUTE(WindowCovering::Attributes::TargetPositionLiftPercent100ths::Id, INT16U, 2, ATTRIBUTE_MASK_WRITABLE),
    DECLARE_DYNAMIC_ATTRIBUTE(WindowCovering::Attributes::ConfigStatus::Id, BITMAP8, 1, 0),
    DECLARE_DYNAMIC_ATTRIBUTE(WindowCovering::Attributes::OperationalStatus::Id, BITMAP8, 1, ATTRIBUTE_MASK_WRITABLE),
    DECLARE_DYNAMIC_ATTRIBUTE(WindowCovering::Attributes::EndProductType::Id, ENUM8, 1, ATTRIBUTE_MASK_WRITABLE),
    DECLARE_DYNAMIC_ATTRIBUTE(WindowCovering::Attributes::Mode::Id, BITMAP8, 1, ATTRIBUTE_MASK_WRITABLE),
    DECLARE_DYNAMIC_ATTRIBUTE(WindowCovering::Attributes::CurrentPositionLiftPercent100ths::Id, INT16U, 2, ATTRIBUTE_MASK_WRITABLE),
    DECLARE_DYNAMIC_ATTRIBUTE(WindowCovering::Attributes::FeatureMap::Id, BITMAP32, 4, ATTRIBUTE_MASK_WRITABLE),
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Declare Descriptor cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(descriptorAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::DeviceTypeList::Id, ARRAY, kDescriptorAttributeArraySize, 0), /* device list */
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ServerList::Id, ARRAY, kDescriptorAttributeArraySize, 0), /* server list */
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ClientList::Id, ARRAY, kDescriptorAttributeArraySize, 0), /* client list */
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::PartsList::Id, ARRAY, kDescriptorAttributeArraySize, 0),  /* parts list */
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Declare Bridged Device Basic Information cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(bridgedDeviceBasicAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::NodeLabel::Id, CHAR_STRING, kNodeLabelSize, 0), /* NodeLabel */
    DECLARE_DYNAMIC_ATTRIBUTE(BridgedDeviceBasicInformation::Attributes::Reachable::Id, BOOLEAN, 1, 0),              /* Reachable */
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

constexpr CommandId windoscoverCommands[] = {
    app::Clusters::WindowCovering::Commands::UpOrOpen::Id,
    app::Clusters::WindowCovering::Commands::DownOrClose::Id,
    app::Clusters::WindowCovering::Commands::StopMotion::Id,
    app::Clusters::WindowCovering::Commands::GoToLiftPercentage::Id,
    app::Clusters::WindowCovering::Commands::GoToTiltPercentage::Id,
    kInvalidCommandId,
};

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(bridgedWindwsClusters)
DECLARE_DYNAMIC_CLUSTER(WindowCovering::Id, WindowsAttrs, ZAP_CLUSTER_MASK(SERVER), windoscoverCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(BridgedDeviceBasicInformation::Id, bridgedDeviceBasicAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr,
                            nullptr) DECLARE_DYNAMIC_CLUSTER_LIST_END;

// Declare Bridged Light endpoint
DECLARE_DYNAMIC_ENDPOINT(bridgedWindowsEndpoint, bridgedWindwsClusters);
DataVersion gWindowsVersions[ArraySize(bridgedWindwsClusters)];

/* REVISION definitions:
 */

#define ZCL_DESCRIPTOR_CLUSTER_REVISION (1u)
#define ZCL_BRIDGED_DEVICE_BASIC_INFORMATION_CLUSTER_REVISION (2u)
#define ZCL_FIXED_LABEL_CLUSTER_REVISION (1u)
#define ZCL_WINDOVER_CLUSTER_VERSION (4u)

int AddDeviceEndpoint(Device * dev, EmberAfEndpointType * ep, const Span<const EmberAfDeviceType> & deviceTypeList,
                      const Span<DataVersion> & dataVersionStorage, chip::EndpointId parentEndpointId)
{
    uint8_t index = 0;
    while (index < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    {
        if (NULL == gDevices[index])
        {
            gDevices[index] = dev;
            CHIP_ERROR err;
            while (true)
            {
                dev->SetEndpointId(gCurrentEndpointId);
                err =
                    emberAfSetDynamicEndpoint(index, gCurrentEndpointId, ep, dataVersionStorage, deviceTypeList, parentEndpointId);
                if (err == CHIP_NO_ERROR)
                {
                    ChipLogProgress(DeviceLayer, "Added device %s to dynamic endpoint %d (index=%d)", dev->GetName(),
                                    gCurrentEndpointId, index);
                    return index;
                }
                else if (err != CHIP_ERROR_ENDPOINT_EXISTS)
                {
                    return -1;
                }
                // Handle wrap condition
                if (++gCurrentEndpointId < gFirstDynamicEndpointId)
                {
                    gCurrentEndpointId = gFirstDynamicEndpointId;
                }
            }
        }
        index++;
    }
    ChipLogProgress(DeviceLayer, "Failed to add dynamic endpoint: No endpoints available!");
    return -1;
}

CHIP_ERROR RemoveDeviceEndpoint(Device * dev)
{
    for (uint8_t index = 0; index < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT; index++)
    {
        if (gDevices[index] == dev)
        {
            // Silence complaints about unused ep when progress logging
            // disabled.
            [[maybe_unused]] EndpointId ep = emberAfClearDynamicEndpoint(index);
            gDevices[index]                = NULL;
            ChipLogProgress(DeviceLayer, "Removed device %s from dynamic endpoint %d (index=%d)", dev->GetName(), ep, index);
            return CHIP_NO_ERROR;
        }
    }
    return CHIP_ERROR_INTERNAL;
}

/**
 * HandleReadWindowsAttribute
 */
Protocols::InteractionModel::Status HandleReadWindowsAttribute(Device * dev, chip::AttributeId attributeId, uint8_t * buffer,
                                                               uint16_t maxReadLength)
{
    ChipLogProgress(DeviceLayer, "HandleReadWindowsAttribute: attrId=%" PRIu32 ", maxReadLength=%u", attributeId, maxReadLength);

    uint16_t rev = 0;
    if ((attributeId == WindowCovering::Attributes::TargetPositionTiltPercent100ths::Id))
    {
        printf("TargetPositionTiltPercent100ths%d\r\n", *buffer);
        dev->WindowsCoverOn();
        // memcpy(buffer, &rev, sizeof(rev));
    }
    else if (attributeId == WindowCovering::Attributes::Type::Id)
    {
        dev->WindowsCoverOn();
        printf("Type%d\r\n", *buffer);
        uint8_t ret = 0;
        memcpy(buffer, &ret, sizeof(ret));
    }
    else if (attributeId == WindowCovering::Attributes::ConfigStatus::Id)
    {
        printf("ConfigStatus%d\r\n", *buffer);
        dev->WindowsCoverOn();
        *buffer = 1;
    }
    else if (attributeId == WindowCovering::Attributes::OperationalStatus::Id)
    {
        printf("OperationalStatus%d\r\n", *buffer);
        dev->WindowsCoverOn();
        uint8_t ret = 0;
        memcpy(buffer, &ret, sizeof(ret));
    }
    else if (attributeId == WindowCovering::Attributes::EndProductType::Id)
    {
        printf("EndProductType%d\r\n", *buffer);
        dev->WindowsCoverOn();
        uint8_t ret = 0;
        memcpy(buffer, &ret, sizeof(ret));
    }
    else if (attributeId == WindowCovering::Attributes::Mode::Id)
    {
        printf("Mode%d\r\n", *buffer);
        dev->WindowsCoverOn();
        uint8_t ret = 1;
        memcpy(buffer, &ret, sizeof(ret));
    }
    else if (attributeId == WindowCovering::Attributes::TargetPositionLiftPercent100ths::Id)
    {

        dev->WindowsCoverOn();
        uint16_t Lift_P = dev->GetCurrentPositionLiftPercent100ths();
        memcpy(buffer, &Lift_P, sizeof(Lift_P));
    }
    else if (attributeId == WindowCovering::Attributes::CurrentPositionLiftPercent100ths::Id)
    {
        printf("CurrentPositionLiftPercent100ths%d\r\n", *buffer);
        dev->WindowsCoverOn();
        uint16_t Lift_V = dev->GetCurrentPositionLiftPercent100ths();
        memcpy(buffer, &Lift_V, sizeof(Lift_V));
    }
    else if (attributeId == WindowCovering::Attributes::FeatureMap::Id)
    {
        rev = 31;
        memcpy(buffer, &rev, sizeof(rev));
    }
    else if ((attributeId == WindowCovering::Attributes::ClusterRevision::Id))
    {
        rev = ZCL_WINDOVER_CLUSTER_VERSION;
        memcpy(buffer, &rev, sizeof(rev));
    }
    else
    {
        return Protocols::InteractionModel::Status::Failure;
    }
    return Protocols::InteractionModel::Status::Success;
}
/**
 * HandleWriteWindowsAttribute
 */
Protocols::InteractionModel::Status HandleWriteWindowsAttribute(Device * dev, chip::AttributeId attributeId, uint8_t * buffer)
{
    ChipLogProgress(DeviceLayer, "Windows write----------------------: attrId=%" PRIu32, attributeId);
    if (attributeId == WindowCovering::Attributes::TargetPositionLiftPercent100ths::Id ||
        attributeId == WindowCovering::Attributes::TargetPositionTiltPercent100ths::Id ||
        attributeId == WindowCovering::Attributes::CurrentPositionLiftPercent100ths::Id)
    {

        uint16_t target = *(buffer + 1);
        target <<= 8;
        target |= *buffer;
        printf("recv:%d\r\n", target);
        dev->GoToLiftPercentage100ths(target);
        window_data = target;
        if (!window_up)
            window_up = true;
        return Protocols::InteractionModel::Status::Success;
    }
    else if (attributeId == WindowCovering::Attributes::OperationalStatus::Id)
    {
        return Protocols::InteractionModel::Status::Success;
    }
    else
        return Protocols::InteractionModel::Status::Failure;
}

Protocols::InteractionModel::Status HandleReadBridgedDeviceBasicAttribute(Device * dev, chip::AttributeId attributeId,
                                                                          uint8_t * buffer, uint16_t maxReadLength)
{
    using namespace BridgedDeviceBasicInformation::Attributes;
    ChipLogProgress(DeviceLayer, "HandleReadBridgedDeviceBasicAttribute: attrId=%" PRIu32 ", maxReadLength=%u", attributeId,
                    maxReadLength);

    if ((attributeId == Reachable::Id) && (maxReadLength == 1))
    {
        *buffer = dev->IsReachable() ? 1 : 0;
    }
    else if ((attributeId == NodeLabel::Id) && (maxReadLength == 32))
    {
        MutableByteSpan zclNameSpan(buffer, maxReadLength);
        MakeZclCharString(zclNameSpan, dev->GetName());
    }
    else if ((attributeId == ClusterRevision::Id) && (maxReadLength == 2))
    {
        uint16_t rev = ZCL_BRIDGED_DEVICE_BASIC_INFORMATION_CLUSTER_REVISION;
        memcpy(buffer, &rev, sizeof(rev));
    }
    else
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status emberAfExternalAttributeReadCallback(EndpointId endpoint, ClusterId clusterId,
                                                                         const EmberAfAttributeMetadata * attributeMetadata,
                                                                         uint8_t * buffer, uint16_t maxReadLength)
{
    uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(endpoint);

    if ((endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT) && (gDevices[endpointIndex] != NULL))
    {
        Device * dev = gDevices[endpointIndex];

        if (clusterId == BridgedDeviceBasicInformation::Id)
        {
            return HandleReadBridgedDeviceBasicAttribute(dev, attributeMetadata->attributeId, buffer, maxReadLength);
        }
        else if (clusterId == WindowCovering::Id)
        {
            return HandleReadWindowsAttribute(dev, attributeMetadata->attributeId, buffer, maxReadLength);
        }
    }

    return Protocols::InteractionModel::Status::Failure;
}

Protocols::InteractionModel::Status emberAfExternalAttributeWriteCallback(EndpointId endpoint, ClusterId clusterId,
                                                                          const EmberAfAttributeMetadata * attributeMetadata,
                                                                          uint8_t * buffer)
{
    uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(endpoint);

    if (endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    {
        Device * dev = gDevices[endpointIndex];

        if ((dev->IsReachable()) && (clusterId == WindowCovering::Id))
        {
            return HandleWriteWindowsAttribute(dev, attributeMetadata->attributeId, buffer);
        }
    }

    return Protocols::InteractionModel::Status::Failure;
}

namespace {
void CallReportingCallback(intptr_t closure)
{
    auto path = reinterpret_cast<app::ConcreteAttributePath *>(closure);
    MatterReportingAttributeChangeCallback(*path);
    Platform::Delete(path);
}

void ScheduleReportingCallback(Device * dev, ClusterId cluster, AttributeId attribute)
{
    auto * path = Platform::New<app::ConcreteAttributePath>(dev->GetEndpointId(), cluster, attribute);
    DeviceLayer::PlatformMgr().ScheduleWork(CallReportingCallback, reinterpret_cast<intptr_t>(path));
}
} // anonymous namespace

void HandleDeviceStatusChanged(Device * dev, Device::Changed_t itemChangedMask)
{
    if (itemChangedMask & Device::kChanged_Reachable)
    {
        ScheduleReportingCallback(dev, BridgedDeviceBasicInformation::Id, BridgedDeviceBasicInformation::Attributes::Reachable::Id);
    }

    if (itemChangedMask & Device::kChanged_Windows)
    {
        ScheduleReportingCallback(dev, WindowCovering::Id, WindowCovering::Attributes::CurrentPositionLiftPercent100ths::Id);
    }

    if (itemChangedMask & Device::kChanged_Name)
    {
        ScheduleReportingCallback(dev, BridgedDeviceBasicInformation::Id, BridgedDeviceBasicInformation::Attributes::NodeLabel::Id);
    }
}

bool emberAfActionsClusterInstantActionCallback(app::CommandHandler * commandObj, const app::ConcreteCommandPath & commandPath,
                                                const Actions::Commands::InstantAction::DecodableType & commandData)
{
    // No actions are implemented, just return status NotFound.
    commandObj->AddStatus(commandPath, Protocols::InteractionModel::Status::NotFound);
    return true;
}

const EmberAfDeviceType gRootDeviceTypes[]          = { { DEVICE_TYPE_ROOT_NODE, DEVICE_VERSION_DEFAULT } };
const EmberAfDeviceType gAggregateNodeDeviceTypes[] = { { DEVICE_TYPE_BRIDGE, DEVICE_VERSION_DEFAULT } };
const EmberAfDeviceType gBridgedwindoscoverTypes[]  = { { DEVICE_TYPE_WINCOVER, DEVICE_VERSION_DEFAULT },
                                                        { DEVICE_TYPE_BRIDGED_NODE, DEVICE_VERSION_DEFAULT } };

static void InitServer(intptr_t context)
{
    PrintOnboardingCodes(chip::RendezvousInformationFlags(CONFIG_RENDEZVOUS_MODE));

    Esp32AppServer::Init(); // Init ZCL Data Model and CHIP App Server AND Initialize device attestation config

    // Set starting endpoint id where dynamic endpoints will be assigned, which
    // will be the next consecutive endpoint id after the last fixed endpoint.
    gFirstDynamicEndpointId = static_cast<chip::EndpointId>(
        static_cast<int>(emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1))) + 1);
    gCurrentEndpointId = gFirstDynamicEndpointId;

    // Disable last fixed endpoint, which is used as a placeholder for all of the
    // supported clusters so that ZAP will generated the requisite code.
    emberAfEndpointEnableDisable(emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1)), false);

    // A bridge has root node device type on EP0 and aggregate node device type (bridge) at EP1
    emberAfSetDeviceTypeList(0, Span<const EmberAfDeviceType>(gRootDeviceTypes));
    emberAfSetDeviceTypeList(1, Span<const EmberAfDeviceType>(gAggregateNodeDeviceTypes));

    // Re-add Light 2 -- > will be mapped to ZCL endpoint 7
    AddDeviceEndpoint(&gWindows, &bridgedWindowsEndpoint, Span<const EmberAfDeviceType>(gBridgedwindoscoverTypes),
                      Span<DataVersion>(gWindowsVersions), 1);
}

extern "C" void app_main()
{
    // Initialize the ESP NVS layer.
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_flash_init() failed: %s", esp_err_to_name(err));
        return;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_event_loop_create_default()  failed: %s", esp_err_to_name(err));
        return;
    }

    CHIP_ERROR chip_err = CHIP_NO_ERROR;

    // bridge will have own database named gDevices.
    // Clear database
    memset(gDevices, 0, sizeof(gDevices));

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    if (DeviceLayer::Internal::ESP32Utils::InitWiFiStack() != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "Failed to initialize the Wi-Fi stack");
        return;
    }
#endif

    gWindows.SetReachable(true);
    gWindows.SetChangeCallback(&HandleDeviceStatusChanged);

    DeviceLayer::SetDeviceInfoProvider(&gExampleDeviceInfoProvider);

    CHIPDeviceManager & deviceMgr = CHIPDeviceManager::GetInstance();

    chip_err = deviceMgr.Init(&AppCallback);
    if (chip_err != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "device.Init() failed: %" CHIP_ERROR_FORMAT, chip_err.Format());
        return;
    }

#if CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER
    SetCommissionableDataProvider(&sFactoryDataProvider);
    SetDeviceAttestationCredentialsProvider(&sFactoryDataProvider);
#if CONFIG_ENABLE_ESP32_DEVICE_INSTANCE_INFO_PROVIDER
    SetDeviceInstanceInfoProvider(&sFactoryDataProvider);
#endif
#else
    SetDeviceAttestationCredentialsProvider(Examples::GetExampleDACProvider());
#endif // CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER

    chip::DeviceLayer::PlatformMgr().ScheduleWork(InitServer, reinterpret_cast<intptr_t>(nullptr));
}
