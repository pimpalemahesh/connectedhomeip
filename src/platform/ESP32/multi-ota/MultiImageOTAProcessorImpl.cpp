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

#include "MultiImageOTAProcessorImpl.h"

#include <app/clusters/ota-requestor/OTARequestorInterface.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceEvent.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/ConfigurationManager.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "nvs.h"
#include "nvs_flash.h"

#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#include "esp_ota_ops.h"
#endif

#define TAG "MultiImageOTA"

static constexpr char kNVSNamespace[]   = "mota";
static constexpr char kNVSKeyRMap[]     = "rmap";
static constexpr char kNVSKeyAttempt[]  = "attempt";

using namespace chip::System;

namespace chip {
namespace ESP32 {
namespace MultiOTA {

namespace {

void HandleRestart(Layer * systemLayer, void * appState)
{
    esp_restart();
}

void PostOTAStateChangeEvent(DeviceLayer::OtaState newState)
{
    DeviceLayer::ChipDeviceEvent event;
    event.Type                     = DeviceLayer::DeviceEventType::kOtaStateChanged;
    event.OtaStateChanged.newState = newState;
    CHIP_ERROR err                 = DeviceLayer::PlatformMgr().PostEvent(&event);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(SoftwareUpdate, "PostEvent OtaStateChanged failed: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

} // namespace

// ── Singleton ───────────────────────────────────────────────────────────────

MultiImageOTAProcessor & MultiImageOTAProcessor::GetDefaultInstance()
{
    static MultiImageOTAProcessor sInstance;
    return sInstance;
}

// ── OTAImageProcessorInterface — schedule work on Matter thread ─────────────

CHIP_ERROR MultiImageOTAProcessor::PrepareDownload()
{
    TEMPORARY_RETURN_IGNORED DeviceLayer::PlatformMgr().ScheduleWork(HandlePrepareDownload, reinterpret_cast<intptr_t>(this));
    return CHIP_NO_ERROR;
}

CHIP_ERROR MultiImageOTAProcessor::Finalize()
{
    TEMPORARY_RETURN_IGNORED DeviceLayer::PlatformMgr().ScheduleWork(HandleFinalize, reinterpret_cast<intptr_t>(this));
    return CHIP_NO_ERROR;
}

CHIP_ERROR MultiImageOTAProcessor::Apply()
{
    TEMPORARY_RETURN_IGNORED DeviceLayer::PlatformMgr().ScheduleWork(HandleApply, reinterpret_cast<intptr_t>(this));
    return CHIP_NO_ERROR;
}

CHIP_ERROR MultiImageOTAProcessor::Abort()
{
    TEMPORARY_RETURN_IGNORED DeviceLayer::PlatformMgr().ScheduleWork(HandleAbort, reinterpret_cast<intptr_t>(this));
    return CHIP_NO_ERROR;
}

CHIP_ERROR MultiImageOTAProcessor::ProcessBlock(ByteSpan & block)
{
    CHIP_ERROR err = SetBlock(block);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(SoftwareUpdate, "SetBlock failed: %" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }
    TEMPORARY_RETURN_IGNORED DeviceLayer::PlatformMgr().ScheduleWork(HandleProcessBlock, reinterpret_cast<intptr_t>(this));
    return CHIP_NO_ERROR;
}

bool MultiImageOTAProcessor::IsFirstImageRun()
{
    OTARequestorInterface * requestor = GetRequestorInstance();
    if (requestor == nullptr)
        return false;
    return requestor->GetCurrentUpdateState() == OTARequestorInterface::OTAUpdateStateEnum::kApplying;
}

CHIP_ERROR MultiImageOTAProcessor::ConfirmCurrentImage()
{
    OTARequestorInterface * requestor = GetRequestorInstance();
    if (requestor == nullptr)
        return CHIP_ERROR_INTERNAL;

    uint32_t currentVersion;
    ReturnErrorOnFailure(DeviceLayer::ConfigurationMgr().GetSoftwareVersion(currentVersion));
    if (currentVersion != requestor->GetTargetVersion())
    {
        ChipLogError(SoftwareUpdate, "Version mismatch: running %" PRIu32 " expected %" PRIu32, currentVersion,
                     requestor->GetTargetVersion());
        return CHIP_ERROR_INCORRECT_STATE;
    }

    // Load persisted readiness map from the download session (may span a reboot).
    LogErrorOnFailure(LoadReadinessMapFromNVS());

    if (!AllComponentsVerified())
    {
        std::vector<uint8_t> pending;
        for (auto & [id, r] : mReadinessMap)
        {
            if (r == DeviceReadinessState::kNotReady)
                pending.push_back(id);
        }

        mAttemptCount++;
        LogErrorOnFailure(SaveReadinessMapToNVS());
        OnPendingNodeUpdates(pending, mAttemptCount);

        ChipLogError(SoftwareUpdate, "ConfirmCurrentImage: %u component(s) still not ready; withholding confirmation",
                     static_cast<unsigned>(pending.size()));
        return CHIP_ERROR_INCORRECT_STATE;
    }

#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    esp_err_t espErr = esp_ota_mark_app_valid_cancel_rollback();
    if (espErr != ESP_OK)
    {
        ChipLogError(SoftwareUpdate, "esp_ota_mark_app_valid_cancel_rollback failed: %s", esp_err_to_name(espErr));
        return CHIP_ERROR_INTERNAL;
    }
#endif

    EraseNVSState();
    ChipLogProgress(SoftwareUpdate, "ConfirmCurrentImage: all components verified; image confirmed");
    return CHIP_NO_ERROR;
}

// ── Registration ─────────────────────────────────────────────────────────────

CHIP_ERROR MultiImageOTAProcessor::RegisterProcessor(uint8_t imageId, SubImageProcessor * processor)
{
    VerifyOrReturnError(imageId != 0, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(processor != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(mProcessorMap.find(imageId) == mProcessorMap.end(), CHIP_ERROR_DUPLICATE_KEY_ID);

    mProcessorMap[imageId] = processor;
    ChipLogProgress(SoftwareUpdate, "Registered sub-processor for image ID 0x%02x", imageId);
    return CHIP_NO_ERROR;
}

// ── Matter-thread handlers ───────────────────────────────────────────────────

void MultiImageOTAProcessor::HandlePrepareDownload(intptr_t context)
{
    auto * self = reinterpret_cast<MultiImageOTAProcessor *>(context);
    VerifyOrReturn(self != nullptr, ChipLogError(SoftwareUpdate, "HandlePrepareDownload: null context"));
    VerifyOrReturn(self->mDownloader != nullptr, ChipLogError(SoftwareUpdate, "HandlePrepareDownload: null downloader"));

    // Reset all dispatcher state for a fresh session.
    self->mHeaderParsed         = false;
    self->mAccumulated          = 0;
    self->mHeaderNeeded         = sizeof(MultiImageHeader);
    self->mNumImages            = 0;
    self->mCurrentIndex         = 0;
    self->mEntryStarted         = false;
    self->mBytesDelivered       = 0;
    self->mCurrentStreamOffset  = 0;

    if (self->mHeaderBuf != nullptr)
    {
        Platform::MemoryFree(self->mHeaderBuf);
        self->mHeaderBuf = nullptr;
    }

    self->mReadinessMap.clear();
    self->mHeaderParser.Init();

    TEMPORARY_RETURN_IGNORED self->mDownloader->OnPreparedForDownload(CHIP_NO_ERROR);
    PostOTAStateChangeEvent(DeviceLayer::kOtaDownloadInProgress);
}

void MultiImageOTAProcessor::HandleProcessBlock(intptr_t context)
{
    auto * self = reinterpret_cast<MultiImageOTAProcessor *>(context);
    VerifyOrReturn(self != nullptr, ChipLogError(SoftwareUpdate, "HandleProcessBlock: null context"));
    VerifyOrReturn(self->mDownloader != nullptr, ChipLogError(SoftwareUpdate, "HandleProcessBlock: null downloader"));

    ByteSpan block(self->mBlock.data(), self->mBlock.size());

    // Strip the outer Matter OTA header (consumed once; no-op afterwards).
    CHIP_ERROR err = self->ProcessHeader(block);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(SoftwareUpdate, "ProcessHeader failed: %" CHIP_ERROR_FORMAT, err.Format());
        self->mDownloader->EndDownload(err);
        PostOTAStateChangeEvent(DeviceLayer::kOtaDownloadFailed);
        return;
    }

    if (block.empty())
    {
        // All bytes consumed by outer header — request next block.
        LogErrorOnFailure(self->mDownloader->FetchNextData());
        return;
    }

    err = self->ProcessPayload(block);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(SoftwareUpdate, "ProcessPayload failed: %" CHIP_ERROR_FORMAT, err.Format());
        self->mDownloader->EndDownload(err);
        PostOTAStateChangeEvent(DeviceLayer::kOtaDownloadFailed);
    }
    // FetchNextData / SkipData already called inside ProcessPayload.
}

void MultiImageOTAProcessor::HandleFinalize(intptr_t context)
{
    auto * self = reinterpret_cast<MultiImageOTAProcessor *>(context);
    VerifyOrReturn(self != nullptr, ChipLogError(SoftwareUpdate, "HandleFinalize: null context"));

    // If any component was not ready this cycle, invoke the retry hook.
    bool anyNotReady = false;
    std::vector<uint8_t> pendingIds;
    for (auto & [id, r] : self->mReadinessMap)
    {
        if (r == DeviceReadinessState::kNotReady)
        {
            anyNotReady = true;
            pendingIds.push_back(id);
        }
    }

    if (anyNotReady)
    {
        self->mAttemptCount++;
        LogErrorOnFailure(self->SaveReadinessMapToNVS());
        self->OnPendingNodeUpdates(pendingIds, self->mAttemptCount);
        ChipLogProgress(SoftwareUpdate, "Finalize: %u image(s) not updated this cycle; attempt %" PRIu32,
                        static_cast<unsigned>(pendingIds.size()), self->mAttemptCount);
    }

    LogErrorOnFailure(self->ReleaseBlock());
    PostOTAStateChangeEvent(DeviceLayer::kOtaDownloadComplete);
}

void MultiImageOTAProcessor::HandleApply(intptr_t context)
{
    PostOTAStateChangeEvent(DeviceLayer::kOtaApplyInProgress);

    auto * self = reinterpret_cast<MultiImageOTAProcessor *>(context);
    VerifyOrReturn(self != nullptr, ChipLogError(SoftwareUpdate, "HandleApply: null context"));

    // Invoke Apply() on every sub-processor that received bytes this session.
    CHIP_ERROR applyErr = CHIP_NO_ERROR;
    for (auto & [id, proc] : self->mProcessorMap)
    {
        auto it = self->mReadinessMap.find(id);
        if (it != self->mReadinessMap.end() && it->second == DeviceReadinessState::kReady)
        {
            CHIP_ERROR err = proc->Apply();
            if (err != CHIP_NO_ERROR)
            {
                ChipLogError(SoftwareUpdate, "Sub-processor Apply() failed for image 0x%02x: %" CHIP_ERROR_FORMAT, id,
                             err.Format());
                applyErr = err;
            }
        }
    }

    if (applyErr != CHIP_NO_ERROR)
    {
        PostOTAStateChangeEvent(DeviceLayer::kOtaApplyFailed);
        return;
    }

    PostOTAStateChangeEvent(DeviceLayer::kOtaApplyComplete);

#ifdef CONFIG_OTA_AUTO_REBOOT_ON_APPLY
    TEMPORARY_RETURN_IGNORED DeviceLayer::SystemLayer().StartTimer(
        System::Clock::Milliseconds32(CONFIG_OTA_AUTO_REBOOT_DELAY_MS), HandleRestart, nullptr);
#else
    ChipLogProgress(SoftwareUpdate, "Apply complete — reboot manually to activate the new image");
#endif
}

void MultiImageOTAProcessor::HandleAbort(intptr_t context)
{
    auto * self = reinterpret_cast<MultiImageOTAProcessor *>(context);
    VerifyOrReturn(self != nullptr, ChipLogError(SoftwareUpdate, "HandleAbort: null context"));

    // Abort every sub-processor that was started (kReady) this session.
    for (auto & [id, r] : self->mReadinessMap)
    {
        if (r == DeviceReadinessState::kReady)
        {
            auto it = self->mProcessorMap.find(id);
            if (it != self->mProcessorMap.end())
                it->second->Abort();
        }
    }

    if (self->mHeaderBuf != nullptr)
    {
        Platform::MemoryFree(self->mHeaderBuf);
        self->mHeaderBuf = nullptr;
    }

    LogErrorOnFailure(self->ReleaseBlock());
    PostOTAStateChangeEvent(DeviceLayer::kOtaDownloadAborted);
}

// ── Block processing pipeline ─────────────────────────────────────────────────

CHIP_ERROR MultiImageOTAProcessor::ProcessHeader(ByteSpan & block)
{
    if (mHeaderParser.IsInitialized())
    {
        OTAImageHeader header;
        CHIP_ERROR err = mHeaderParser.AccumulateAndDecode(block, header);

        // CHIP_ERROR_BUFFER_TOO_SMALL means the outer header is not yet complete.
        VerifyOrReturnError(err != CHIP_ERROR_BUFFER_TOO_SMALL, CHIP_NO_ERROR);
        ReturnErrorOnFailure(err);

        mParams.totalFileBytes = header.mPayloadSize;
        mHeaderParser.Clear();
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR MultiImageOTAProcessor::ProcessPayload(ByteSpan & block)
{
    if (!mHeaderParsed)
    {
        ReturnErrorOnFailure(AccumulateHeader(block));

        if (!mHeaderParsed)
        {
            // Still accumulating — request next block.
            LogErrorOnFailure(mDownloader->FetchNextData());
            return CHIP_NO_ERROR;
        }

        if (block.empty())
        {
            LogErrorOnFailure(mDownloader->FetchNextData());
            return CHIP_NO_ERROR;
        }
    }

    return RoutePayload(block);
}

// Phase 1: accumulate MultiImageHeader + SubImageHeader[] before routing.
// On return, mHeaderParsed is true and block contains any remaining payload bytes.
CHIP_ERROR MultiImageOTAProcessor::AccumulateHeader(ByteSpan & block)
{
    // Lazy-allocate the accumulation buffer. Start with sizeof(MultiImageHeader);
    // reallocate to the full size once numImages is known.
    if (mHeaderBuf == nullptr)
    {
        mHeaderBuf = static_cast<uint8_t *>(Platform::MemoryAlloc(sizeof(MultiImageHeader)));
        VerifyOrReturnError(mHeaderBuf != nullptr, CHIP_ERROR_NO_MEMORY);
    }

    // Consume bytes into the buffer up to what is currently needed.
    uint32_t toConsume = std::min(mHeaderNeeded - mAccumulated, static_cast<uint32_t>(block.size()));
    memcpy(mHeaderBuf + mAccumulated, block.data(), toConsume);
    mAccumulated += toConsume;
    block         = block.SubSpan(toConsume);

    // Wait until we have the fixed 8-byte MultiImageHeader.
    if (mAccumulated < sizeof(MultiImageHeader))
        return CHIP_NO_ERROR;

    // Parse the fixed header exactly once (when headerNeeded is still its initial value).
    if (mHeaderNeeded == sizeof(MultiImageHeader))
    {
        MultiImageHeader hdr;
        memcpy(&hdr, mHeaderBuf, sizeof(MultiImageHeader));

        if (hdr.magic != MULTI_IMAGE_HEADER_MAGIC)
        {
            ChipLogError(SoftwareUpdate, "MultiImageHeader: bad magic 0x%08" PRIx32, hdr.magic);
            return CHIP_ERROR_INVALID_SIGNATURE;
        }

        mNumImages = hdr.numImages;

        if (mNumImages == 0)
        {
            // Bundle with no sub-images; nothing to route.
            Platform::MemoryFree(mHeaderBuf);
            mHeaderBuf           = nullptr;
            mHeaderParsed        = true;
            mCurrentStreamOffset = mAccumulated;
            ChipLogProgress(SoftwareUpdate, "MultiImageHeader: 0 images; header done");
            return CHIP_NO_ERROR;
        }

        // Now we know the full header size; grow the buffer.
        uint32_t fullSize = sizeof(MultiImageHeader) + static_cast<uint32_t>(mNumImages) * sizeof(SubImageHeader);
        auto * newBuf     = static_cast<uint8_t *>(Platform::MemoryRealloc(mHeaderBuf, fullSize));
        if (newBuf == nullptr)
        {
            Platform::MemoryFree(mHeaderBuf);
            mHeaderBuf = nullptr;
            return CHIP_ERROR_NO_MEMORY;
        }
        mHeaderBuf    = newBuf;
        mHeaderNeeded = fullSize;

        // Consume any SubImageHeader bytes already in the current block.
        toConsume     = std::min(mHeaderNeeded - mAccumulated, static_cast<uint32_t>(block.size()));
        memcpy(mHeaderBuf + mAccumulated, block.data(), toConsume);
        mAccumulated += toConsume;
        block         = block.SubSpan(toConsume);
    }

    // Still accumulating SubImageHeaders.
    if (mAccumulated < mHeaderNeeded)
        return CHIP_NO_ERROR;

    // Full header is in hand — parse all SubImageHeaders.
    for (uint8_t i = 0; i < mNumImages; i++)
    {
        memcpy(&mSubImages[i], mHeaderBuf + sizeof(MultiImageHeader) + i * sizeof(SubImageHeader), sizeof(SubImageHeader));
    }

    Platform::MemoryFree(mHeaderBuf);
    mHeaderBuf = nullptr;

    mHeaderParsed        = true;
    mCurrentIndex        = 0;
    mEntryStarted        = false;
    mBytesDelivered      = 0;
    mCurrentStreamOffset = mHeaderNeeded; // payload cursor starts right after the header section

    ChipLogProgress(SoftwareUpdate, "MultiImageHeader parsed: %u image(s)", mNumImages);
    return CHIP_NO_ERROR;
}

// Phase 2: route payload bytes to sub-processors per DESIGN.md §5.2.
CHIP_ERROR MultiImageOTAProcessor::RoutePayload(ByteSpan & block)
{
    while (mCurrentIndex < mNumImages && !block.empty())
    {
        const SubImageHeader & entry = mSubImages[mCurrentIndex];

        if (!mEntryStarted)
        {
            // Jump over any alignment gap between the current cursor and entry.offset.
            if (entry.offset > mCurrentStreamOffset)
            {
                uint32_t gap         = entry.offset - mCurrentStreamOffset;
                mCurrentStreamOffset += gap;
                SkipData(gap); // BlockQueryWithSkip — also the next-block request
                return CHIP_NO_ERROR;
            }

            // Look up the sub-processor for this image ID.
            SubImageProcessor * proc = nullptr;
            auto it                  = mProcessorMap.find(entry.imageId);
            if (it != mProcessorMap.end())
                proc = it->second;

            // No registered processor → assume already up to date; does not block confirmation.
            DeviceReadinessState readiness =
                (proc != nullptr) ? proc->IsReadyForOTA(entry.version) : DeviceReadinessState::kAlreadyUpToDate;

            mReadinessMap[entry.imageId] = readiness;
            LogErrorOnFailure(SaveReadinessMapToNVS());

            if (readiness != DeviceReadinessState::kReady)
            {
                const char * reason = (readiness == DeviceReadinessState::kAlreadyUpToDate) ? "already up to date" : "not ready";
                ChipLogProgress(SoftwareUpdate, "Image 0x%02x: %s — skipping %" PRIu32 " bytes", entry.imageId, reason,
                                entry.length);
                mCurrentStreamOffset += entry.length;
                SkipData(entry.length); // BlockQueryWithSkip — also the next-block request
                mCurrentIndex++;
                mBytesDelivered = 0;
                return CHIP_NO_ERROR;
            }

            ReturnErrorOnFailure(proc->Init(entry));
            mEntryStarted = true;
            ChipLogProgress(SoftwareUpdate, "Image 0x%02x: starting write, %" PRIu32 " bytes", entry.imageId, entry.length);
        }

        // Feed bytes to the active sub-processor.
        SubImageProcessor * proc = mProcessorMap.at(entry.imageId); // guaranteed present
        uint32_t toDeliver = std::min(entry.length - mBytesDelivered, static_cast<uint32_t>(block.size()));

        ReturnErrorOnFailure(proc->Write(block.SubSpan(0, toDeliver)));

        mBytesDelivered      += toDeliver;
        mCurrentStreamOffset += toDeliver;
        block                 = block.SubSpan(toDeliver);

        if (mBytesDelivered == entry.length)
        {
            ChipLogProgress(SoftwareUpdate, "Image 0x%02x: write complete", entry.imageId);
            mCurrentIndex++;
            mEntryStarted   = false;
            mBytesDelivered = 0;
        }
    }

    // Block exhausted — request the next one.
    LogErrorOnFailure(mDownloader->FetchNextData());
    return CHIP_NO_ERROR;
}

// ── BDX helpers ──────────────────────────────────────────────────────────────

void MultiImageOTAProcessor::FetchNextData()
{
    LogErrorOnFailure(mDownloader->FetchNextData());
}

void MultiImageOTAProcessor::SkipData(uint32_t numBytes)
{
    LogErrorOnFailure(mDownloader->SkipData(numBytes));
}

// ── Confirmation helpers ──────────────────────────────────────────────────────

bool MultiImageOTAProcessor::AllComponentsVerified() const
{
    for (auto & [id, r] : mReadinessMap)
    {
        if (r == DeviceReadinessState::kNotReady)
            return false;
    }
    return true;
}

CHIP_ERROR MultiImageOTAProcessor::SaveReadinessMapToNVS()
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNVSNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ChipLogError(SoftwareUpdate, "SaveReadinessMapToNVS: nvs_open failed: %s", esp_err_to_name(err));
        return CHIP_ERROR_INTERNAL;
    }

    // Serialise: [imageId(1) | readiness(1)] per entry — matches IMPLEMENTATION.md §9.
    std::vector<uint8_t> blob;
    blob.reserve(mReadinessMap.size() * 2);
    for (auto & [id, r] : mReadinessMap)
    {
        blob.push_back(id);
        blob.push_back(static_cast<uint8_t>(r));
    }

    err = nvs_set_blob(handle, kNVSKeyRMap, blob.data(), blob.size());
    if (err == ESP_OK)
        err = nvs_set_u32(handle, kNVSKeyAttempt, mAttemptCount);
    if (err == ESP_OK)
        err = nvs_commit(handle);

    nvs_close(handle);

    if (err != ESP_OK)
    {
        ChipLogError(SoftwareUpdate, "SaveReadinessMapToNVS failed: %s", esp_err_to_name(err));
        return CHIP_ERROR_INTERNAL;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR MultiImageOTAProcessor::LoadReadinessMapFromNVS()
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNVSNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return CHIP_NO_ERROR; // nothing persisted yet — fresh state
    if (err != ESP_OK)
    {
        ChipLogError(SoftwareUpdate, "LoadReadinessMapFromNVS: nvs_open failed: %s", esp_err_to_name(err));
        return CHIP_ERROR_INTERNAL;
    }

    size_t blobSize = 0;
    err             = nvs_get_blob(handle, kNVSKeyRMap, nullptr, &blobSize);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        nvs_close(handle);
        return CHIP_NO_ERROR;
    }
    if (err != ESP_OK)
    {
        nvs_close(handle);
        return CHIP_ERROR_INTERNAL;
    }

    std::vector<uint8_t> blob(blobSize);
    err = nvs_get_blob(handle, kNVSKeyRMap, blob.data(), &blobSize);
    if (err == ESP_OK)
    {
        mReadinessMap.clear();
        for (size_t i = 0; i + 1 < blobSize; i += 2)
        {
            uint8_t id              = blob[i];
            auto readiness          = static_cast<DeviceReadinessState>(blob[i + 1]);
            mReadinessMap[id]       = readiness;
        }
    }

    uint32_t count = 0;
    if (nvs_get_u32(handle, kNVSKeyAttempt, &count) == ESP_OK)
        mAttemptCount = count;

    nvs_close(handle);

    if (err != ESP_OK)
    {
        ChipLogError(SoftwareUpdate, "LoadReadinessMapFromNVS: read failed: %s", esp_err_to_name(err));
        return CHIP_ERROR_INTERNAL;
    }
    return CHIP_NO_ERROR;
}

void MultiImageOTAProcessor::EraseNVSState()
{
    nvs_handle_t handle;
    if (nvs_open(kNVSNamespace, NVS_READWRITE, &handle) != ESP_OK)
        return;

    nvs_erase_key(handle, kNVSKeyRMap);
    nvs_erase_key(handle, kNVSKeyAttempt);
    nvs_commit(handle);
    nvs_close(handle);

    mReadinessMap.clear();
    mAttemptCount = 0;
}

// ── Block buffer ──────────────────────────────────────────────────────────────

CHIP_ERROR MultiImageOTAProcessor::SetBlock(ByteSpan & block)
{
    if (block.empty())
    {
        LogErrorOnFailure(ReleaseBlock());
        return CHIP_NO_ERROR;
    }

    if (mBlock.size() < block.size())
    {
        if (!mBlock.empty())
            LogErrorOnFailure(ReleaseBlock());

        auto * ptr = static_cast<uint8_t *>(Platform::MemoryAlloc(block.size()));
        VerifyOrReturnError(ptr != nullptr, CHIP_ERROR_NO_MEMORY);
        mBlock = MutableByteSpan(ptr, block.size());
    }

    return CopySpanToMutableSpan(block, mBlock);
}

CHIP_ERROR MultiImageOTAProcessor::ReleaseBlock()
{
    if (mBlock.data() != nullptr)
        Platform::MemoryFree(mBlock.data());
    mBlock = MutableByteSpan();
    return CHIP_NO_ERROR;
}

} // namespace MultiOTA
} // namespace ESP32
} // namespace chip
