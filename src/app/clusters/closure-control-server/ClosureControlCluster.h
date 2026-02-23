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

#include <app/clusters/closure-control-server/ClosureControlClusterLogic.h>
#include <app/clusters/closure-control-server/ClosureControlClusterObjects.h>
#include <app/server-cluster/DefaultServerCluster.h>
#include <clusters/ClosureControl/Attributes.h>
#include <clusters/ClosureControl/Commands.h>
#include <clusters/ClosureControl/Structs.h>
#include <lib/core/CHIPError.h>

namespace chip {
namespace app {
namespace Clusters {
namespace ClosureControl {

/**
 * @brief Closure Control cluster implementation
 *        Applications should instantiate and init one Cluster per endpoint
 *
 */
class ClosureControlCluster : public DefaultServerCluster
{
public:

    ClosureControlCluster(ClosureControlClusterDelegate & delegate, EndpointId endpoint, const BitFlags<Feature> & aFeatures,
                          const BitFlags<OptionalAttribute> & aOptionalAttributes, MainStateEnum mainState, DataModel::Nullable<GenericOverallCurrentState> overallCurrentState, const LatchControlModesBitmap & aLatchControlModes):
                          DefaultServerCluster({ endpoint, ClosureControl::Id }), mEnabledFeatures(aFeatures),
    mOptionalAttributes(aOptionalAttributes), mLogic(delegate, MatterContext(endpoint))
{
    mLogic.Init(ClusterConformance(aFeatures, aOptionalAttributes), ClusterInitParameters(mainState, overallCurrentState));
    mLogic.SetLatchControlModes(aLatchControlModes);
}
    ~ClosureControlCluster();
    virtual ~ClosureControlCluster() = default;

    // All Get functions
    // Return CHIP_ERROR_INCORRECT_STATE if the class has not been initialized.
    // Return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE if the attribute is not supported by the conformance.
    // Otherwise return CHIP_NO_ERROR and set the input parameter value to the current cluster state value

    CHIP_ERROR GetCountdownTime(DataModel::Nullable<ElapsedS> & countdownTime);
    CHIP_ERROR GetMainState(MainStateEnum & mainState);
    CHIP_ERROR GetOverallCurrentState(DataModel::Nullable<GenericOverallCurrentState> & overallCurrentState);
    CHIP_ERROR GetOverallTargetState(DataModel::Nullable<GenericOverallTargetState> & overallTarget);
    CHIP_ERROR GetLatchControlModes(BitFlags<LatchControlModesBitmap> & latchControlModes);
    CHIP_ERROR GetFeatureMap(BitFlags<Feature> & featureMap);
    CHIP_ERROR GetClusterRevision(Attributes::ClusterRevision::TypeInfo::Type & clusterRevision);

    /**
     * @brief Reads the CurrentErrorList attribute.
     *        This method is used to read the CurrentErrorList attribute and encode it using the provided encoder.
     *
     * @param[in] encoder The encoder to use for encoding the CurrentErrorList attribute.
     *
     * @return CHIP_NO_ERROR if the read was successful.
     *         CHIP_ERROR_INCORRECT_STATE if the cluster has not been initialized.
     */
    CHIP_ERROR GetCurrentErrorList(const AttributeValueEncoder::ListEncodeHelper & encoder);

    /**
     * @brief Updates the countdown time based on the Quiet reporting conditions of the attribute.
     *
     * @param countdownTime The countdown time to be set.
     */
    CHIP_ERROR SetCountdownTime(const DataModel::Nullable<ElapsedS> & countdownTime);

    /**
     * @brief Set SetOverallCurrentState.
     *
     * @param[in] overallCurrentState SetOverallCurrentState Position, Latch and Speed.
     *
     * @return CHIP_NO_ERROR if set was successful.
     *         CHIP_ERROR_INCORRECT_STATE if the cluster has not been initialized.
     *         CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE if feature is not supported.
     *         CHIP_ERROR_INVALID_ARGUMENT if argument are not valid
     */
    CHIP_ERROR SetOverallCurrentState(const DataModel::Nullable<GenericOverallCurrentState> & overallCurrentState);

    /**
     * @brief Set OverallTargetState.
     *
     * @param[in] overallTarget OverallTargetState Position, Latch and Speed.
     *
     * @return CHIP_NO_ERROR if set was successful.
     *         CHIP_ERROR_INCORRECT_STATE if the cluster has not been initialized.
     *         CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE if feature is not supported.
     *         CHIP_ERROR_INVALID_ARGUMENT if argument are not valid
     */
    CHIP_ERROR SetOverallTargetState(const DataModel::Nullable<GenericOverallTargetState> & overallTarget);

    /**
     * @brief Sets the main state of the cluster.
     *        This method also generates the EngageStateChanged event based on MainState transition.
     *        This method also updates the CountdownTime attribute based on MainState
     *
     * @param[in] mainState - The new main state to be set.
     *
     * @return CHIP_NO_ERROR if the main state is set successfully.
     *         CHIP_ERROR_INCORRECT_STATE if the cluster has not been initialized.
     *         CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE if new MainState is not supported.
     *         CHIP_ERROR_INCORRECT_STATE if the transition to new MainState is not supported.
     *         CHIP_ERROR_INVALID_ARGUMENT if argument are not valid
     */
    CHIP_ERROR SetMainState(MainStateEnum mainState);

    /**
     * @brief ServerClusterInterface methods.
     */

    CHIP_ERROR Attributes(const ConcreteClusterPath & path, ReadOnlyBufferBuilder<DataModel::AttributeEntry> & builder) override;

    CHIP_ERROR AcceptedCommands(const ConcreteClusterPath & path,
                                ReadOnlyBufferBuilder<DataModel::AcceptedCommandEntry> & builder) override;

    DataModel::ActionReturnStatus ReadAttribute(const DataModel::ReadAttributeRequest & request,
                                                AttributeValueEncoder & encoder) override;

    std::optional<DataModel::ActionReturnStatus> InvokeCommand(const DataModel::InvokeRequest & request,
                                                               chip::TLV::TLVReader & input_arguments,
                                                               CommandHandler * handler) override;

private:
    const BitFlags<Feature> mEnabledFeatures;
    const BitFlags<OptionalAttribute> mOptionalAttributes;
    ClosureControlClusterLogic mLogic;
};

} // namespace ClosureControl
} // namespace Clusters
} // namespace app
} // namespace chip
