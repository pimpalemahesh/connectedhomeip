/*
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
 */

#include <app/AttributeAccessInterface.h>
#include <app/AttributeAccessInterfaceRegistry.h>
#include <app/CommandHandlerInterfaceRegistry.h>
#include <app/ConcreteAttributePath.h>
#include <app/InteractionModelEngine.h>
#include <app/clusters/closure-control-server/ClosureControlCluster.h>
#include <app/clusters/closure-control-server/ClosureControlClusterMatterContext.h>
#include <app/util/attribute-storage.h>
#include <lib/support/logging/CHIPLogging.h>

using namespace chip;
using namespace chip::app;
using namespace chip::app::DataModel;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::ClosureControl;

using chip::Protocols::InteractionModel::Status;

namespace chip {
namespace app {
namespace Clusters {
namespace ClosureControl {

ClosureControlCluster::~ClosureControlCluster() {}

CHIP_ERROR ClosureControlCluster::Attributes(const ConcreteClusterPath & path, ReadOnlyBufferBuilder<DataModel::AttributeEntry> & builder)
{
    DataModel::AcceptedCommandEntry acceptedCommands[] = {
        not HasFeature(Feature::Instantaneous), Commands::Stop::kMetadataEntry,
        Commands::MoveTo::kMetadataEntry,
        HasFeature(Feature::kCalibration), Commands::Calibrate::kMetadataEntry,
    };
    return builder.AppendElements(Span<DataModel::AcceptedCommandEntry>(acceptedCommands));
}

CHIP_ERROR ClosureControlCluster::AcceptedCommands(const ConcreteClusterPath & path,
    ReadOnlyBufferBuilder<DataModel::AcceptedCommandEntry> & builder)
{
    static constexpr DataModel::AcceptedCommandEntry kAcceptedCommands[] = {
        Commands::Stop::kMetadataEntry,
        Commands::MoveTo::kMetadataEntry,
        Commands::Calibrate::kMetadataEntry,
    };
    return builder.ReferenceExisting(kAcceptedCommands);
}

CHIP_ERROR GetCountdownTime(DataModel::Nullable<ElapsedS> & countdownTime)
{
    return mLogic.GetCountdownTime(countdownTime);
}

CHIP_ERROR GetMainState(MainStateEnum & mainState)
{
    return mLogic.GetMainState(mainState);
}

CHIP_ERROR GetOverallCurrentState(DataModel::Nullable<GenericOverallCurrentState> & overallCurrentState)
{
    return mLogic.GetOverallCurrentState(overallCurrentState);
}

CHIP_ERROR GetOverallTargetState(DataModel::Nullable<GenericOverallTargetState> & overallTarget)
{
    return mLogic.GetOverallTargetState(overallTarget);
}

CHIP_ERROR GetLatchControlModes(BitFlags<LatchControlModesBitmap> & latchControlModes)
{
    return mLogic.GetLatchControlModes(latchControlModes);
}

CHIP_ERROR GetCurrentErrorList(Span<ClosureErrorEnum> & outputSpan)
{
    return mLogic.GetCurrentErrorList(outputSpan);
}

CHIP_ERROR SetCountdownTime(const DataModel::Nullable<ElapsedS> & countdownTime)
{
    return mLogic.SetCountdownTime(countdownTime);
}

CHIP_ERROR SetOverallCurrentState(const DataModel::Nullable<GenericOverallCurrentState> & overallCurrentState)
{
    return mLogic.SetOverallCurrentState(overallCurrentState);
}

CHIP_ERROR SetOverallTargetState(const DataModel::Nullable<GenericOverallTargetState> & overallTarget)
{
    return mLogic.SetOverallTargetState(overallTarget);
}

CHIP_ERROR SetMainState(MainStateEnum mainState)
{
    return mLogic.SetMainState(mainState);
}

CHIP_ERROR SetLatchControlModes(const BitFlags<LatchControlModesBitmap> & latchControlModes)
{
    return mLogic.SetLatchControlModes(latchControlModes);
}

DataModel::ActionReturnStatus ClosureControlCluster::ReadAttribute(const DataModel::ReadAttributeRequest & request, AttributeValueEncoder & encoder)
{
    switch (request.path.mAttributeId)
    {
    case FeatureMap::Id:
        ReturnErrorOnFailure(encoder.Encode(mEnabledFeatures));
        break;

    case ClusterRevision::Id:
        ReturnErrorOnFailure(encoder.Encode(ClosureControl::kRevision));
        break;
    case CountdownTime::Id:
        ReturnErrorOnFailure(encoder.Encode(GetCountdownTime()));
        break;
    case MainState::Id:
        ReturnErrorOnFailure(encoder.Encode(GetMainState()));
        break;
    case CurrentErrorList::Id:
        ReturnErrorOnFailure(encoder.EncodeList(GetCurrentErrorList()));
        break;
    case OverallCurrentState::Id:
        ReturnErrorOnFailure(encoder.Encode(GetOverallCurrentState()));
        break;
    case OverallTargetState::Id:
        ReturnErrorOnFailure(encoder.Encode(GetOverallTargetState()));
        break;
    case LatchControlModes::Id:
        ReturnErrorOnFailure(encoder.Encode(GetLatchControlModes()));
        break;
    default:
        return Protocols::InteractionModel::Status::UnsupportedAttribute;
    }
}

std::optional<DataModel::ActionReturnStatus> ClosureControlCluster::InvokeCommand(const DataModel::InvokeRequest & request,
    chip::TLV::TLVReader & input_arguments,
    CommandHandler * handler)
{
    VerifyOrDie(request.path.mClusterId == ClosureControl::Id);

    switch (request.path.mCommandId)
    {
    case Commands::Stop::Id:
        HandleCommand<Commands::Stop::DecodableType>(
            handlerContext, [&logic = mClusterLogic, &status](HandlerContext & ctx, const auto & commandData) {
                status = logic.HandleStop();
                ctx.mCommandHandler.AddStatus(ctx.mRequestPath, status);
            });
        return;

    case Commands::MoveTo::Id:
        HandleCommand<Commands::MoveTo::DecodableType>(
            handlerContext, [&logic = mClusterLogic, &status](HandlerContext & ctx, const auto & commandData) {
                status = logic.HandleMoveTo(commandData.position, commandData.latch, commandData.speed);
                ctx.mCommandHandler.AddStatus(ctx.mRequestPath, status);
            });
        return;
    case Commands::Calibrate::Id:
        HandleCommand<Commands::Calibrate::DecodableType>(
            handlerContext, [&logic = mClusterLogic, &status](HandlerContext & ctx, const auto & commandData) {
                status = logic.HandleCalibrate();
                ctx.mCommandHandler.AddStatus(ctx.mRequestPath, status);
            });
        return;

    default:
        return Protocols::InteractionModel::Status::UnsupportedCommand;
    }
}

} // namespace ClosureControl
} // namespace Clusters
} // namespace app
} // namespace chip
