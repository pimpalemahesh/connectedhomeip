/*
 *    Copyright (c) 2021-2025 Project CHIP Authors
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

#include "DiagnosticLogsCluster.h"

#include <cluster/DiagnosticLogs/Commands.h>
#include <cluster/DiagnosticLogs/Ids.h>
#include <cluster/DiagnosticLogs/Metadata.h>

constexpr DataModel::AcceptedCommandEntry kAcceptedCommands[] = { RetrieveLogsRequest::kMetadataEntry }

constexpr DataModel::GeneratedCommandEntry kGeneratedCommands[] = { RetrieveLogsResponse::Id }

namespace chip
{
    namespace app {
    namespace Clusters {

    DataModel::ActionReturnStatus DiagnosticLogsCluster::ReadAttribute(const DataModel::ReadAttributeRequest & request,
                                                                       AttributeValueEncoder & encoder)
    {
        using namespace DiagnosticLogs::Attributes;

        switch (request.path.mAttributeId)
        {
        case FeatureMap::Id:
            return encoder.Encode<uint32_t>(0);
        case ClusterRevision::Id:
            return encoder.Encode(DiagnosticLogs::kClusterRevision);
        }
        return Status::UnsupportedAttribute;
    }

    CHIP_ERROR DiagnosticLogsCluster::AcceptedCommands(const ConcreteClusterPath & path,
                                                       ReadOnlyBufferBuilder<DataModel::AcceptedCommandEntry> & builder)
    {
        return builder.ReferenceExisting(kAcceptedCommands);
    }

    CHIP_ERROR DiagnosticLogsCluster::GeneratedCommands(const ConcreteClusterPath & path,
                                                        ReadOnlyBufferBuilder<CommandId> & builder)
    {
        return builder.ReferenceExisting(kGeneratedCommands);
    }

    std::optional<DataModel::ActionReturnStatus> DiagnosticLogsCluster::InvokeCommand(const DataModel::InvokeRequest & request,
                                                                                      TLV::TLVReader & input_arguments,
                                                                                      CommandHandler * handler)
    {
        switch (request.path.mCommandId)
        {
        case RetrieveLogsRequest::Id: {
            RetrieveLogsRequest::DecodableType data;
            ReturnErrorOnFailure(data.Decode(input_arguments));
            return RetrieveLogsRequest(request.path, data, handler);
        }
        }

        return Status::UnsupportedCommand;
    }

    } // namespace Clusters
    } // namespace app
} // namespace chip
