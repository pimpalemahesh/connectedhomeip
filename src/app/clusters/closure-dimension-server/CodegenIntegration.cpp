/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
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

#include "CodegenIntegration.h"
#include <app/clusters/closure-dimension-server/ClosureDimensionCluster.h>
#include <app/clusters/closure-dimension-server/ClosureDimensionClusterDelegate.h>
#include <app/static-cluster-config/ClosureDimension.h>
#include <data-model-providers/codegen/ClusterIntegration.h>
#include <data-model-providers/codegen/CodegenDataModelProvider.h>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::ClosureDimension;
using namespace chip::app::Clusters::ClosureDimension::Attributes;
using namespace chip::Protocols::InteractionModel;

namespace {

constexpr size_t kClosureDimensionFixedClusterCount = ClosureDimension::StaticApplicationConfig::kFixedClusterConfig.size();
constexpr size_t kClosureDimensionMaxClusterCount = kClosureDimensionFixedClusterCount + CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT;

LazyRegisteredServerCluster<ClosureDimensionCluster> gServer[kClosureDimensionMaxClusterCount];
ClosureDimensionClusterDelegate * gDelegates[kClosureDimensionMaxClusterCount] = { nullptr };
ClusterConformance gConformances[kClosureDimensionMaxClusterCount];
ClusterInitParameters gInitParams[kClosureDimensionMaxClusterCount];
} // namespace

namespace chip {
namespace app {
namespace Clusters {
namespace ClosureDimension {

ClosureDimensionCluster * GetInstance(EndpointId endpointId)
{
    if (gServer[endpointId].IsConstructed())
    {
        return &gServer[endpointId].Cluster();
    }
    ChipLogError(Zcl, "Closure Dimension Cluster not initialized.");
    return nullptr;
}

void MatterClosureDimensionSetDelegate(EndpointId endpointId, ClosureDimensionClusterDelegate & delegate)
{
    VerifyOrReturn(!gServer[endpointId].IsConstructed(),
                   ChipLogError(Zcl, "Closure Dimension Cluster already initialized. Cannot set delegate."));
    gDelegates[endpointId] = &delegate;
}

void MatterClosureDimensionSetConformance(EndpointId endpointId, const ClusterConformance & conformance)
{
    VerifyOrReturn(!gServer[endpointId].IsConstructed(),
                   ChipLogError(Zcl, "Closure Dimension Cluster already initialized. Cannot set conformance."));
    gConformances[endpointId] = conformance;
}

void MatterClosureDimensionSetInitParams(EndpointId endpointId, const ClusterInitParameters & initParams)
{
    VerifyOrReturn(!gServer[endpointId].IsConstructed(),
                   ChipLogError(Zcl, "Closure Dimension Cluster already initialized. Cannot set init params."));
    gInitParams[endpointId] = initParams;
}

} // namespace ClosureDimension
} // namespace Clusters
} // namespace app
} // namespace chip

void MatterClosureDimensionClusterInitCallback(EndpointId endpointId)
{
    if (endpointId >= kClosureDimensionMaxClusterCount)
    {
        ChipLogError(Zcl, "Closure Dimension Cluster cannot be initialized on endpoint %u. Endpoint ID is out of range.",
                     endpointId);
        return;
    }

    if (gServer[endpointId].IsConstructed())
    {
        ChipLogError(Zcl, "Closure Dimension Cluster already initialized. Ignoring duplicate initialization.");
        return;
    }

    if (gDelegates[endpointId] == nullptr)
    {
        ChipLogError(Zcl,
                     "Closure Dimension Cluster cannot be initialized without a delegate. Call MatterClosureDimensionSetDelegate() "
                     "before ServerInit().");
        return;
    }

    ClosureDimensionCluster::Context context{ *gDelegates[endpointId], gConformances[endpointId], gInitParams[endpointId] };
    gServer[endpointId].Create(endpointId, context);
    LogErrorOnFailure(CodegenDataModelProvider::Instance().Registry().Register(gServer[endpointId].Registration()));
}

void MatterClosureDimensionClusterShutdownCallback(EndpointId endpointId, MatterClusterShutdownType shutdownType)
{
    if (!gServer[endpointId].IsConstructed())
    {
        ChipLogError(Zcl, "Closure Dimension Cluster not initialized. Ignoring shutdown.");
        return;
    }

    LogErrorOnFailure(CodegenDataModelProvider::Instance().Registry().Unregister(&gServer[endpointId].Cluster()));
    gServer[endpointId].Destroy();
}
// -----------------------------------------------------------------------------
// Plugin initialization

void MatterClosureDimensionPluginServerInitCallback() {}
