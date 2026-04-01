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
#include <data-model-providers/codegen/ClusterIntegration.h>

#include <app/util/attribute-storage.h>
#include <data-model-providers/codegen/CodegenDataModelProvider.h>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::ClosureDimension;
using namespace chip::app::Clusters::ClosureDimension::Attributes;
using namespace chip::Protocols::InteractionModel;

namespace chip {
namespace app {
namespace Clusters {
namespace ClosureDimension {

Interface::Interface(EndpointId endpoint, ClosureDimensionClusterDelegate & delegate) : mEndpoint(endpoint), mDelegate(delegate) {}

CHIP_ERROR Interface::Init(ClusterConformance & conformance, ClusterInitParameters & initParams)
{
    ClosureDimensionCluster::Context context{ mDelegate, conformance, initParams };
    mCluster.Create(mEndpoint, context);
    return CodegenDataModelProvider::Instance().Registry().Register(mCluster.Registration());
}

CHIP_ERROR Interface::Shutdown()
{
    VerifyOrDie(mCluster.IsConstructed());
    return CodegenDataModelProvider::Instance().Registry().Unregister(&mCluster.Cluster());
}

ClosureDimensionCluster & Interface::GetClusterInstance()
{
    VerifyOrDie(mCluster.IsConstructed());
    return mCluster.Cluster();
}

} // namespace ClosureDimension
} // namespace Clusters
} // namespace app
} // namespace chip

void MatterClosureDimensionClusterInitCallback(EndpointId endpointId) {}

void MatterClosureDimensionClusterShutdownCallback(EndpointId endpointId, MatterClusterShutdownType shutdownType) {}
// -----------------------------------------------------------------------------
// Plugin initialization

void MatterClosureDimensionPluginServerInitCallback() {}
