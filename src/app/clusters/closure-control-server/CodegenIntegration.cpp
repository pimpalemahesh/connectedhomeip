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

#include <app/clusters/closure-control-server/ClosureControlCluster.h>
#include <app/clusters/closure-control-server/ClosureControlClusterDelegate.h>
#include <data-model-providers/codegen/CodegenDataModelProvider.h>
#include <platform/DefaultTimerDelegate.h>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::ClosureControl;

namespace {

// A single process-wide default timer delegate shared by every ClosureControlCluster
// instance created through the Interface.
DefaultTimerDelegate gTimerDelegate;

} // namespace

namespace chip {
namespace app {
namespace Clusters {
namespace ClosureControl {

Interface::Interface(EndpointId endpoint, ClosureControlClusterDelegate & delegate) : mEndpoint(endpoint), mDelegate(delegate) {}

CHIP_ERROR Interface::Init(const ClusterConformance & conformance, const ClusterInitParameters & initParams)
{
    mConformance = conformance;
    mInitParams  = initParams;
    return CHIP_NO_ERROR;
}

CHIP_ERROR Interface::Init()
{
    ClosureControlCluster::Config config(mEndpoint, mDelegate, gTimerDelegate);

    if (mConformance.HasFeature(Feature::kPositioning))
    {
        config.WithPositioning();
    }
    if (mConformance.HasFeature(Feature::kMotionLatching))
    {
        config.WithMotionLatching(mInitParams.mLatchControlModes);
    }
    if (mConformance.HasFeature(Feature::kInstantaneous))
    {
        config.WithInstantaneous();
    }
    if (mConformance.HasFeature(Feature::kSpeed))
    {
        config.WithSpeed();
    }
    if (mConformance.HasFeature(Feature::kVentilation))
    {
        config.WithVentilation();
    }
    if (mConformance.HasFeature(Feature::kPedestrian))
    {
        config.WithPedestrian();
    }
    if (mConformance.HasFeature(Feature::kCalibration))
    {
        config.WithCalibration();
    }
    if (mConformance.HasFeature(Feature::kProtection))
    {
        config.WithProtection();
    }
    if (mConformance.HasFeature(Feature::kManuallyOperable))
    {
        config.WithManuallyOperable();
    }
    if (mConformance.OptionalAttributes().IsSet(Attributes::CountdownTime::Id))
    {
        config.WithCountdownTime();
    }

    config.WithInitialMainState(mInitParams.mMainState).WithInitialOverallCurrentState(mInitParams.mOverallCurrentState);

    mCluster.Create(config);
    return CodegenDataModelProvider::Instance().Registry().Register(mCluster.Registration());
}

CHIP_ERROR Interface::Shutdown()
{
    VerifyOrReturnError(mCluster.IsConstructed(), CHIP_NO_ERROR);
    ReturnErrorOnFailure(CodegenDataModelProvider::Instance().Registry().Unregister(&mCluster.Cluster()));
    mCluster.Destroy();
    return CHIP_NO_ERROR;
}

ClosureControlCluster & Interface::Cluster()
{
    VerifyOrDie(mCluster.IsConstructed());
    return mCluster.Cluster();
}

} // namespace ClosureControl
} // namespace Clusters
} // namespace app
} // namespace chip

// Ember hooks: the Interface-based pattern owns cluster creation/destruction, so these
// callbacks are no-ops. The application is responsible for calling Interface::Init() /
// Interface::Shutdown() at the appropriate lifecycle points.
void MatterClosureControlClusterInitCallback(EndpointId endpointId) {}
void MatterClosureControlClusterShutdownCallback(EndpointId endpointId, MatterClusterShutdownType shutdownType) {}
void MatterClosureControlPluginServerInitCallback() {}
void MatterClosureControlPluginServerShutdownCallback() {}
