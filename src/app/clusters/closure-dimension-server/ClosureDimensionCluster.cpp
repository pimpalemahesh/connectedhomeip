/**
 *
 *    Copyright (c) 2024 Project CHIP Authors
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

#include "ClosureDimensionCluster.h"
#include <app/server-cluster/AttributeListBuilder.h>
#include <clusters/ClosureDimension/Attributes.h>
#include <clusters/ClosureDimension/Commands.h>
#include <clusters/ClosureDimension/Metadata.h>
#include <lib/support/logging/CHIPLogging.h>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::ClosureDimension;
using namespace chip::app::Clusters::ClosureDimension::Attributes;

namespace chip {
namespace app {
namespace Clusters {
namespace ClosureDimension {

using namespace Protocols::InteractionModel;

namespace {

constexpr Percent100ths kPercents100thsMaxValue    = 10000;
constexpr uint64_t kPositionQuietReportingInterval = 5000;

} // namespace

ClosureDimensionCluster::ClosureDimensionCluster(EndpointId endpointId, const Context & context) :
    DefaultServerCluster({ endpointId, ClosureDimension::Id }), mDelegate(context.delegate), mConformance(context.conformance)
{
    VerifyOrDieWithMsg(context.conformance.IsValid(), AppServer, "Invalid conformance");

    // Need to set TranslationDirection, RotationAxis, ModulationType before Initilization of closure, as they should not be changed
    // after Initalization.
    if (mConformance.HasFeature(Feature::kTranslation))
    {
        VerifyOrDieWithMsg(SetTranslationDirection(context.initParams.translationDirection) == CHIP_NO_ERROR, AppServer,
                           "Failed to set translation direction");
    }

    if (mConformance.HasFeature(Feature::kRotation))
    {
        VerifyOrDieWithMsg(SetRotationAxis(context.initParams.rotationAxis) == CHIP_NO_ERROR, AppServer,
                           "Failed to set rotation axis");
    }

    if (mConformance.HasFeature(Feature::kModulation))
    {
        VerifyOrDieWithMsg(SetModulationType(context.initParams.modulationType) == CHIP_NO_ERROR, AppServer,
                           "Failed to set modulation type");
    }
}

ClosureDimensionCluster::~ClosureDimensionCluster() {}

CHIP_ERROR ClosureDimensionCluster::Attributes(const ConcreteClusterPath & path,
                                               ReadOnlyBufferBuilder<DataModel::AttributeEntry> & builder)
{
    using OptionalEntry                = AttributeListBuilder::OptionalAttributeEntry;
    OptionalEntry optionalAttributes[] = {
        { mConformance.HasFeature(Feature::kPositioning), Attributes::Resolution::kMetadataEntry },
        { mConformance.HasFeature(Feature::kPositioning), Attributes::StepValue::kMetadataEntry },
        { mConformance.HasFeature(Feature::kUnit), Attributes::Unit::kMetadataEntry },
        { mConformance.HasFeature(Feature::kUnit), Attributes::UnitRange::kMetadataEntry },
        { mConformance.HasFeature(Feature::kLimitation), Attributes::LimitRange::kMetadataEntry },
        { mConformance.HasFeature(Feature::kTranslation), Attributes::TranslationDirection::kMetadataEntry },
        { mConformance.HasFeature(Feature::kRotation), Attributes::RotationAxis::kMetadataEntry },
        { mConformance.HasFeature(Feature::kRotation), Attributes::Overflow::kMetadataEntry },
        { mConformance.HasFeature(Feature::kModulation), Attributes::ModulationType::kMetadataEntry },
        { mConformance.HasFeature(Feature::kMotionLatching), Attributes::LatchControlModes::kMetadataEntry },
    };

    AttributeListBuilder listBuilder(builder);
    return listBuilder.Append(Span(Attributes::kMandatoryMetadata), Span(optionalAttributes));
}

CHIP_ERROR ClosureDimensionCluster::AcceptedCommands(const ConcreteClusterPath & path,
                                                     ReadOnlyBufferBuilder<DataModel::AcceptedCommandEntry> & builder)
{
    const ClusterConformance & conformance = mConformance;

    static constexpr DataModel::AcceptedCommandEntry kMandatoryCommands[] = {
        Commands::SetTarget::kMetadataEntry,
    };

    static constexpr DataModel::AcceptedCommandEntry kStepCommand[] = {
        Commands::Step::kMetadataEntry,
    };

    ReturnErrorOnFailure(builder.ReferenceExisting(kMandatoryCommands));
    if (conformance.HasFeature(Feature::kPositioning))
    {
        ReturnErrorOnFailure(builder.ReferenceExisting(kStepCommand));
    }

    return CHIP_NO_ERROR;
}

DataModel::ActionReturnStatus ClosureDimensionCluster::ReadAttribute(const DataModel::ReadAttributeRequest & request,
                                                                     AttributeValueEncoder & encoder)
{
    switch (request.path.mAttributeId)
    {
    case Attributes::CurrentState::Id:
        return encoder.Encode(GetCurrentState());
    case Attributes::TargetState::Id:
        return encoder.Encode(GetTargetState());
    case Attributes::Resolution::Id:
        return encoder.Encode(GetResolution());
    case Attributes::StepValue::Id:
        return encoder.Encode(GetStepValue());
    case Attributes::Unit::Id:
        return encoder.Encode(GetUnit());
    case Attributes::UnitRange::Id:
        return encoder.Encode(GetUnitRange());
    case Attributes::LimitRange::Id:
        return encoder.Encode(GetLimitRange());
    case Attributes::TranslationDirection::Id:
        return encoder.Encode(GetTranslationDirection());
    case Attributes::RotationAxis::Id:
        return encoder.Encode(GetRotationAxis());
    case Attributes::Overflow::Id:
        return encoder.Encode(GetOverflow());
    case Attributes::ModulationType::Id:
        return encoder.Encode(GetModulationType());
    case Attributes::LatchControlModes::Id:
        return encoder.Encode(GetLatchControlModes());
    case Attributes::FeatureMap::Id:
        return encoder.Encode(GetFeatureMap());
    case Attributes::ClusterRevision::Id:
        return encoder.Encode(kRevision);
    default:
        return Status::UnsupportedAttribute;
    }
}

std::optional<DataModel::ActionReturnStatus> ClosureDimensionCluster::InvokeCommand(const DataModel::InvokeRequest & request,
                                                                                    chip::TLV::TLVReader & input_arguments,
                                                                                    CommandHandler * handler)
{
    switch (request.path.mCommandId)
    {
    case Commands::SetTarget::Id: {
        Commands::SetTarget::DecodableType commandData;
        ReturnErrorOnFailure(commandData.Decode(input_arguments));
        return HandleSetTargetCommand(commandData.position, commandData.latch, commandData.speed);
    }
    case Commands::Step::Id: {
        Commands::Step::DecodableType commandData;
        ReturnErrorOnFailure(commandData.Decode(input_arguments));
        return HandleStepCommand(commandData.direction, commandData.numberOfSteps, commandData.speed);
    }
    default:
        return Status::UnsupportedCommand;
    }
}

// Specification rules for CurrentState quiet reporting:
// Changes to this attribute SHALL only be marked as reportable in the following cases:
// When the Position changes from null to any other value and vice versa, or
// At most once every 5 seconds when the Position changes from one non-null value to another non-null value, or
// When Target.Position is reached, or
// When CurrentState.Speed changes, or
// When CurrentState.Latch changes.

// At present, QuieterReportingAttribute class does not support Structs.
//  so each field of current state struct has to be handled independently.
//  At present, we are using QuieterReportingAttribute class for Position only.
//  Latch and Speed changes are directly handled by the cluster logic seperately.
//  i.e Speed and latch changes are not considered when calculating the at most 5 seconds quiet reportable changes for Position.
CHIP_ERROR ClosureDimensionCluster::SetCurrentState(const DataModel::Nullable<GenericDimensionStateStruct> & incomingCurrentState)
{
    assertChipStackLockedByCurrentThread();

    VerifyOrReturnError(mState.currentState != incomingCurrentState, CHIP_NO_ERROR);

    bool markDirty = false;

    if (!incomingCurrentState.IsNull())
    {
        // Validate the incoming Position value has valid input parameters and FeatureMap conformance.
        if (incomingCurrentState.Value().position.HasValue())
        {
            //  If the position member is present in the incoming CurrentState, we need to check if the Positioning
            //  feature is supported by the closure. If the Positioning feature is not supported, return an error.
            VerifyOrReturnError(mConformance.HasFeature(Feature::kPositioning), CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);

            if (!incomingCurrentState.Value().position.Value().IsNull())
            {

                VerifyOrReturnError(incomingCurrentState.Value().position.Value().Value() <= kPercents100thsMaxValue,
                                    CHIP_ERROR_INVALID_ARGUMENT);
            }

            bool targetPositionReached = false;
            auto now                   = System::SystemClock().GetMonotonicTimestamp();

            // Logic to determine if target position is reached.
            // If the target position is reached, current state attribute will be marked dirty and reported.
            if (!mState.targetState.IsNull() && mState.targetState.Value().position.HasValue() &&
                !mState.targetState.Value().position.Value().IsNull() &&
                mState.targetState.Value().position == incomingCurrentState.Value().position)
            {
                targetPositionReached = true;
            }

            if (targetPositionReached)
            {
                auto predicate =
                    [](const decltype(quietReportableCurrentStatePosition)::SufficientChangePredicateCandidate &) -> bool {
                    return true;
                };
                markDirty |= (quietReportableCurrentStatePosition.SetValue(incomingCurrentState.Value().position.Value(), now,
                                                                           predicate) == AttributeDirtyState::kMustReport);
            }
            else
            {
                // Predicate to report at most once every 5 seconds when the Position changes from one non-null value to another
                // non-null value, or when the Position changes from null to any other value and vice versa
                System::Clock::Milliseconds64 reportInterval = System::Clock::Milliseconds64(kPositionQuietReportingInterval);
                auto predicate = quietReportableCurrentStatePosition.GetPredicateForSufficientTimeSinceLastDirty(reportInterval);
                markDirty |= (quietReportableCurrentStatePosition.SetValue(incomingCurrentState.Value().position.Value(), now,
                                                                           predicate) == AttributeDirtyState::kMustReport);
            }
        }

        // Validate the incoming latch value has valid FeatureMap conformance.
        if (incomingCurrentState.Value().latch.HasValue())
        {
            //  If the latching member is present in the incoming CurrentState, we need to check if the MotionLatching
            //  feature is supported by the closure. If the MotionLatching feature is not supported, return an error.
            VerifyOrReturnError(mConformance.HasFeature(Feature::kMotionLatching), CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);
        }

        // Changes to this attribute SHALL only be marked as reportable when latch changes.
        if (!mState.currentState.IsNull() && mState.currentState.Value().latch != incomingCurrentState.Value().latch)
        {
            markDirty = true;
        }

        // Validate the incoming Speed value has valid input parameters and FeatureMap conformance.
        if (incomingCurrentState.Value().speed.HasValue())
        {
            //  If the speed member is present in the incoming CurrentState, we need to check if the Speed feature is
            //  supported by the closure. If the Speed feature is not supported, return an error.
            VerifyOrReturnError(mConformance.HasFeature(Feature::kSpeed), CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);

            VerifyOrReturnError(EnsureKnownEnumValue(incomingCurrentState.Value().speed.Value()) !=
                                    Globals::ThreeLevelAutoEnum::kUnknownEnumValue,
                                CHIP_ERROR_INVALID_ARGUMENT);
        }

        // Changes to this attribute SHALL be marked as reportable when speed changes.
        if (!mState.currentState.IsNull() && mState.currentState.Value().speed != incomingCurrentState.Value().speed)
        {
            markDirty = true;
        }
    }

    // If the current state is null and the incoming current state is not null and vice versa, we need to mark dirty.
    if (mState.currentState.IsNull() != incomingCurrentState.IsNull())
    {
        markDirty = true;
    }

    mState.currentState = incomingCurrentState;

    if (markDirty)
    {
        NotifyAttributeChanged(Attributes::CurrentState::Id);
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR ClosureDimensionCluster::SetTargetState(const DataModel::Nullable<GenericDimensionStateStruct> & incomingTargetState)
{
    assertChipStackLockedByCurrentThread();

    VerifyOrReturnError(mState.targetState != incomingTargetState, CHIP_NO_ERROR);

    if (!incomingTargetState.IsNull())
    {
        // Validate the incoming Position value has valid input parameters and FeatureMap conformance.
        if (incomingTargetState.Value().position.HasValue())
        {
            //  If the position member is present in the incoming TargetState, we need to check if the Positioning
            //  feature is supported by the closure. If the Positioning feature is not supported, return an error.
            VerifyOrReturnError(mConformance.HasFeature(Feature::kPositioning), CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);

            if (!incomingTargetState.Value().position.Value().IsNull())
            {
                VerifyOrReturnError(incomingTargetState.Value().position.Value().Value() <= kPercents100thsMaxValue,
                                    CHIP_ERROR_INVALID_ARGUMENT);

                // Incoming TargetState Position value SHALL follow the scaling from Resolution Attribute.
                Percent100ths resolution = GetResolution();
                VerifyOrReturnError(
                    incomingTargetState.Value().position.Value().Value() % resolution == 0, CHIP_ERROR_INVALID_ARGUMENT,
                    ChipLogError(AppServer, "TargetState Position value SHALL follow the scaling from Resolution Attribute"));
            }
        }

        // Validate the incoming latch value has valid FeatureMap conformance.
        if (incomingTargetState.Value().latch.HasValue())
        {
            //  If the latching member is present in the incoming TargetState, we need to check if the MotionLatching
            //  feature is supported by the closure. If the MotionLatching feature is not supported, return an error.
            VerifyOrReturnError(mConformance.HasFeature(Feature::kMotionLatching), CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);
        }

        // Validate the incoming Speed value has valid input parameters and FeatureMap conformance.
        if (incomingTargetState.Value().speed.HasValue())
        {
            //  If the speed member is present in the incoming TargetState, we need to check if the Speed feature is
            //  supported by the closure. If the Speed feature is not supported, return an error.
            VerifyOrReturnError(mConformance.HasFeature(Feature::kSpeed), CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);

            VerifyOrReturnError(EnsureKnownEnumValue(incomingTargetState.Value().speed.Value()) !=
                                    Globals::ThreeLevelAutoEnum::kUnknownEnumValue,
                                CHIP_ERROR_INVALID_ARGUMENT);
        }
    }

    SetAttributeValue(mState.targetState, incomingTargetState, Attributes::TargetState::Id);

    return CHIP_NO_ERROR;
}

CHIP_ERROR ClosureDimensionCluster::SetResolution(const Percent100ths resolution)
{
    VerifyOrReturnError(mConformance.HasFeature(Feature::kPositioning), CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);
    VerifyOrReturnError(0 < resolution && resolution <= kPercents100thsMaxValue, CHIP_ERROR_INVALID_ARGUMENT);

    SetAttributeValue(mState.resolution, resolution, Attributes::Resolution::Id);

    return CHIP_NO_ERROR;
}

CHIP_ERROR ClosureDimensionCluster::SetStepValue(const Percent100ths stepValue)
{
    VerifyOrReturnError(mConformance.HasFeature(Feature::kPositioning), CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);
    VerifyOrReturnError(stepValue <= kPercents100thsMaxValue, CHIP_ERROR_INVALID_ARGUMENT);

    // StepValue SHALL be equal to an integer multiple of the Resolution attribute , if not return Invalid Argument.
    Percent100ths resolution = GetResolution();
    VerifyOrReturnError(stepValue % resolution == 0, CHIP_ERROR_INVALID_ARGUMENT,
                        ChipLogError(AppServer, "StepValue SHALL be equal to an integer multiple of the Resolution attribute"));

    SetAttributeValue(mState.stepValue, stepValue, Attributes::StepValue::Id);

    return CHIP_NO_ERROR;
}

CHIP_ERROR ClosureDimensionCluster::SetUnit(const ClosureUnitEnum unit)
{
    VerifyOrReturnError(mConformance.HasFeature(Feature::kUnit), CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);
    VerifyOrReturnError(EnsureKnownEnumValue(unit) != ClosureUnitEnum::kUnknownEnumValue, CHIP_ERROR_INVALID_ARGUMENT);

    SetAttributeValue(mState.unit, unit, Attributes::Unit::Id);

    return CHIP_NO_ERROR;
}

CHIP_ERROR ClosureDimensionCluster::SetUnitRange(const DataModel::Nullable<Structs::UnitRangeStruct::Type> & unitRange)
{
    VerifyOrReturnError(mConformance.HasFeature(Feature::kUnit), CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);

    if (unitRange.IsNull())
    {
        SetAttributeValue(mState.unitRange, DataModel::NullNullable, Attributes::UnitRange::Id);
        return CHIP_NO_ERROR;
    }

    // Return error if unitRange is invalid
    VerifyOrReturnError(unitRange.Value().min <= unitRange.Value().max, CHIP_ERROR_INVALID_ARGUMENT);

    ClosureUnitEnum unit = GetUnit();

    // If Unit is Millimeter , Range values SHALL contain unsigned values from 0 to 32767 only
    if (unit == ClosureUnitEnum::kMillimeter)
    {
        VerifyOrReturnError(unitRange.Value().min >= 0 && unitRange.Value().min <= 32767, CHIP_ERROR_INVALID_ARGUMENT);
        VerifyOrReturnError(unitRange.Value().max >= 0 && unitRange.Value().max <= 32767, CHIP_ERROR_INVALID_ARGUMENT);
    }

    // If Unit is Degrees the maximum span range is 360 degrees.
    if (unit == ClosureUnitEnum::kDegree)
    {
        VerifyOrReturnError(unitRange.Value().min >= -360 && unitRange.Value().min <= 360, CHIP_ERROR_INVALID_ARGUMENT);
        VerifyOrReturnError(unitRange.Value().max >= -360 && unitRange.Value().max <= 360, CHIP_ERROR_INVALID_ARGUMENT);
        VerifyOrReturnError((unitRange.Value().max - unitRange.Value().min) <= 360, CHIP_ERROR_INVALID_ARGUMENT);
    }

    // If the mState unitRange is null, we need to set it to the new value
    if (mState.unitRange.IsNull())
    {
        mState.unitRange.SetNonNull(unitRange.Value());
        NotifyAttributeChanged(Attributes::UnitRange::Id);
        return CHIP_NO_ERROR;
    }

    // If both the mState unitRange and unitRange are not null, we need to update mState unitRange if the values are different
    if ((unitRange.Value().min != mState.unitRange.Value().min) || (unitRange.Value().max != mState.unitRange.Value().max))
    {
        mState.unitRange.Value().min = unitRange.Value().min;
        mState.unitRange.Value().max = unitRange.Value().max;
        NotifyAttributeChanged(Attributes::UnitRange::Id);
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR ClosureDimensionCluster::SetLimitRange(const Structs::RangePercent100thsStruct::Type & limitRange)
{
    VerifyOrReturnError(mConformance.HasFeature(Feature::kLimitation), CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);

    // If the limit range is invalid, we need to return an error
    VerifyOrReturnError(limitRange.min <= limitRange.max, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(limitRange.min <= kPercents100thsMaxValue, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(limitRange.max <= kPercents100thsMaxValue, CHIP_ERROR_INVALID_ARGUMENT);

    // LimitRange.Min and LimitRange.Max SHALL be equal to an integer multiple of the Resolution attribute.
    Percent100ths resolution = GetResolution();
    VerifyOrReturnError(
        limitRange.min % resolution == 0, CHIP_ERROR_INVALID_ARGUMENT,
        ChipLogError(AppServer, "LimitRange.Min SHALL be equal to an integer multiple of the Resolution attribute."));
    VerifyOrReturnError(
        limitRange.max % resolution == 0, CHIP_ERROR_INVALID_ARGUMENT,
        ChipLogError(AppServer, "LimitRange.Max SHALL be equal to an integer multiple of the Resolution attribute."));

    if ((limitRange.min != mState.limitRange.min) || (limitRange.max != mState.limitRange.max))
    {
        mState.limitRange.min = limitRange.min;
        mState.limitRange.max = limitRange.max;
        NotifyAttributeChanged(Attributes::LimitRange::Id);
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR ClosureDimensionCluster::SetTranslationDirection(const TranslationDirectionEnum translationDirection)
{
    VerifyOrReturnError(mConformance.HasFeature(Feature::kTranslation), CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);
    VerifyOrReturnError(EnsureKnownEnumValue(translationDirection) != TranslationDirectionEnum::kUnknownEnumValue,
                        CHIP_ERROR_INVALID_ARGUMENT);

    SetAttributeValue(mState.translationDirection, translationDirection, Attributes::TranslationDirection::Id);

    return CHIP_NO_ERROR;
}

CHIP_ERROR ClosureDimensionCluster::SetRotationAxis(const RotationAxisEnum rotationAxis)
{
    VerifyOrReturnError(mConformance.HasFeature(Feature::kRotation), CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);
    VerifyOrReturnError(EnsureKnownEnumValue(rotationAxis) != RotationAxisEnum::kUnknownEnumValue, CHIP_ERROR_INVALID_ARGUMENT);

    SetAttributeValue(mState.rotationAxis, rotationAxis, Attributes::RotationAxis::Id);

    return CHIP_NO_ERROR;
}

CHIP_ERROR ClosureDimensionCluster::SetOverflow(const OverflowEnum overflow)
{
    VerifyOrReturnError(mConformance.HasFeature(Feature::kRotation), CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);
    VerifyOrReturnError(EnsureKnownEnumValue(overflow) != OverflowEnum::kUnknownEnumValue, CHIP_ERROR_INVALID_ARGUMENT);

    RotationAxisEnum rotationAxis = GetRotationAxis();

    // If the axis is centered, one part goes Outside and the other part goes Inside.
    // In this case, this attribute SHALL use Top/Bottom/Left/Right Inside or Top/Bottom/Left/Right Outside enumerated value.
    if (rotationAxis == RotationAxisEnum::kCenteredHorizontal || rotationAxis == RotationAxisEnum::kCenteredVertical)
    {
        VerifyOrReturnError(overflow != OverflowEnum::kNoOverflow && overflow != OverflowEnum::kInside &&
                                overflow != OverflowEnum::kOutside,
                            CHIP_ERROR_INVALID_ARGUMENT);
    }

    SetAttributeValue(mState.overflow, overflow, Attributes::Overflow::Id);

    return CHIP_NO_ERROR;
}

CHIP_ERROR ClosureDimensionCluster::SetModulationType(const ModulationTypeEnum modulationType)
{
    VerifyOrReturnError(mConformance.HasFeature(Feature::kModulation), CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);
    VerifyOrReturnError(EnsureKnownEnumValue(modulationType) != ModulationTypeEnum::kUnknownEnumValue, CHIP_ERROR_INVALID_ARGUMENT);

    SetAttributeValue(mState.modulationType, modulationType, Attributes::ModulationType::Id);

    return CHIP_NO_ERROR;
}

CHIP_ERROR ClosureDimensionCluster::SetLatchControlModes(const BitFlags<LatchControlModesBitmap> & latchControlModes)
{
    VerifyOrReturnError(mConformance.HasFeature(Feature::kMotionLatching), CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);

    SetAttributeValue(mState.latchControlModes, latchControlModes, Attributes::LatchControlModes::Id);

    return CHIP_NO_ERROR;
}

Status ClosureDimensionCluster::HandleSetTargetCommand(Optional<Percent100ths> position, Optional<bool> latch,
                                                       Optional<Globals::ThreeLevelAutoEnum> speed)
{
    //  If all command parameters don't have a value, return InvalidCommand
    VerifyOrReturnError(position.HasValue() || latch.HasValue() || speed.HasValue(), Status::InvalidCommand);

    // TODO: If this command is sent while the closure is in a non-compatible internal-state, a status code of
    // INVALID_IN_STATE SHALL be returned.

    DataModel::Nullable<GenericDimensionStateStruct> targetState = GetTargetState();

    // If targetState is null, we need to initialize to default value.
    // This is to ensure that we can set the position, latch, and speed values in the targetState.
    if (targetState.IsNull())
    {
        targetState.SetNonNull(GenericDimensionStateStruct{});
    }

    // If position field is present and Positioning(PS) feature is not supported, we should not set targetState.position value.
    if (position.HasValue() && mConformance.HasFeature(Feature::kPositioning))
    {
        VerifyOrReturnError((position.Value() <= kPercents100thsMaxValue), Status::ConstraintError);

        // If the Limitation Feature is active, the closure will automatically offset the TargetState.Position value to fit within
        // LimitRange.Min and LimitRange.Max.
        if (mConformance.HasFeature(Feature::kLimitation))
        {
            Structs::RangePercent100thsStruct::Type limitRange = GetLimitRange();
            if (position.Value() > limitRange.max)
            {
                position.Value() = limitRange.max;
            }

            else if (position.Value() < limitRange.min)
            {
                position.Value() = limitRange.min;
            }
        }

        Percent100ths resolution = GetResolution();
        // Check if position.Value() is an integer multiple of resolution, else round to nearest valid value
        if (position.Value() % resolution != 0)
        {
            Percent100ths roundedPosition =
                static_cast<Percent100ths>(((position.Value() + resolution / 2) / resolution) * resolution);
            ChipLogProgress(AppServer, "Rounding position from %u to nearest valid value %u based on resolution %u",
                            position.Value(), roundedPosition, resolution);
            position.SetValue(roundedPosition);
        }
        targetState.Value().position.SetValue(DataModel::MakeNullable(position.Value()));
    }

    // If latch field is present and MotionLatching feature is not supported, we should not set targetState.latch value.
    if (latch.HasValue() && mConformance.HasFeature(Feature::kMotionLatching))
    {
        // If latch value is true and the Remote Latching feature is not supported, or
        // if latch value is false and the Remote Unlatching feature is not supported, return InvalidInState.
        if ((latch.Value() && !mState.latchControlModes.Has(LatchControlModesBitmap::kRemoteLatching)) ||
            (!latch.Value() && !mState.latchControlModes.Has(LatchControlModesBitmap::kRemoteUnlatching)))
        {
            return Status::InvalidInState;
        }

        targetState.Value().latch.SetValue(DataModel::MakeNullable(latch.Value()));
    }

    // If speed field is present and Speed feature is not supported, we should not set targetState.speed value.
    if (speed.HasValue() && mConformance.HasFeature(Feature::kSpeed))
    {
        VerifyOrReturnError(speed.Value() != Globals::ThreeLevelAutoEnum::kUnknownEnumValue, Status::ConstraintError);
        targetState.Value().speed.SetValue(speed.Value());
    }

    // Check if the current position is valid or else return InvalidInState
    DataModel::Nullable<GenericDimensionStateStruct> currentState = GetCurrentState();
    VerifyOrReturnError(!currentState.IsNull(), Status::InvalidInState);
    if (mConformance.HasFeature(Feature::kPositioning))
    {
        VerifyOrReturnError(currentState.Value().position.HasValue() && !currentState.Value().position.Value().IsNull(),
                            Status::InvalidInState);
    }

    // If this command requests a position change while the Latch field of the CurrentState is True (Latched), and the Latch field
    // of this command is not set to False (Unlatched), a status code of INVALID_IN_STATE SHALL be returned.
    if (mConformance.HasFeature(Feature::kMotionLatching))
    {
        if (position.HasValue() && currentState.Value().latch.HasValue() && !currentState.Value().latch.Value().IsNull() &&
            currentState.Value().latch.Value().Value())
        {
            VerifyOrReturnError(latch.HasValue() && !latch.Value(), Status::InvalidInState,
                                ChipLogError(AppServer,
                                             "Latch is True in State, but SetTarget command does not set latch to False"
                                             "when position change is requested on endpoint : %d",
                                             GetEndpointId()));
        }
    }

    // Target should only be set when delegate function returns status as Success. Return failure otherwise
    VerifyOrReturnError(mDelegate.HandleSetTarget(position, latch, speed) == Status::Success, Status::Failure);

    VerifyOrReturnError(SetTargetState(targetState) == CHIP_NO_ERROR, Status::Failure);

    return Status::Success;
}

Status ClosureDimensionCluster::HandleStepCommand(StepDirectionEnum direction, uint16_t numberOfSteps,
                                                  Optional<Globals::ThreeLevelAutoEnum> speed)
{
    // Return ConstraintError if command parameters are out of bounds
    VerifyOrReturnError(direction != StepDirectionEnum::kUnknownEnumValue, Status::ConstraintError);
    VerifyOrReturnError(numberOfSteps > 0, Status::ConstraintError);

    DataModel::Nullable<GenericDimensionStateStruct> stepTarget = GetTargetState();

    if (stepTarget.IsNull())
    {
        // If stepTarget is null, we need to initialize to default value.
        // This is to ensure that we can set the position, latch, and speed values in the stepTarget.
        stepTarget.SetNonNull(GenericDimensionStateStruct{});
    }

    // If speed field is present and Speed feature is not supported, we should not set stepTarget.speed value.
    if (speed.HasValue() && mConformance.HasFeature(Feature::kSpeed))
    {
        VerifyOrReturnError(speed.Value() != Globals::ThreeLevelAutoEnum::kUnknownEnumValue, Status::ConstraintError);
        stepTarget.Value().speed.SetValue(speed.Value());
    }

    // TODO: If the server is in a state where it cannot support the command, the server SHALL respond with an
    // INVALID_IN_STATE response and the TargetState attribute value SHALL remain unchanged.

    // Check if the current position is valid or else return InvalidInState
    DataModel::Nullable<GenericDimensionStateStruct> currentState = GetCurrentState();
    VerifyOrReturnError(!currentState.IsNull(), Status::InvalidInState);
    VerifyOrReturnError(currentState.Value().position.HasValue() && !currentState.Value().position.Value().IsNull(),
                        Status::InvalidInState);

    if (mConformance.HasFeature(Feature::kMotionLatching))
    {
        if (currentState.Value().latch.HasValue() && !currentState.Value().latch.Value().IsNull())
        {
            VerifyOrReturnError(!currentState.Value().latch.Value().Value(), Status::InvalidInState,
                                ChipLogError(AppServer,
                                             "Step command cannot be processed when current latch is True"
                                             "on endpoint : %d",
                                             GetEndpointId()));
        }
        // Return InvalidInState if currentState is latched
    }

    // Derive TargetState Position from StepValue and NumberOfSteps.
    Percent100ths stepValue = GetStepValue();

    // Convert step to position delta.
    // As StepValue can only take maxvalue of kPercents100thsMaxValue(which is 10000). Below product will be within limits of
    // int32_t
    uint32_t delta       = numberOfSteps * stepValue;
    uint32_t newPosition = 0;

    // check if closure supports Limitation feature, if yes fetch the LimitRange values
    bool limitSupported = mConformance.HasFeature(Feature::kLimitation) ? true : false;

    Structs::RangePercent100thsStruct::Type limitRange;

    if (limitSupported)
    {
        limitRange = GetLimitRange();
    }

    // Position = Position - NumberOfSteps * StepValue
    uint32_t currentPosition = static_cast<uint32_t>(currentState.Value().position.Value().Value());

    switch (direction)
    {

    case StepDirectionEnum::kDecrease:
        // To avoid underflow, newPosition will be set to 0 if currentPosition is less than or equal to delta
        newPosition = (currentPosition > delta) ? currentPosition - delta : 0;
        // Position value SHALL be clamped to 0.00% if the LM feature is not supported or LimitRange.Min if the LM feature is
        // supported.
        newPosition = limitSupported ? std::max(newPosition, static_cast<uint32_t>(limitRange.min)) : newPosition;
        break;

    case StepDirectionEnum::kIncrease:
        // To avoid overflow, newPosition will be set to UINT32_MAX if sum of currentPosition and delta is greater than UINT32_MAX
        newPosition = (currentPosition > UINT32_MAX - delta) ? UINT32_MAX : currentPosition + delta;
        // Position value SHALL be clamped to 0.00% if the LM feature is not supported or LimitRange.Max if the LM feature is
        // supported.
        newPosition = limitSupported ? std::min(newPosition, static_cast<uint32_t>(limitRange.max))
                                     : std::min(newPosition, static_cast<uint32_t>(kPercents100thsMaxValue));
        break;

    default:
        // Should never reach here due to earlier VerifyOrReturnError check
        ChipLogError(AppServer, "Unhandled StepDirectionEnum value");
        return Status::ConstraintError;
    }

    // TargetState should only be set when delegate function returns status as Success. Return failure otherwise
    VerifyOrReturnError(mDelegate.HandleStep(direction, numberOfSteps, speed) == Status::Success, Status::Failure);

    stepTarget.Value().position.SetValue(DataModel::MakeNullable(static_cast<Percent100ths>(newPosition)));
    VerifyOrReturnError(SetTargetState(stepTarget) == CHIP_NO_ERROR, Status::Failure);

    return Status::Success;
}

} // namespace ClosureDimension
} // namespace Clusters
} // namespace app
} // namespace chip
