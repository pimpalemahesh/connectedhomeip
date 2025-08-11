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

#include "DiagnosticLogsLogic.h"

namespace chip {
namespace app {
namespace Clusters {

void DiagnosticLogsLogic::SetDelegate(EndpointId endpoint, DiagnosticLogsProviderDelegate * delegate)
{
    uint16_t ep = emberAfGetClusterServerEndpointIndex(endpoint, Id, MATTER_DM_DIAGNOSTIC_LOGS_CLUSTER_SERVER_ENDPOINT_COUNT);
    if (ep < kDiagnosticLogsDiagnosticLogsProviderDelegateTableSize)
    {
        gDiagnosticLogsProviderDelegateTable[ep] = delegate;
    }
}

void DiagnosticLogsServer::HandleLogRequestForBdx(CommandHandler * commandObj, const ConcreteCommandPath & path, IntentEnum intent,
                                                  Optional<CharSpan> transferFileDesignator)
{
    // If the RequestedProtocol is set to BDX and there is no TransferFileDesignator the command SHALL fail with a Status Code of
    // INVALID_COMMAND.
    VerifyOrReturn(transferFileDesignator.HasValue(), commandObj->AddStatus(path, Status::InvalidCommand));

    VerifyOrReturn(transferFileDesignator.Value().size() <= kMaxFileDesignatorLen,
                   commandObj->AddStatus(path, Status::ConstraintError));

    // If there is no delegate, there is no mechanism to read the logs. Assume those are empty and return NoLogs
    auto * delegate = GetDiagnosticLogsProviderDelegate(path.mEndpointId);
    VerifyOrReturn(nullptr != delegate, AddResponse(commandObj, path, StatusEnum::kNoLogs));

    auto size = delegate->GetSizeForIntent(intent);
    // In the case where the size is 0 sets the Status field of the RetrieveLogsResponse to NoLogs and do not start a BDX session.
    VerifyOrReturn(size != 0, HandleLogRequestForResponsePayload(commandObj, path, intent, StatusEnum::kNoLogs));

    // In the case where the Node is able to fit the entirety of the requested logs within the LogContent field, the Status field of
    // the RetrieveLogsResponse SHALL be set to Exhausted and a BDX session SHALL NOT be initiated.
    VerifyOrReturn(size > kMaxLogContentSize, HandleLogRequestForResponsePayload(commandObj, path, intent, StatusEnum::kExhausted));

// If the RequestedProtocol is set to BDX and either the Node does not support BDX or it is not possible for the Node
// to establish a BDX session, then the Node SHALL utilize the LogContent field of the RetrieveLogsResponse command
// to transfer as much of the current logs as it can fit within the response, and the Status field of the
// RetrieveLogsResponse SHALL be set to Exhausted.
#if CHIP_CONFIG_ENABLE_BDX_LOG_TRANSFER
    VerifyOrReturn(!gBDXDiagnosticLogsProvider.IsBusy(), AddResponse(commandObj, path, StatusEnum::kBusy));
    auto err = gBDXDiagnosticLogsProvider.InitializeTransfer(commandObj, path, delegate, intent, transferFileDesignator.Value());
    VerifyOrReturn(CHIP_NO_ERROR == err, AddResponse(commandObj, path, StatusEnum::kDenied));
#else
    HandleLogRequestForResponsePayload(commandObj, path, intent, StatusEnum::kExhausted);
#endif // CHIP_CONFIG_ENABLE_BDX_LOG_TRANSFER
}

} // namespace Clusters
} // namespace app
} // namespace chip
