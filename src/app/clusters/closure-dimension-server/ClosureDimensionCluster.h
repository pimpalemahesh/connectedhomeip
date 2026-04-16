/**
 *
 *    Copyright (c) 2025 Project CHIP Authors
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
 *
 */
#pragma once
#include <app/cluster-building-blocks/QuieterReporting.h>
#include <app/clusters/closure-dimension-server/ClosureDimensionClusterDelegate.h>
#include <app/clusters/closure-dimension-server/GenericDimensionState.h>
#include <app/server-cluster/DefaultServerCluster.h>
#include <app/server-cluster/OptionalAttributeSet.h>
#include <lib/core/CHIPError.h>
#include <lib/support/BitFlags.h>
#include <lib/support/logging/CHIPLogging.h>

namespace chip {
namespace app {
namespace Clusters {
namespace ClosureDimension {

class ClosureDimensionCluster : public DefaultServerCluster
{
public:
    struct Config
    {
        Config(EndpointId endpoint, ClosureDimensionClusterDelegate & delegate) : mEndpointId(endpoint), mDelegate(delegate) {}

        Config & WithPositioning(Percent100ths resolution, Percent100ths stepValue)
        {
            mFeatureMap.Set(Feature::kPositioning);
            mResolution = resolution;
            mStepValue  = stepValue;
            return *this;
        }
        Config & WithMotionLatching(BitFlags<LatchControlModesBitmap> latchControlModes)
        {
            mFeatureMap.Set(Feature::kMotionLatching);
            mLatchControlModes = latchControlModes;
            return *this;
        }
        Config & WithUnit(ClosureUnitEnum unit, DataModel::Nullable<Structs::UnitRangeStruct::Type> unitRange)
        {
            mFeatureMap.Set(Feature::kUnit);
            mUnit      = unit;
            mUnitRange = unitRange;
            return *this;
        }
        Config & WithLimitation(Structs::RangePercent100thsStruct::Type limitRange)
        {
            mFeatureMap.Set(Feature::kLimitation);
            mLimitRange = limitRange;
            return *this;
        }
        Config & WithSpeed()
        {
            mFeatureMap.Set(Feature::kSpeed);
            return *this;
        }
        Config & WithTranslation(TranslationDirectionEnum translationDirection)
        {
            mFeatureMap.Set(Feature::kTranslation);
            mTranslationDirection = translationDirection;
            return *this;
        }
        Config & WithRotation(RotationAxisEnum rotationAxis, OverflowEnum overflow)
        {
            mFeatureMap.Set(Feature::kRotation);
            mRotationAxis = rotationAxis;
            mOverflow     = overflow;
            return *this;
        }
        Config & WithModulation(ModulationTypeEnum modulationType)
        {
            mFeatureMap.Set(Feature::kModulation);
            mModulationType = modulationType;
            return *this;
        }

        EndpointId mEndpointId;
        ClosureDimensionClusterDelegate & mDelegate;
        BitMask<Feature> mFeatureMap;
        Percent100ths mResolution                                      = 1;
        Percent100ths mStepValue                                       = 1;
        ClosureUnitEnum mUnit                                          = ClosureUnitEnum::kUnknownEnumValue;
        DataModel::Nullable<Structs::UnitRangeStruct::Type> mUnitRange = DataModel::Nullable<Structs::UnitRangeStruct::Type>();
        Structs::RangePercent100thsStruct::Type mLimitRange{};
        TranslationDirectionEnum mTranslationDirection = TranslationDirectionEnum::kUnknownEnumValue;
        RotationAxisEnum mRotationAxis                 = RotationAxisEnum::kUnknownEnumValue;
        OverflowEnum mOverflow                         = OverflowEnum::kUnknownEnumValue;
        ModulationTypeEnum mModulationType             = ModulationTypeEnum::kUnknownEnumValue;
        BitFlags<LatchControlModesBitmap> mLatchControlModes;
    };

    ClosureDimensionCluster(const Config & config);
    ~ClosureDimensionCluster();

    CHIP_ERROR Attributes(const ConcreteClusterPath & path, ReadOnlyBufferBuilder<DataModel::AttributeEntry> & builder) override;

    CHIP_ERROR AcceptedCommands(const ConcreteClusterPath & path,
                                ReadOnlyBufferBuilder<DataModel::AcceptedCommandEntry> & builder) override;

    DataModel::ActionReturnStatus ReadAttribute(const DataModel::ReadAttributeRequest & request,
                                                AttributeValueEncoder & encoder) override;

    std::optional<DataModel::ActionReturnStatus> InvokeCommand(const DataModel::InvokeRequest & request,
                                                               chip::TLV::TLVReader & input_arguments,
                                                               CommandHandler * handler) override;

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
    // Return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE if the attribute is not supported.
    // Otherwise return CHIP_NO_ERROR and set the input parameter value to the current cluster state value
    DataModel::Nullable<GenericDimensionStateStruct> GetCurrentState() const { return mCurrentState; }
    DataModel::Nullable<GenericDimensionStateStruct> GetTargetState() const { return mTargetState; }
    Percent100ths GetResolution() const { return mResolution; }
    Percent100ths GetStepValue() const { return mStepValue; }
    ClosureUnitEnum GetUnit() const { return mUnit; }
    DataModel::Nullable<Structs::UnitRangeStruct::Type> GetUnitRange() const { return mUnitRange; }
    Structs::RangePercent100thsStruct::Type GetLimitRange() const { return mLimitRange; }
    TranslationDirectionEnum GetTranslationDirection() const { return mTranslationDirection; }
    RotationAxisEnum GetRotationAxis() const { return mRotationAxis; }
    OverflowEnum GetOverflow() const { return mOverflow; }
    ModulationTypeEnum GetModulationType() const { return mModulationType; }
    BitFlags<LatchControlModesBitmap> GetLatchControlModes() const { return mLatchControlModes; }
    BitFlags<Feature> GetFeatureMap() const { return mFeatureMap; }

private:
    ClosureDimensionClusterDelegate & mDelegate;
    DataModel::Nullable<GenericDimensionStateStruct> mCurrentState{ DataModel::NullNullable };
    DataModel::Nullable<GenericDimensionStateStruct> mTargetState{ DataModel::NullNullable };
    Percent100ths mResolution                                      = 1;
    Percent100ths mStepValue                                       = 1;
    ClosureUnitEnum mUnit                                          = ClosureUnitEnum::kUnknownEnumValue;
    DataModel::Nullable<Structs::UnitRangeStruct::Type> mUnitRange = DataModel::Nullable<Structs::UnitRangeStruct::Type>();
    Structs::RangePercent100thsStruct::Type mLimitRange{};
    TranslationDirectionEnum mTranslationDirection = TranslationDirectionEnum::kUnknownEnumValue;
    RotationAxisEnum mRotationAxis                 = RotationAxisEnum::kUnknownEnumValue;
    OverflowEnum mOverflow                         = OverflowEnum::kUnknownEnumValue;
    ModulationTypeEnum mModulationType             = ModulationTypeEnum::kUnknownEnumValue;
    BitFlags<LatchControlModesBitmap> mLatchControlModes;
    BitMask<Feature> mFeatureMap = BitMask<Feature>(0);

    /**
     *  @brief Calls delegate HandleSetTarget function after validating the parameters and conformance.
     *
     *  @param [in] position TargetState position
     *  @param [in] latch TargetState latch
     *  @param [in] speed TargetState speed
     *
     *  @return Exits if the cluster is not initialized.
     *          InvalidCommand if none of the input parameters are present.
     *          ConstraintError if the input values are out is out of range.
     *          InvalidInState if the current position of closure is not known.
     *          Success on succesful handling.
     */
    Protocols::InteractionModel::Status HandleSetTargetCommand(Optional<Percent100ths> position, Optional<bool> latch,
                                                               Optional<Globals::ThreeLevelAutoEnum> speed);

    /**
     *  @brief Calls delegate HandleStep function after validating the parameters and conformance.
     *
     *  @param [in] direction step direction
     *  @param [in] numberOfSteps Number of steps
     *  @param [in] speed step speed
     *
     *  @return Exits if the cluster is not initialized.
     *          UnsupportedCommand if Positioning feature is not supported.
     *          ConstraintError if the input values are out is out of range.
     *          InvalidInState if the current position of closure is not known.
     *          Success on successful handling.
     */
    Protocols::InteractionModel::Status HandleStepCommand(StepDirectionEnum direction, uint16_t numberOfSteps,
                                                          Optional<Globals::ThreeLevelAutoEnum> speed);

    /**
     * @brief Set TranslationDirection.
     *             This attribute is not supposed to change once the installation is finalized.
     *             SetTranslationDirection should only be called from Init()
     *
     * @param[in] translationDirection Direction of the translation.
     *
     * @return CHIP_NO_ERROR if set was successful.
     *         CHIP_ERROR_INVALID_ARGUMENT if argument are not valid
     *         CHIP_ERROR_INCORRECT_STATE if the cluster has not been initialized
     *         CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE if feature is not supported
     *
     */
    CHIP_ERROR SetTranslationDirection(const TranslationDirectionEnum translationDirection);

    /**
     * @brief Set RotationAxis.
     *              This attribute is not supposed to change once the installation is finalized.
     *              so SetRotationAxis should only be called from Init().
     *
     * @param[in] rotationAxis Axis of the rotation.
     *
     * @return CHIP_NO_ERROR if set was successful.
     *         CHIP_ERROR_INVALID_ARGUMENT if argument are not valid
     *         CHIP_ERROR_INCORRECT_STATE if the cluster has not been initialized
     *         CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE if feature is not supported
     *
     */
    CHIP_ERROR SetRotationAxis(const RotationAxisEnum rotationAxis);

    /**
     * @brief Set ModulationType.
     *              This attribute is not supposed to change once the installation is finalized.
     *              so SetModulationType should only be called from Init().
     *
     * @param[in] modulationType Modulation type.
     *
     * @return CHIP_NO_ERROR if set was successful.
     *         CHIP_ERROR_INVALID_ARGUMENT if argument are not valid
     *         CHIP_ERROR_INCORRECT_STATE if the cluster has not been initialized
     *         CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE if feature is not supported.
     *
     */
    CHIP_ERROR SetModulationType(const ModulationTypeEnum modulationType);

    // At Present, QuieterReportingAttribute doesnt support Structs.
    // So, this variable will be used for Quietreporting of current state position.
    // TODO: Refactor CurrentState Atrribute to use QuieterReportingAttribute once Issue#39801 is resolved
    QuieterReportingAttribute<Percent100ths> quietReportableCurrentStatePosition{ DataModel::NullNullable };

    EndpointId GetEndpointId() { return mPath.mEndpointId; }
};

} // namespace ClosureDimension
} // namespace Clusters
} // namespace app
} // namespace chip
