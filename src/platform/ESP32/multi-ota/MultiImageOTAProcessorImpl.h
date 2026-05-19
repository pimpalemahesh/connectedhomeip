/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
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
#include "MultiImageHeader.h"
#include "SubImageProcessor.h"

#include <app/clusters/ota-requestor/OTADownloader.h>
#include <include/platform/OTAImageProcessor.h>
#include <lib/core/OTAImageHeader.h>
#include <map>
#include <vector>

namespace chip {
namespace ESP32 {
namespace MultiOTA {

class MultiImageOTAProcessor : public OTAImageProcessorInterface
{
public:
    // ── OTAImageProcessorInterface ──────────────────────────────────────────
    CHIP_ERROR PrepareDownload() override;
    CHIP_ERROR Finalize() override;
    CHIP_ERROR Apply() override;
    CHIP_ERROR Abort() override;
    CHIP_ERROR ProcessBlock(ByteSpan & block) override;
    bool IsFirstImageRun() override;
    CHIP_ERROR ConfirmCurrentImage() override;

    void SetOTADownloader(OTADownloader * downloader) { mDownloader = downloader; }

    // ── Registration ────────────────────────────────────────────────────────
    // Register a sub-processor for imageId. Must be called at application
    // initialisation, before any BDX session can start. Calling after a
    // session has begun is undefined behaviour.
    // Each imageId may have at most one processor; duplicate = error.
    CHIP_ERROR RegisterProcessor(uint8_t imageId, SubImageProcessor * processor);

    // ── Retry hook (R3) — override for custom retry policy ─────────────────
    // Called after any cycle that ends with kNotReady entries, or when
    // ConfirmCurrentImage() withholds confirmation.
    // pendingIds: image IDs whose processors returned kNotReady this cycle.
    // attemptCount: persisted across reboots for this softwareVersion.
    // Default: do nothing (fall back to periodic QueryImage timer).
    virtual void OnPendingNodeUpdates(const std::vector<uint8_t> & pendingIds, uint32_t attemptCount) {}

    static MultiImageOTAProcessor & GetDefaultInstance();

private:
    // ── Matter-thread handlers ───────────────────────────────────────────────
    static void HandlePrepareDownload(intptr_t context);
    static void HandleFinalize(intptr_t context);
    static void HandleApply(intptr_t context);
    static void HandleAbort(intptr_t context);
    static void HandleProcessBlock(intptr_t context);

    // ── Block processing pipeline ────────────────────────────────────────────
    CHIP_ERROR ProcessHeader(ByteSpan & block);  // strips outer Matter OTA header
    CHIP_ERROR ProcessPayload(ByteSpan & block); // Phase 1 + Phase 2 dispatch

    // Phase 1: accumulate MultiImageHeader + SubImageHeader[] before routing
    CHIP_ERROR AccumulateHeader(ByteSpan & block);

    // Phase 2: route bytes to sub-processors
    CHIP_ERROR RoutePayload(ByteSpan & block);

    void FetchNextData();
    void SkipData(uint32_t numBytes); // issues BlockQueryWithSkip

    // ── Confirmation helpers ─────────────────────────────────────────────────
    CHIP_ERROR LoadReadinessMapFromNVS();
    CHIP_ERROR SaveReadinessMapToNVS();
    void EraseNVSState(); // called on successful confirmation
    bool AllComponentsVerified() const;

    // ── Block buffer ─────────────────────────────────────────────────────────
    CHIP_ERROR SetBlock(ByteSpan & block);
    CHIP_ERROR ReleaseBlock();

    // ── State ────────────────────────────────────────────────────────────────
    OTADownloader * mDownloader = nullptr;
    OTAImageHeaderParser mHeaderParser;
    MutableByteSpan mBlock;

    // Header accumulation (Phase 1)
    bool mHeaderParsed     = false;
    uint8_t * mHeaderBuf   = nullptr; // heap-allocated; freed after parse
    uint32_t mHeaderNeeded = sizeof(MultiImageHeader);
    uint32_t mAccumulated  = 0;

    // Parsed header
    uint8_t mNumImages = 0;
    SubImageHeader mSubImages[255];

    // Routing state (Phase 2)
    uint8_t mCurrentIndex         = 0;
    bool mEntryStarted            = false;
    uint32_t mBytesDelivered      = 0;
    uint32_t mCurrentStreamOffset = 0;

    // Per-entry readiness recorded during download; persisted in NVS
    std::map<uint8_t, DeviceReadinessState> mReadinessMap;
    uint32_t mAttemptCount = 0;

    // Processor registry
    std::map<uint8_t, SubImageProcessor *> mProcessorMap;
};

} // namespace MultiOTA
} // namespace ESP32
} // namespace chip
