/**
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

#include <app/clusters/closure-dimension-server/ClosureDimensionCluster.h>
#include <app/clusters/closure-dimension-server/ClosureDimensionClusterDelegate.h>

#include <app/server-cluster/ServerClusterInterfaceRegistry.h>

namespace chip {
namespace app {
namespace Clusters {
namespace ClosureDimension {

class Interface
{
public:
    Interface(EndpointId endpoint, ClosureDimensionClusterDelegate & delegate);
    ~Interface() = default;

    CHIP_ERROR Init(ClusterConformance & conformance, ClusterInitParameters & initParams);

    CHIP_ERROR Init();

    CHIP_ERROR Shutdown();

    /**
     * @brief Set Current State.
     *
     * @param[in] currentState Current State Position, Latch and Speed.
     *
     * @return CHIP_NO_ERROR if set was successful.
     *         CHIP_ERROR_INCORRECT_STATE if the cluster has not been initialized.
     *         CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE if feature is not supported.
     *         CHIP_ERROR_INVALID_ARGUMENT if argument are not valid
     */
    CHIP_ERROR SetCurrentState(const DataModel::Nullable<GenericDimensionStateStruct> & currentState);

    /**
     * @brief Set TargetState.
     *
     * @param[in] targetState TargetState Position, Latch and Speed.
     *
     * @return CHIP_NO_ERROR if set was successful.
     *         CHIP_ERROR_INCORRECT_STATE if the cluster has not been initialized.
     *         CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE if feature is not supported.
     *         CHIP_ERROR_INVALID_ARGUMENT if argument are not valid
     */
    CHIP_ERROR SetTargetState(const DataModel::Nullable<GenericDimensionStateStruct> & targetState);

    /**
     * @brief Set Resolution.
     *
     * @param[in] resolution Minimal acceptable change of Position fields of attributes.
     *
     * @return CHIP_NO_ERROR if set was successful.
     *         CHIP_ERROR_INCORRECT_STATE if the cluster has not been initialized
     *         CHIP_ERROR_INVALID_ARGUMENT if argument are not valid
     *         CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE if feature is not supported.
     */
    CHIP_ERROR SetResolution(const Percent100ths resolution);

    /**
     * @brief Set StepValue.
     *
     * @param[in] stepValue One step value for Step command
     *
     * @return CHIP_NO_ERROR if set was successful.
     *         CHIP_ERROR_INCORRECT_STATE if the cluster has not been initialized
     *         CHIP_ERROR_INVALID_ARGUMENT if argument are not valid
     *         CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE if feature is not supported.
     */
    CHIP_ERROR SetStepValue(const Percent100ths stepValue);

    /**
     * @brief Set Unit.
     *
     * @param[in] unit Unit related to the Positioning.
     *
     * @return CHIP_NO_ERROR if set was successful.
     *         CHIP_ERROR_INCORRECT_STATE if the cluster has not been initialized
     *         CHIP_ERROR_INVALID_ARGUMENT if argument are not valid
     *         CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE if feature is not supported.
     */
    CHIP_ERROR SetUnit(const ClosureUnitEnum unit);

    /**
     * @brief Set UnitRange.
     *
     * @param[in] unitRange Minimum and Maximum values expressed by positioning following the unit.
     *
     * @return CHIP_NO_ERROR if set was successful.
     *         CHIP_ERROR_INCORRECT_STATE if the cluster has not been initialized
     *         CHIP_ERROR_INVALID_ARGUMENT if argument are not valid
     *         CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE if feature is not supported.
     */
    CHIP_ERROR SetUnitRange(const DataModel::Nullable<Structs::UnitRangeStruct::Type> & unitRange);

    /**
     * @brief Set LimitRange.
     *
     * @param[in] limitRange Range of possible values for the position field in Current attribute.
     *
     * @return CHIP_NO_ERROR if set was successful.
     *         CHIP_ERROR_INCORRECT_STATE if the cluster has not been initialized
     *         CHIP_ERROR_INVALID_ARGUMENT if argument are not valid
     *         CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE if feature is not supported.
     */
    CHIP_ERROR SetLimitRange(const Structs::RangePercent100thsStruct::Type & limitRange);

    /**
     * @brief Set Overflow.
     *
     * @param[in] overflow Overflow related to Rotation.
     *
     * @return CHIP_NO_ERROR if set was successful.
     *         CHIP_ERROR_INVALID_ARGUMENT if argument are not valid
     *         CHIP_ERROR_INCORRECT_STATE if the cluster has not been initialized
     *         CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE if feature is not supported.
     */
    CHIP_ERROR SetOverflow(const OverflowEnum overflow);

    /**
     * @brief Sets the latch control modes for the closure dimension cluster.
     *
     * This method updates the latch control modes using the provided bit flags.
     *
     * @param latchControlModes BitFlags representing the desired latch control modes.
     * @return CHIP_ERROR Returns CHIP_NO_ERROR on success, or an appropriate error code on failure.
     */
    CHIP_ERROR SetLatchControlModes(const BitFlags<LatchControlModesBitmap> & latchControlModes);

    // All Get functions:
    // Return CHIP_ERROR_INCORRECT_STATE if the cluster has not been initialized.
    // Return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE if the attribute is not supported.
    // Otherwise return CHIP_NO_ERROR and set the input parameter value to the current cluster state value
    CHIP_ERROR GetCurrentState(DataModel::Nullable<GenericDimensionStateStruct> & currentState);
    CHIP_ERROR GetTargetState(DataModel::Nullable<GenericDimensionStateStruct> & targetState);
    CHIP_ERROR GetResolution(Percent100ths & resolution);
    CHIP_ERROR GetStepValue(Percent100ths & stepValue);
    CHIP_ERROR GetUnit(ClosureUnitEnum & unit);
    CHIP_ERROR GetUnitRange(DataModel::Nullable<Structs::UnitRangeStruct::Type> & unitRange);
    CHIP_ERROR GetLimitRange(Structs::RangePercent100thsStruct::Type & limitRange);
    CHIP_ERROR GetTranslationDirection(TranslationDirectionEnum & translationDirection);
    CHIP_ERROR GetRotationAxis(RotationAxisEnum & rotationAxis);
    CHIP_ERROR GetOverflow(OverflowEnum & overflow);
    CHIP_ERROR GetModulationType(ModulationTypeEnum & modulationType);
    CHIP_ERROR GetLatchControlModes(BitFlags<LatchControlModesBitmap> & latchControlModes);
    CHIP_ERROR GetFeatureMap(BitFlags<Feature> & featureMap);
    CHIP_ERROR GetClusterRevision(Attributes::ClusterRevision::TypeInfo::Type & clusterRevision);
    ClusterConformance & GetConformance();


private:
    EndpointId mEndpoint;
    ClosureDimensionClusterDelegate & mDelegate;
    ClusterConformance mConformance;
    ClusterInitParameters mInitParams;
    // The Code Driven Closure Dimension Cluster instance (lazy-initialized)
    chip::app::LazyRegisteredServerCluster<ClosureDimensionCluster> mCluster;
};
} // namespace ClosureDimension
} // namespace Clusters
} // namespace app
} // namespace chip
