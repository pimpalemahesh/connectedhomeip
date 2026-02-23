/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
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

#include <app/clusters/closure-control-server/ClosureControlCluster.h>
#include <app/server-cluster/ServerClusterInterfaceRegistry.h>

namespace chip {
namespace app {
namespace Clusters {

/**
 * @brief Closure Control optional attribute enum class
 */
enum class OptionalAttributeEnum : uint32_t
{
    kCountdownTime = 0x1
};
// As per the spec, the maximum allowed CurrentErrorList size is 10.
constexpr int kCurrentErrorListMaxSize = 10;

/**
 * @brief Structure is used to configure and validate the Cluster configuration.
 *        Validates if the feature map, attributes and commands configuration is valid.
 */
struct ClusterConformance
{
public:
    BitFlags<Feature> & FeatureMap() { return mFeatureMap; }
    const BitFlags<Feature> & FeatureMap() const { return mFeatureMap; }

    BitFlags<OptionalAttributeEnum> & OptionalAttributes() { return mOptionalAttributes; }
    const BitFlags<OptionalAttributeEnum> & OptionalAttributes() const { return mOptionalAttributes; }

    inline bool HasFeature(Feature aFeature) const { return mFeatureMap.Has(aFeature); }

    /**
     * @brief Function determines if Cluster conformance is valid
     *
     *        The function executes these checks in order to validate the conformance
     *        1. Check if either Positioning or MotionLatching is supported. If neither are enabled, returns false.
     *        2. If Speed is enabled, checks that Positioning is enabled and Instantaneous is disabled. Returns false otherwise.
     *        3. If Ventilation, pedestrian or calibration is enabled, Positioning must be enabled. Return false otherwise.
     *
     * @return true, the cluster confirmance is valid
     *         false, otherwise
     */
    bool Valid() const
    {
        // Positioning or Matching must be enabled
        VerifyOrReturnValue(HasFeature(Feature::kPositioning) || HasFeature(Feature::kMotionLatching), false,
                            ChipLogError(AppServer, "Validation failed: Neither Positioning nor MotionLatching is enabled."));

        // If Speed is enabled, Positioning shall be enabled and Instantaneous shall be disabled.
        if (HasFeature(Feature::kSpeed))
        {
            VerifyOrReturnValue(
                HasFeature(Feature::kPositioning) && !HasFeature(Feature::kInstantaneous), false,
                ChipLogError(AppServer, "Validation failed: Speed requires Positioning enabled and Instantaneous disabled."));
        }

        if (HasFeature(Feature::kVentilation) || HasFeature(Feature::kPedestrian) || HasFeature(Feature::kCalibration))
        {
            VerifyOrReturnValue(
                HasFeature(Feature::kPositioning), false,
                ChipLogError(AppServer,
                             "Validation failed: Ventilation, Pedestrian, or Calibration requires Positioning enabled."));
        }

        return true;
    }

private:
    BitFlags<Feature> mFeatureMap;
    BitFlags<OptionalAttributeEnum> mOptionalAttributes;
};

class ClusterLogic
{

}

class Interface
{
public:
    /**
     * Creates a chime server instance. This is just a backwards compatibility wrapper around the ChimeCluster.
     * @param aEndpointId The endpoint on which this cluster exists. This must match the zap configuration.
     * @param aDelegate A reference to the delegate to be used by this server.
     * Note: the caller must ensure that the delegate lives throughout the instance's lifetime.
     */
    Interface(EndpointId endpointId, ClosureControlClusterLogic & logic);
    ~Interface();

    /**
     * Register the closure control cluster instance with the codegen data model provider.
     * @return Returns an error if registration fails.
     */
    CHIP_ERROR Init();

private:
    EndpointId mEndpointId;
    ChimeDelegate * mDelegate;
};

} // namespace Clusters
} // namespace app
} // namespace chip
