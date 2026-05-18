# Matter OTA — Complete Code Reference

This document walks you through every file involved in Matter's OTA Software
Update implementation, explains what each piece does in plain words, and anchors
that explanation to the actual source with file:line references and inline code
blocks.

Read BDX.md first if you want the transport-protocol background; this file is
about the application-layer logic on top of BDX.

---

## Table of Contents

1.  Two-Node Architecture (who is who)
2.  The Six-Layer Requestor Stack
3.  Layer 0 — OTA Image File Format (OTAImageHeader.h)
4.  Layer 1 — Cluster Command Handling 4a. Provider Side (OTAProviderCluster,
    ota-provider-delegate.h) 4b. Requestor Side (OTARequestorCluster,
    CodegenIntegration)
5.  Layer 2 — Core State Machine (OTARequestorInterface + DefaultOTARequestor)
6.  Layer 3 — Policy / Driver (OTARequestorDriver + DefaultOTARequestorDriver)
7.  Layer 4 — Transport Abstraction (OTADownloader + BDXDownloader)
8.  Layer 5 — Persistent State (OTARequestorStorage +
    DefaultOTARequestorStorage)
9.  Layer 6 — Platform Image Processing (OTAImageProcessorInterface)
10. ESP32 OTAImageProcessorImpl — the concrete platform implementation 10a.
    Class structure and member fields 10b. The async dispatch pattern
    (ScheduleWork) 10c. PrepareDownload — HandlePrepareDownload 10d.
    ProcessBlock — HandleProcessBlock 10e. Finalize — HandleFinalize 10f. Apply
    — HandleApply 10g. Abort — HandleAbort 10h. IsFirstImageRun and
    ConfirmCurrentImage 10i. ProcessHeader — OTA file header stripping 10j.
    SetBlock / ReleaseBlock — block buffer management 10k. Optional: Encrypted
    OTA (CONFIG_ENABLE_ENCRYPTED_OTA) 10l. Optional: Delta OTA
    (CONFIG_ENABLE_DELTA_OTA) 10m. Optional: RCP co-processor OTA
    (CONFIG_OPENTHREAD_BORDER_ROUTER)
11. Attribute Bookkeeping (OTARequestorAttributes)
12. End-to-End Flow — annotated step by step
13. State Machine Diagram
14. Timers and Watchdogs
15. Error / Recovery Paths
16. How to Wire Everything Up — ESP32 example app (OTAHelper + main.cpp)
17. File & Symbol Quick Reference

---

1. Two-Node Architecture

---

In Matter OTA there are always two devices on the fabric:

    OTA Provider                        OTA Requestor
    ────────────                        ─────────────
    Has the new firmware file.          Wants the new firmware.
    Runs OtaSoftwareUpdateProvider      Runs OtaSoftwareUpdateRequestor
    cluster server.                     cluster server (yes, also a server —
                                        the AnnounceOTAProvider command comes
                                        in to the Requestor).

    Serves the image over BDX           Downloads the image over BDX
    (Sender + Responder role).          (Receiver + Initiator role).

The Requestor is always the initiator of the BDX session; the Provider is the
BDX responder. All three cluster commands (QueryImage, ApplyUpdateRequest,
NotifyUpdateApplied) go from Requestor → Provider. The Provider never calls
back.

---

2. The Six-Layer Requestor Stack

---

The Requestor side is intentionally split into six layers so that each piece can
be replaced or mocked independently.

    ┌─────────────────────────────────────────────┐
    │  OTARequestorCluster  (cluster server impl)  │  ← receives cluster commands
    ├─────────────────────────────────────────────┤
    │  OTARequestorInterface / DefaultOTARequestor │  ← state machine
    ├─────────────────────────────────────────────┤
    │  OTARequestorDriver / DefaultOTARequestorDriver│ ← policy decisions
    ├─────────────────────────────────────────────┤
    │  OTADownloader / BDXDownloader              │  ← BDX transport
    ├─────────────────────────────────────────────┤
    │  OTARequestorStorage / DefaultOTARequestorStorage│ ← KVS persistence
    ├─────────────────────────────────────────────┤
    │  OTAImageProcessorInterface                 │  ← platform flash writing
    └─────────────────────────────────────────────┘

All of the layers are pure abstract interfaces except the Default\* concrete
classes, which are the SDK's ready-to-use implementations. The platform (ESP32,
Silabs, etc.) must provide an OTAImageProcessorInterface implementation;
everything else has a usable default.

---

3. Layer 0 — OTA Image File Format

---

File: src/lib/core/OTAImageHeader.h

Before any layer above processes a downloaded byte, the file itself has a fixed
on-disk format defined here.

    // File magic — first 4 bytes of every valid OTA image
    inline constexpr uint32_t kOTAImageFileIdentifier = 0x1BEEF11E;
                                               // OTAImageHeader.h:30

The OTAImageHeader struct carries the decoded header fields:

    struct OTAImageHeader               // OTAImageHeader.h:48
    {
        uint16_t mVendorId;             // must match the Requestor's VendorID
        uint16_t mProductId;            // must match the Requestor's ProductID
        uint32_t mSoftwareVersion;      // numeric version — compared against current
        CharSpan mSoftwareVersionString;// human readable e.g. "1.2.3"
        uint64_t mPayloadSize;          // byte count of the payload after the header
        Optional<uint32_t> mMinApplicableVersion;  // min version this update can be
        Optional<uint32_t> mMaxApplicableVersion;  // applied over (optional)
        CharSpan mReleaseNotesURL;
        OTAImageDigestType mImageDigestType;       // e.g. kSha256
        ByteSpan mImageDigest;          // actual hash bytes
    };

The header is TLV-encoded with a fixed 4-byte magic + 4-byte TLV-length prefix,
then TLV data. The parser:

    class OTAImageHeaderParser          // OTAImageHeader.h:63
    {
        void   Init();                  // reset; call before each new image
        CHIP_ERROR AccumulateAndDecode(ByteSpan & buffer, OTAImageHeader & header);
            // Takes successive chunks. Returns CHIP_ERROR_BUFFER_TOO_SMALL until the
            // full header is collected, then CHIP_NO_ERROR + fills header.
            // Returns CHIP_ERROR_INVALID_FILE_IDENTIFIER if magic is wrong.
    };

The parser is typically called inside
OTAImageProcessorInterface::ProcessBlock(). When AccumulateAndDecode returns
CHIP_NO_ERROR the header is known and the remainder of the ByteSpan is the start
of the actual firmware payload.

---

4. Layer 1 — Cluster Command Handling

---

### 4a. Provider Side

Files: src/app/clusters/ota-provider/OTAProviderCluster.h
src/app/clusters/ota-provider/OTAProviderCluster.cpp
src/app/clusters/ota-provider/ota-provider-delegate.h

The Provider exposes two classes:

    class OtaProviderLogic              // OTAProviderCluster.h:33
    class OtaProviderServer             // OTAProviderCluster.h:65
        : public DefaultServerCluster
        , private OtaProviderLogic

OtaProviderServer is the Matter data-model server object. It handles the three
inbound commands by decoding them and calling through OtaProviderLogic, which
then calls the application-supplied delegate.

The three commands the Provider server accepts (OTAProviderCluster.cpp:37):

    constexpr DataModel::AcceptedCommandEntry kAcceptedCommands[] = {
        QueryImage::kMetadataEntry,
        ApplyUpdateRequest::kMetadataEntry,
        NotifyUpdateApplied::kMetadataEntry,
    };

The two commands the Provider generates as responses:

    constexpr CommandId kGeneratedCommands[] = {
        QueryImageResponse::Id,
        ApplyUpdateResponse::Id,
    };

The application must set a delegate via:

    otaProviderServer.SetDelegate(&myDelegate);

The delegate interface (ota-provider-delegate.h:36):

    class OTAProviderDelegate
    {
        virtual void HandleQueryImage(
            CommandHandler *, const ConcreteCommandPath &,
            const OtaSoftwareUpdateProvider::Commands::QueryImage::DecodableType &) = 0;

        virtual void HandleApplyUpdateRequest(
            CommandHandler *, const ConcreteCommandPath &,
            const OtaSoftwareUpdateProvider::Commands::ApplyUpdateRequest::DecodableType &) = 0;

        virtual void HandleNotifyUpdateApplied(
            CommandHandler *, const ConcreteCommandPath &,
            const OtaSoftwareUpdateProvider::Commands::NotifyUpdateApplied::DecodableType &) = 0;
    };

What the application's HandleQueryImage does in practice:

1. Check vendorId / productId match.
2. Check softwareVersion is older than what we have.
3. Build a BDX URI of the form "bdx://<nodeId>/<fileDesignator>".
4. Call commandObj->AddResponse() with QueryImageResponse containing {
   status=kUpdateAvailable, imageURI, softwareVersion, updateToken, ... }.
5. Optionally set up a BDX transfer server to serve the file when the Requestor
   connects back.

The Provider has no persistent state — it is stateless on the SDK side; all
state lives in the application's delegate implementation.

Validation performed by OtaProviderLogic before calling the delegate:

-   updateToken must be 8–32 bytes (kUpdateTokenMinLength /
    kUpdateTokenMaxLength, OTAProviderCluster.cpp:33).
-   location must be exactly 2 characters.
-   metadata must be ≤512 bytes.

### 4b. Requestor Side

Files: src/app/clusters/ota-requestor/OTARequestorCluster.h
src/app/clusters/ota-requestor/OTARequestorCluster.cpp
src/app/clusters/ota-requestor/CodegenIntegration.h
src/app/clusters/ota-requestor/CodegenIntegration.cpp

    class OTARequestorCluster           // OTARequestorCluster.h:28
        : public DefaultServerCluster
        , public DefaultOTARequestorEventGenerator
        , public OTARequestorAttributes::AttributeChangeListener

OTARequestorCluster is the Matter data-model server for the
OtaSoftwareUpdateRequestor cluster. It:

-   Reads attributes (UpdateState, UpdateStateProgress, DefaultOTAProviders,
    ...).
-   Writes DefaultOTAProviders (which persists via OTARequestorAttributes →
    Storage).
-   Handles the one inbound command: AnnounceOTAProvider.
-   Generates events: StateTransition, VersionApplied, DownloadError.

AnnounceOTAProvider is routed to the core logic via:

    OTARequestorCommandInterface & mOtaCommands;   // OTARequestorCluster.h:62

OTARequestorCommandInterface is a narrow interface
(OTARequestorInterface.h:142):

    class OTARequestorCommandInterface {
        virtual void HandleAnnounceOTAProvider(
            CommandHandler *, const ConcreteCommandPath &,
            const AnnounceOTAProvider::DecodableType &) = 0;
    };

DefaultOTARequestor implements this; its HandleAnnounceOTAProvider delegates to
the driver (DefaultOTARequestor.cpp:347):

    mOtaRequestorDriver->ProcessAnnounceOTAProviders(providerLocation,
                                                     announcementReason);

CodegenIntegration.{h,cpp} wires the cluster server object created by
code-generation (ZAP) to the runtime DefaultOTARequestor singleton via a global
hook:

    void (*gInternalOnSetRequestorInstance)(OTARequestorInterface *) = nullptr;
                                            // DefaultOTARequestor.cpp:51

When SetRequestorInstance() is called during app init, this callback fires and
plugs the requestor into whatever cluster server object the code-gen created.

---

5. Layer 2 — Core State Machine

---

Files: src/app/clusters/ota-requestor/OTARequestorInterface.h
src/app/clusters/ota-requestor/DefaultOTARequestor.h
src/app/clusters/ota-requestor/DefaultOTARequestor.cpp

### 5.1 OTARequestorInterface

OTARequestorInterface (OTARequestorInterface.h:158) is the public API of the
entire Requestor subsystem. Any code that wants to drive OTA talks to this
interface.

Key methods:

    CHIP_ERROR TriggerImmediateQuery(FabricIndex = kUndefined);
        // App calls this to start OTA immediately (e.g. button press).
        // Picks a provider from the default list and begins.

    void TriggerImmediateQueryInternal();
        // Driver calls this internally after choosing a provider.
        // Transitions state → kQuerying and calls ConnectToProvider(kQueryImage).

    void DownloadUpdate();
        // Driver calls this after UpdateAvailable() to start BDX download.
        // Transitions state → kDownloading and calls ConnectToProvider(kDownload).

    void ApplyUpdate();
        // Driver calls this after UpdateDownloaded() to send ApplyUpdateRequest.
        // Transitions state → kApplying.

    void NotifyUpdateApplied();
        // Driver calls this after ConfirmCurrentImage() to send NotifyUpdateApplied.
        // This is the last step; clears all state.

    void CancelImageUpdate();
        // App or watchdog calls this to abort. Calls BDXDownloader::EndDownload(),
        // then driver->UpdateCancelled(), then Reset().

    OTAUpdateStateEnum GetCurrentUpdateState();
        // Returns kIdle / kQuerying / kDownloading / kApplying / kRollingBack / ...

The global singleton accessors:

    void SetRequestorInstance(OTARequestorInterface * instance);   // line 232
    OTARequestorInterface * GetRequestorInstance();                 // line 235

These are called during init and from ZAP-generated cluster code to find the
Requestor object without needing a direct pointer.

ProviderLocationList (OTARequestorInterface.h:40) is a fixed-size array
(CHIP_CONFIG_MAX_FABRICS entries) of Optional<ProviderLocation> structs. It has
an Iterator that skips empty slots.

### 5.2 DefaultOTARequestor

DefaultOTARequestor (DefaultOTARequestor.h:39) is the concrete implementation.

Member fields:

    OTARequestorStorage * mStorage;         // KVS back-end
    OTARequestorDriver  * mOtaRequestorDriver;  // policy layer
    CASESessionManager  * mCASESessionManager;  // CASE session pool
    OTARequestorAttributes * mAttributes;   // cluster attribute cache
    BDXDownloader       * mBdxDownloader;   // transport layer
    BDXMessenger          mBdxMessenger;    // internal exchange adapter

    OnConnectedAction mOnConnectedAction;   // what to do once CASE is up:
        // kQueryImage | kDownload | kApplyUpdate | kNotifyUpdateApplied

    uint8_t  mUpdateTokenBuffer[32];        // opaque token from Provider
    ByteSpan mUpdateToken;
    uint32_t mCurrentVersion;
    uint32_t mTargetVersion;
    char     mFileDesignatorBuffer[bdx::kMaxFileDesignatorLen];
    CharSpan mFileDesignator;               // BDX file path e.g. "/firmware.bin"
    Optional<ProviderLocationType> mProviderLocation;  // current Provider CASE target

Init (DefaultOTARequestor.cpp:117):

    CHIP_ERROR DefaultOTARequestor::Init(
        Server & server,
        OTARequestorStorage & storage,
        OTARequestorDriver & driver,
        BDXDownloader & downloader,
        OTARequestorAttributes & attributes,
        DefaultOTARequestorEventGenerator & eventGenerator)
    {
        // Store all pointers.
        // Read current software version from ConfigurationMgr.
        // Call LoadCurrentUpdateInfo() to restore in-flight state from KVS.
        // Register OnCommissioningCompleteRequestor event handler.
    }

ConnectToProvider (DefaultOTARequestor.cpp:352):

    void DefaultOTARequestor::ConnectToProvider(OnConnectedAction action)
    {
        mOnConnectedAction = action;
        mCASESessionManager->FindOrEstablishSession(
            GetProviderScopedId(),          // NodeId + FabricIndex
            &mOnConnectedCallback,          // success → OnConnected()
            &mOnConnectionFailureCallback); // failure → OnConnectionFailure()
    }

OnConnected (DefaultOTARequestor.cpp:432) dispatches on mOnConnectedAction:

    case kQueryImage:   SendQueryImageRequest(exchangeMgr, session);
    case kDownload:     StartDownload(exchangeMgr, session);
    case kApplyUpdate:  SendApplyUpdateRequest(exchangeMgr, session);
    case kNotifyUpdateApplied: SendNotifyUpdateAppliedRequest(exchangeMgr, session);

SendQueryImageRequest (DefaultOTARequestor.cpp:725) fills the QueryImage command
from device info:

    QueryImage::Type args;
    args.vendorID   = <from DeviceInstanceInfoProvider>;
    args.productID  = <from DeviceInstanceInfoProvider>;
    args.softwareVersion = <from ConfigurationMgr>;
    args.protocolsSupported = { OTADownloadProtocol::kBDXSynchronous };
    args.requestorCanConsent = !localConfigDisabled && driver->CanConsent();
    args.hardwareVersion = <optional>;
    args.location        = <2-char country code or "XX">;
    args.metadataForProvider = <optional opaque bytes>;

    cluster.InvokeCommand(args, this, OnQueryImageResponse, OnQueryImageFailure);

OnQueryImageResponse (DefaultOTARequestor.cpp:137) handles three status values:

    case kUpdateAvailable:
        ExtractUpdateDescription(response, update);
        // Validates imageURI (must be a BDX URI), checks version > current.
        // Stores token, version, file designator.
        mOtaRequestorDriver->UpdateAvailable(update, delay);
        break;

    case kBusy:
        mOtaRequestorDriver->UpdateNotFound(kBusy, delay);
        // → driver retries same or next provider after delay
        break;

    case kNotAvailable:
        mOtaRequestorDriver->UpdateNotFound(kNotAvailable, delay);
        // → driver tries next provider
        break;

StartDownload (DefaultOTARequestor.cpp:797) sets up BDX:

    TransferSession::TransferInitData initOptions;
    initOptions.TransferCtlFlags = bdx::TransferControlFlags::kReceiverDrive;
    initOptions.MaxBlockSize     = driver->GetMaxDownloadBlockSize(); // 1024 default
    initOptions.FileDesignator   = mFileDesignator.data();

    exchangeCtx = exchangeMgr.NewContext(session, &mBdxMessenger);
    mBdxMessenger.Init(mBdxDownloader, exchangeCtx);
    mBdxDownloader->SetBDXParams(initOptions, kDownloadTimeoutSec /* 5 min */);
    mBdxDownloader->BeginPrepareDownload();

StoreCurrentUpdateInfo / LoadCurrentUpdateInfo
(DefaultOTARequestor.cpp:864, 912) are called:

-   After every state transition (so a crash can be recovered).
-   On Init() (to resume after power cycle if update was in progress).

They persist: providerLocation, updateToken, updateState, targetVersion.

### 5.3 BDXMessenger (inner class)

BDXMessenger (DefaultOTARequestor.h:138) sits between the CHIP messaging layer
and BDXDownloader.

SendMessage (DefaultOTARequestor.h:141):

-   Called by BDXDownloader when it has a BDX message to send (ReceiveInit,
    BlockQuery, BlockAck, BlockAckEOF).
-   Uses mExchangeCtx->SendMessage().
-   Sets kExpectResponse flag for all messages except BlockAckEOF and
    StatusReport.

OnMessageReceived (DefaultOTARequestor.h:162):

-   Called by CHIP messaging when the Provider sends back a BDX message
    (SendAccept, Block, BlockEOF).
-   Passes the raw payload to mDownloader->OnMessageReceived().

OnResponseTimeout (DefaultOTARequestor.h:182):

-   Called when the exchange context times out.
-   Calls mDownloader->OnDownloadTimeout() which aborts the BDX session.

---

6. Layer 3 — Policy / Driver

---

Files: src/app/clusters/ota-requestor/OTARequestorDriver.h
src/app/clusters/ota-requestor/DefaultOTARequestorDriver.h
src/app/clusters/ota-requestor/DefaultOTARequestorDriver.cpp

### 6.1 OTARequestorDriver (abstract interface)

The Driver makes all policy decisions: when to query, which provider to pick,
whether the user must consent, how long to delay before applying, etc.

Key callbacks (OTARequestorDriver.h:71):

    virtual bool CanConsent();
        // Should we ask user before downloading? Default returns false.

    virtual uint16_t GetMaxDownloadBlockSize();
        // BDX block size. Default 1024. Increase for faster downloads if RAM allows.

    virtual void UpdateAvailable(const UpdateDescription & update, Seconds32 delay);
        // Called by core when kUpdateAvailable received.
        // Driver decides: download now, or delay?

    virtual CHIP_ERROR UpdateNotFound(UpdateNotFoundReason reason, Seconds32 delay);
        // Called when provider says kBusy / kNotAvailable / kUpToDate.
        // Driver decides: retry same provider, try next, or sleep?

    virtual void UpdateDownloaded();
        // Called when BDX transfer is complete.
        // Driver calls requestor->ApplyUpdate().

    virtual void UpdateConfirmed(Seconds32 delay);
        // Called when ApplyUpdateResponse.action = kProceed.
        // Driver calls imageProcessor->Apply() (with optional delay).

    virtual void UpdateSuspended(Seconds32 delay);
        // Called when ApplyUpdateResponse.action = kAwaitNextAction.
        // Driver schedules ApplyUpdate retry after delay.

    virtual void UpdateDiscontinued();
        // Called when ApplyUpdateResponse.action = kDiscontinue.
        // Driver calls imageProcessor->Abort().

    virtual void OTACommissioningCallback();
        // Called when device first joins a fabric.
        // Driver schedules an initial QueryImage after 30 s.

    virtual void ProcessAnnounceOTAProviders(
        const ProviderLocationType &, OTAAnnouncementReason);
        // Called when AnnounceOTAProvider command arrives.
        // Driver decides if this supersedes any in-progress update.

    virtual void SendQueryImage();
        // Internal: driver triggers the actual QueryImage send.

    virtual bool GetNextProviderLocation(ProviderLocationType &, bool & listExhausted);
        // Called to pick the next provider from the default list.

UpdateDescription struct (OTARequestorDriver.h:35):

    struct UpdateDescription {
        NodeId   nodeId;            // Provider's node ID (from BDX URI)
        CharSpan fileDesignator;    // BDX file path
        uint32_t softwareVersion;
        CharSpan softwareVersionStr;
        ByteSpan updateToken;
        bool     userConsentNeeded;
        ByteSpan metadataForRequestor;
    };

### 6.2 DefaultOTARequestorDriver (concrete)

DefaultOTARequestorDriver (DefaultOTARequestorDriver.h:32) provides a
fully-working implementation with sane defaults.

Init (DefaultOTARequestorDriver.cpp:67):

    void DefaultOTARequestorDriver::Init(
        OTARequestorInterface * requestor,
        OTAImageProcessorInterface * processor)
    {
        mRequestor      = requestor;
        mImageProcessor = processor;

        if (mImageProcessor->IsFirstImageRun())
        {
            // We just booted the new image for the first time.
            // Confirm it (marks it as valid so rollback doesn't happen).
            mImageProcessor->ConfirmCurrentImage();
            // Tell the Provider we applied successfully.
            mRequestor->NotifyUpdateApplied();
        }
        else if (requestor state is not kIdle)
        {
            // Something went wrong before reboot; reset.
            mRequestor->Reset();
        }
        else
        {
            // Normal boot — start the periodic query timer.
            StartSelectedTimer(SelectedTimer::kPeriodicQueryTimer);
        }
    }

Timer strategy:

-   Exactly one timer runs at a time: either the periodic query timer or the
    watchdog.
-   Periodic query timer (default 24 h): fires PeriodicQueryTimerHandler which
    calls GetNextProviderLocation + SendQueryImage.
-   Watchdog timer (default 6 h): fires if an update gets stuck in a non-Idle
    state. Calls UpdateDiscontinued() + CancelImageUpdate() + restarts periodic
    timer.
-   Switching happens in StartSelectedTimer()
    (DefaultOTARequestorDriver.cpp:438).

UpdateAvailable (DefaultOTARequestorDriver.cpp:204): The default implementation
always downloads unconditionally: ScheduleDelayedAction(delay,
DownloadUpdateTimerHandler, this); // → calls requestor->DownloadUpdate() after
`delay` seconds

UpdateNotFound (DefaultOTARequestorDriver.cpp:213): kBusy → retry same provider
(max 3 times), then try next. kNotAvail → try next provider. kUpToDate → do
nothing; periodic timer will try again later.

GetNextProviderLocation (DefaultOTARequestorDriver.cpp:458): Walks the
DefaultOTAProviders list as a circular list. Finds the last used provider and
returns the one after it. If the end is reached (listExhausted=true), wraps back
to the beginning.

Provider retry constants (DefaultOTARequestorDriver.cpp:50–51): constexpr
uint8_t kMaxInvalidSessionRetries = 1; constexpr uint32_t
kDelayQueryUponCommissioningSec = 30; constexpr uint32_t kImmediateStartDelaySec
= 1; // for urgent announce constexpr Seconds32 kDefaultDelayedActionTime = 120
s; // floor for delays

---

7. Layer 4 — Transport Abstraction

---

Files: src/app/clusters/ota-requestor/OTADownloader.h
src/app/clusters/ota-requestor/BDXDownloader.h
src/app/clusters/ota-requestor/BDXDownloader.cpp

### 7.1 OTADownloader (abstract)

OTADownloader (OTADownloader.h:36) abstracts the download transport. Matter only
defines BDX but this interface allows HTTPS or other protocols in theory.

States (OTADownloader.h:39): kIdle — no transfer active kPreparing — waiting for
imageProcessor->PrepareDownload() to complete kInProgress — BDX blocks flowing
kComplete — BlockAckEOF sent; transfer done

Methods: CHIP_ERROR BeginPrepareDownload(); // Calls
imageProcessor->PrepareDownload(). // Moves to kPreparing; doesn't send anything
yet.

    CHIP_ERROR OnPreparedForDownload(CHIP_ERROR status);
        // Platform calls this from PrepareDownload() callback.
        // If status==OK, moves to kInProgress and sends ReceiveInit via BDX.

    void EndDownload(CHIP_ERROR reason = CHIP_NO_ERROR);
        // Abort or graceful end. Sends BDX StatusReport (AbortTransfer).

    CHIP_ERROR FetchNextData();
        // Driver-mode: send a BlockQuery to request the next block.

    CHIP_ERROR SkipData(uint32_t numBytes);
        // Send BlockQueryWithSkip to resume after a partial write.

    void SetImageProcessorDelegate(OTAImageProcessorInterface *);
    OTAImageProcessorInterface * GetImageProcessorDelegate();

### 7.2 BDXDownloader (concrete)

BDXDownloader (BDXDownloader.h:37) wraps a bdx::TransferSession and drives it in
Receiver-Drive mode (Requestor sends BlockQuery, Provider replies with Block).

SetBDXParams (BDXDownloader.cpp:96):

    CHIP_ERROR BDXDownloader::SetBDXParams(
        const TransferSession::TransferInitData & bdxInitData,
        System::Clock::Timeout timeout)
    {
        mTimeout = timeout;
        mBdxTransfer.Reset();
        // StartTransfer with kReceiver role copies the TransferInitData
        // into the session object (important: must happen before the data
        // pointed to by initData is freed).
        mBdxTransfer.StartTransfer(bdx::TransferRole::kReceiver, bdxInitData,
                                   System::Clock::Seconds16(30));
    }

BeginPrepareDownload (BDXDownloader.cpp:113):

    CHIP_ERROR BDXDownloader::BeginPrepareDownload()
    {
        // Start the timeout watchdog timer.
        SystemLayer().StartTimer(mTimeout, TransferTimeoutCheckHandler, this);
        // Ask the platform to prepare flash storage.
        mImageProcessor->PrepareDownload();
        // → platform calls back OnPreparedForDownload() when ready.
        SetState(kPreparing, ...);
    }

OnPreparedForDownload (BDXDownloader.cpp:131):

    CHIP_ERROR BDXDownloader::OnPreparedForDownload(CHIP_ERROR status)
    {
        if (status == CHIP_NO_ERROR)
        {
            SetState(kInProgress, ...);
            // StartTransfer() prepared a ReceiveInit message internally.
            // PollTransferSession() drains it → mMsgDelegate->SendMessage(ReceiveInit).
            PollTransferSession();
        }
    }

PollTransferSession (BDXDownloader.cpp:225) — the heart of BDX:

    void BDXDownloader::PollTransferSession()
    {
        TransferSession::OutputEvent outEvent;
        do {
            mBdxTransfer.PollOutput(outEvent, System::Clock::Seconds16(0));
            HandleBdxEvent(outEvent);
        } while (outEvent.EventType != TransferSession::OutputEventType::kNone);
    }

HandleBdxEvent (BDXDownloader.cpp:250) switch:

    kAcceptReceived  → PrepareBlockQuery()   // Provider accepted our ReceiveInit
    kMsgToSend       → mMsgDelegate->SendMessage(outEvent)   // send BlockQuery etc.
                       if BlockAckEOF was just sent → SetState(kComplete)
    kBlockReceived   → mImageProcessor->ProcessBlock(blockData)
                       if IsEof → PrepareBlockAck() + mImageProcessor->Finalize()
    kStatusReceived  → CleanupOnError(kFailure)  // Provider aborted
    kInternalError   → CleanupOnError(kFailure)
    kTransferTimeout → CleanupOnError(kTimeOut)

Timeout detection (BDXDownloader.cpp:39):

    void TransferTimeoutCheckHandler(System::Layer *, void * appState)
    {
        BDXDownloader * dl = static_cast<BDXDownloader *>(appState);
        if (dl->HasTransferTimedOut())
            dl->OnDownloadTimeout();
        else
            systemLayer->StartTimer(dl->GetTimeout(), TransferTimeoutCheckHandler, dl);
    }

HasTransferTimedOut compares the BDX block counter between consecutive timer
ticks. If it didn't advance (no new blocks), the transfer is considered stuck.

---

8. Layer 5 — Persistent State

---

Files: src/app/clusters/ota-requestor/OTARequestorStorage.h
src/app/clusters/ota-requestor/DefaultOTARequestorStorage.h
src/app/clusters/ota-requestor/DefaultOTARequestorStorage.cpp

OTARequestorStorage (OTARequestorStorage.h:29) defines five groups of KVS keys:

    // Default Providers list (written when admin writes the attribute)
    CHIP_ERROR StoreDefaultProviders(const ProviderLocationList &);
    CHIP_ERROR LoadDefaultProviders(ProviderLocationList &);

    // Current Provider location (in-progress update)
    CHIP_ERROR StoreCurrentProviderLocation(const ProviderLocationType &);
    CHIP_ERROR ClearCurrentProviderLocation();
    CHIP_ERROR LoadCurrentProviderLocation(ProviderLocationType &);

    // Update token (opaque blob from Provider)
    CHIP_ERROR StoreUpdateToken(ByteSpan);
    CHIP_ERROR LoadUpdateToken(MutableByteSpan &);
    CHIP_ERROR ClearUpdateToken();

    // Update state enum (kQuerying / kDownloading / kApplying / ...)
    CHIP_ERROR StoreCurrentUpdateState(OTAUpdateStateEnum);
    CHIP_ERROR LoadCurrentUpdateState(OTAUpdateStateEnum &);
    CHIP_ERROR ClearCurrentUpdateState();

    // Target version
    CHIP_ERROR StoreTargetVersion(uint32_t);
    CHIP_ERROR LoadTargetVersion(uint32_t &);
    CHIP_ERROR ClearTargetVersion();

DefaultOTARequestorStorage uses PersistentStorageDelegate (the generic KVS
interface) to serialize these as TLV into the platform's NVS / KVS partition.

Why persistence matters: During download the device might reboot (power loss,
crash). On the next boot, Init() calls LoadCurrentUpdateInfo() to restore state.
If state shows kDownloading, it was in the middle of a download that never
finished — the driver resets to Idle and the periodic timer will query the
Provider again. If state shows kApplying and IsFirstImageRun() is true, the
download succeeded and the new image is running — driver calls
ConfirmCurrentImage() + NotifyUpdateApplied().

---

9. Layer 6 — Platform Image Processing

---

File: src/include/platform/OTAImageProcessor.h

OTAImageProcessorInterface (OTAImageProcessor.h:44) is the only layer the
platform MUST implement. All other layers have defaults.

The seven methods:

    CHIP_ERROR PrepareDownload();
        // Called before the first block arrives.
        // Platform: open the OTA partition, erase flash, allocate buffers.
        // MUST call OTADownloader::OnPreparedForDownload() when ready (can be async).
        // Must not block.

    CHIP_ERROR ProcessBlock(ByteSpan & block);
        // Called for every BDX data block (typically 1024 bytes).
        // Platform: parse OTA header, then write firmware bytes to flash.
        // Must not block (write to a buffer, defer actual flash write if needed).

    CHIP_ERROR Finalize();
        // Called after the last block (IsEof = true).
        // Platform: flush any remaining buffered data, verify image digest.

    CHIP_ERROR Apply();
        // Called after ApplyUpdateRequest returns kProceed.
        // Platform: set next boot partition + reboot.

    CHIP_ERROR Abort();
        // Called on any error, or if the update is cancelled.
        // Platform: erase incomplete data, release buffers.

    bool IsFirstImageRun();
        // Called during driver Init().
        // Returns true if this boot is the first boot of a newly applied image.
        // ESP32: compare running partition address with ota_next.

    CHIP_ERROR ConfirmCurrentImage();
        // Called if IsFirstImageRun() is true.
        // Platform: mark the running image as valid (cancel rollback).
        // ESP32: esp_ota_mark_app_valid_cancel_rollback().

Progress tracking (OTAImageProcessor.h:81):

    virtual app::DataModel::Nullable<uint8_t> GetPercentComplete()
    {
        return (mParams.totalFileBytes > 0)
            ? Nullable<uint8_t>((mParams.downloadedBytes * 100) / mParams.totalFileBytes)
            : Nullable<uint8_t>{};
    }
    virtual uint64_t GetBytesDownloaded() { return mParams.downloadedBytes; }

    OTAImageProgress mParams;   // { downloadedBytes, totalFileBytes }

The platform's ProcessBlock() must update mParams.downloadedBytes on each call.
BDXDownloader reads GetPercentComplete() after each block and reports it to the
core via OnUpdateProgressChanged(), which sets the UpdateStateProgress cluster
attribute.

---

10. ESP32 OTAImageProcessorImpl — the concrete platform implementation

---

Files: src/platform/ESP32/OTAImageProcessorImpl.h
src/platform/ESP32/OTAImageProcessorImpl.cpp

This is the only layer the ESP32 platform must supply. Everything above it
(BDXDownloader, DefaultOTARequestor, DefaultOTARequestorDriver, storage) is
generic and already implemented by the SDK. This class owns all interaction with
ESP-IDF's OTA APIs: `esp_ota_begin`, `esp_ota_write`, `esp_ota_end`,
`esp_ota_set_boot_partition`, and `esp_ota_mark_app_valid_cancel_rollback`.

### 10a. Class structure and member fields

    class OTAImageProcessorImpl : public OTAImageProcessorInterface
                                            // OTAImageProcessorImpl.h:38
    {
        OTADownloader * mDownloader;         // back-pointer to BDXDownloader
        MutableByteSpan mBlock;             // heap-allocated copy of current BDX block
        const esp_partition_t * mOTAUpdatePartition; // next OTA slot partition
        esp_ota_handle_t mOTAUpdateHandle;  // IDF OTA context

        OTAImageHeaderParser mHeaderParser; // strips the Matter OTA file header

        // Optional: Delta OTA members
        esp_delta_ota_handle_t mDeltaOTAUpdateHandle;
        esp_delta_ota_cfg_t    deltaOtaCfg;
        bool patchHeaderVerified;
        bool chipIdVerified;

        // Optional: Encrypted OTA members
        CharSpan mKey;                       // RSA-3072 PEM key
        bool mEncryptedOTAEnabled;
        esp_decrypt_handle_t mOTADecryptionHandle;

        // Optional: RCP delegate (Border Router)
        OTARcpProcessorDelegate * mOtaRcpDelegate;  // set by app if Thread + RCP
    };

The back-pointer `mDownloader` is set by the app before Init (it is not passed
to the constructor). It is used in exactly one place: to call
`mDownloader->OnPreparedForDownload()` from inside `HandlePrepareDownload()`,
because the BDXDownloader needs to know when the flash is ready before it sends
ReceiveInit.

### 10b. The async dispatch pattern (ScheduleWork)

Every public method in OTAImageProcessorInterface (`PrepareDownload`,
`ProcessBlock`, `Finalize`, `Apply`, `Abort`) is called from the CHIP Matter
task. ESP-IDF flash operations are NOT safe to call from any arbitrary task —
they need proper locking and may need to run on the main task.

The solution used here is `PlatformMgr().ScheduleWork(handler, context)`. Every
public method schedules a static function to run on the Matter platform task
with `this` pointer as the argument, then returns immediately:

    CHIP_ERROR OTAImageProcessorImpl::PrepareDownload()      // .cpp:101
    {
        DeviceLayer::PlatformMgr().ScheduleWork(HandlePrepareDownload,
                                                reinterpret_cast<intptr_t>(this));
        return CHIP_NO_ERROR;   // ← returns immediately; actual work is async
    }

The same pattern is repeated for ProcessBlock (line 125), Finalize (line 107),
Apply (line 113), Abort (line 119).

ProcessBlock additionally copies the block data to heap before scheduling,
because the ByteSpan passed in is a view into a BDX packet buffer that may be
recycled:

    CHIP_ERROR OTAImageProcessorImpl::ProcessBlock(ByteSpan & block)  // .cpp:125
    {
        CHIP_ERROR err = SetBlock(block);   // heap-allocates mBlock, copies data
        if (err != CHIP_NO_ERROR) return err;
        PlatformMgr().ScheduleWork(HandleProcessBlock, reinterpret_cast<intptr_t>(this));
        return CHIP_NO_ERROR;
    }

This is important: the BDXDownloader calls `FetchNextData()` (which sends
BlockQuery to the Provider) only AFTER `ProcessBlock()` returns. Since we return
immediately here, the BDX layer thinks the block was processed and immediately
asks for the next one. The actual flash write happens asynchronously on the
platform task.

If `HandleProcessBlock` encounters an error, it calls
`imageProcessor->mDownloader->EndDownload(error)` to abort the BDX session from
inside the platform task.

### 10c. PrepareDownload — HandlePrepareDownload

Source: OTAImageProcessorImpl.cpp:290

    void OTAImageProcessorImpl::HandlePrepareDownload(intptr_t context)
    {
        auto * imageProcessor = reinterpret_cast<OTAImageProcessorImpl *>(context);

        // 1. Find the passive OTA partition (the one not currently booted).
        imageProcessor->mOTAUpdatePartition = esp_ota_get_next_update_partition(NULL);
        if (!imageProcessor->mOTAUpdatePartition)
        {
            imageProcessor->mDownloader->OnPreparedForDownload(CHIP_ERROR_INTERNAL);
            return;
        }

        // 2. Begin OTA: erases the partition and returns a write handle.
        //    Normal OTA: OTA_WITH_SEQUENTIAL_WRITES (writes must be sequential).
        //    Delta OTA:  OTA_SIZE_UNKNOWN (patch size differs from final image size).
        esp_ota_begin(mOTAUpdatePartition,
                      OTA_WITH_SEQUENTIAL_WRITES,  // or OTA_SIZE_UNKNOWN for delta
                      &mOTAUpdateHandle);

        // 3. [Optional] Initialize delta OTA engine (esp_delta_ota_init).
        // 4. [Optional] Start decryption session (esp_encrypted_img_decrypt_start).
        // 5. [Optional] Call RCP delegate OnOtaRcpPrepareDownload().

        // 6. Reset the Matter OTA file header parser.
        imageProcessor->mHeaderParser.Init();

        // 7. Tell BDXDownloader we are ready — this triggers sending ReceiveInit.
        imageProcessor->mDownloader->OnPreparedForDownload(CHIP_NO_ERROR);

        // 8. Post kOtaDownloadInProgress event to the app.
        PostOTAStateChangeEvent(DeviceLayer::kOtaDownloadInProgress);
    }

Key ESP-IDF call: esp_ota_begin(partition, size, &handle) → erases the given OTA
partition and returns an opaque handle. → OTA_WITH_SEQUENTIAL_WRITES tells IDF
that writes will come in order (no random access needed), which lets it optimize
erasing ahead of writes. → OTA_SIZE_UNKNOWN is used for delta OTA because the
patch is smaller than the reconstructed image; IDF erase-on-write instead.

### 10d. ProcessBlock — HandleProcessBlock

Source: OTAImageProcessorImpl.cpp:460

    void OTAImageProcessorImpl::HandleProcessBlock(intptr_t context)
    {
        auto * imageProcessor = reinterpret_cast<OTAImageProcessorImpl *>(context);
        ByteSpan block(imageProcessor->mBlock.data(), imageProcessor->mBlock.size());

        // Step 1: strip the Matter OTA file header (first few blocks only).
        CHIP_ERROR error = imageProcessor->ProcessHeader(block);
        if (error != CHIP_NO_ERROR) {
            imageProcessor->mDownloader->EndDownload(error);
            PostOTAStateChangeEvent(DeviceLayer::kOtaDownloadFailed);
            return;
        }
        // After ProcessHeader(), `block` has been advanced past the header bytes.
        // For all subsequent blocks, ProcessHeader() is a no-op.

        ByteSpan blockToWrite = block;

        // Step 2 [Optional]: decrypt.
        // DecryptBlock() calls esp_encrypted_img_decrypt_data(), dynamically
        // allocates plain-text buffer, and returns a new ByteSpan pointing to it.
        // Caller must free(blockToWrite.data()) after writing.

        // Step 3: write to flash.
        //   Normal OTA:  esp_ota_write(mOTAUpdateHandle, data, size)
        //   Delta OTA:   esp_delta_ota_feed_patch(mDeltaOTAUpdateHandle, data, size)
        //     → the delta engine applies bsdiff + HEATSHRINK, reconstructing the
        //       full image and writing it to flash via DeltaOTAWriteCallback.
        //   Border Router: RCP delegate gets bytes first, remainder goes to
        //                  esp_ota_write for the host firmware.

        esp_err_t err = esp_ota_write(imageProcessor->mOTAUpdateHandle,
                                      blockToWrite.data(), blockToWrite.size());
        if (err != ESP_OK) {
            imageProcessor->mDownloader->EndDownload(CHIP_ERROR_WRITE_FAILED);
            PostOTAStateChangeEvent(DeviceLayer::kOtaDownloadFailed);
            return;
        }

        // Step 4: advance byte counter for progress reporting.
        imageProcessor->mParams.downloadedBytes += blockToWrite.size();

        // Step 5: ask BDXDownloader to fetch the next block.
        // This calls mBdxTransfer.PrepareBlockQuery() → sends BlockQuery to Provider.
        imageProcessor->mDownloader->FetchNextData();
    }

Important: `FetchNextData()` is called at the END of `HandleProcessBlock`, not
before. This creates back-pressure: the next BDX BlockQuery is sent only after
the current block is fully written to flash. If the write takes a long time the
Provider will just wait (no BlockQuery means no new Block comes).

### 10e. Finalize — HandleFinalize

Source: OTAImageProcessorImpl.cpp:359

    void OTAImageProcessorImpl::HandleFinalize(intptr_t context)
    {
        // [Optional] DecryptEnd — flush and verify decryption.
        // [Optional] esp_delta_ota_finalize + esp_delta_ota_deinit.
        // [Optional] RCP delegate OnOtaRcpFinalize.

        // Key call: validate and close the OTA write handle.
        esp_err_t err = esp_ota_end(imageProcessor->mOTAUpdateHandle);
        // esp_ota_end() internally validates the image digest (SHA-256 by default).
        // If ESP_ERR_OTA_VALIDATE_FAILED → image is corrupted.
        // If ESP_OK → flash image is valid and bootable.

        if (err == ESP_OK)
            PostOTAStateChangeEvent(DeviceLayer::kOtaDownloadComplete);
        else
            PostOTAStateChangeEvent(DeviceLayer::kOtaDownloadFailed);

        imageProcessor->ReleaseBlock();   // free heap block buffer
    }

After `esp_ota_end()` succeeds, the image is sitting in the passive OTA
partition, verified, but NOT yet set as the boot target. That happens in
`HandleApply`.

### 10f. Apply — HandleApply

Source: OTAImageProcessorImpl.cpp:549

    void OTAImageProcessorImpl::HandleApply(intptr_t context)
    {
        PostOTAStateChangeEvent(DeviceLayer::kOtaApplyInProgress);

        // Set the boot partition to the new image slot.
        esp_err_t err = esp_ota_set_boot_partition(imageProcessor->mOTAUpdatePartition);
        // This writes the OTA data partition (otadata) to indicate which app slot
        // to boot from next restart. If this call fails nothing changes at boot.

        if (err != ESP_OK)
        {
            PostOTAStateChangeEvent(DeviceLayer::kOtaApplyFailed);
            return;
        }

        PostOTAStateChangeEvent(DeviceLayer::kOtaApplyComplete);

        // CONFIG_OTA_AUTO_REBOOT_ON_APPLY (default ON):
        //   Schedule esp_restart() after CONFIG_OTA_AUTO_REBOOT_DELAY_MS ms.
        //   This gives the Matter stack time to finish the ApplyUpdate exchange.
        // If disabled:
        //   Logs "Please reboot the device manually".  The new image will boot on
        //   the next natural reboot, not immediately.
        DeviceLayer::SystemLayer().StartTimer(
            System::Clock::Milliseconds32(CONFIG_OTA_AUTO_REBOOT_DELAY_MS),
            HandleRestart, nullptr);
    }

    static void HandleRestart(Layer *, void *)   // .cpp:52
    {
        esp_restart();
    }

The delayed restart is important: if we called `esp_restart()` immediately, the
CHIP stack wouldn't have time to send the `BlockAckEOF` or complete the
`ApplyUpdateResponse` exchange. The timer lets the exchange close cleanly first.

### 10g. Abort — HandleAbort

Source: OTAImageProcessorImpl.cpp:428

    void OTAImageProcessorImpl::HandleAbort(intptr_t context)
    {
        // [Optional] DecryptAbort — release decryption handle.
        // [Optional] DeltaOTACleanUp — reset patch/chipId verified flags.
        // [Optional] RCP delegate OnOtaRcpAbort.

        // Discard all written data and release the OTA handle.
        esp_ota_abort(imageProcessor->mOTAUpdateHandle);
        // The passive partition is left erased (partially written data discarded).

        imageProcessor->ReleaseBlock();
        PostOTAStateChangeEvent(DeviceLayer::kOtaDownloadAborted);
    }

After `esp_ota_abort()`, the OTA data partition still points at the old boot
slot, so the device boots normally on the next restart.

### 10h. IsFirstImageRun and ConfirmCurrentImage

Source: OTAImageProcessorImpl.cpp:72–98

    bool OTAImageProcessorImpl::IsFirstImageRun()
    {
        OTARequestorInterface * requestor = GetRequestorInstance();
        // Returns true if the stored update state is kApplying.
        // This means: last boot set a new image AND set state to kApplying in KVS.
        // On this boot, that persisted state is loaded by LoadCurrentUpdateInfo(),
        // so GetCurrentUpdateState() returns kApplying.
        return requestor->GetCurrentUpdateState()
                   == OTAUpdateStateEnum::kApplying;
    }

Notice: this does NOT call `esp_ota_get_running_partition()` or compare
partition addresses. Instead, it checks the Matter KVS state. The reasoning: if
the Matter state machine reached kApplying and then the device rebooted (because
HandleApply called esp_restart()), then on the next boot the KVS still has
kApplying stored. That is the signal that we just booted the new image.

    CHIP_ERROR OTAImageProcessorImpl::ConfirmCurrentImage()
    {
        OTARequestorInterface * requestor = GetRequestorInstance();

        uint32_t currentVersion;
        ConfigurationMgr().GetSoftwareVersion(currentVersion);

        // Sanity check: the running version must match what was advertised.
        if (currentVersion != requestor->GetTargetVersion())
            return CHIP_ERROR_INCORRECT_STATE;

        // NOTE: Unlike other platforms (Silabs, NXP), the ESP32 implementation
        // does NOT call esp_ota_mark_app_valid_cancel_rollback() here.
        // On ESP32 the rollback protection is disabled by default; the app is
        // considered valid as soon as esp_ota_set_boot_partition() is called.
        // Rollback can be enabled via CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE;
        // if so, the app must call esp_ota_mark_app_valid_cancel_rollback()
        // itself before the watchdog fires.
        return CHIP_NO_ERROR;
    }

If `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is set in sdkconfig, the bootloader
will revert to the previous partition if
`esp_ota_mark_app_valid_cancel_rollback()` is not called within the watchdog
timeout. In that case, ConfirmCurrentImage should call it. The current SDK
implementation leaves this to the application.

### 10i. ProcessHeader — OTA file header stripping

Source: OTAImageProcessorImpl.cpp:612

    CHIP_ERROR OTAImageProcessorImpl::ProcessHeader(ByteSpan & block)
    {
        if (mHeaderParser.IsInitialized())
        {
            OTAImageHeader header;
            CHIP_ERROR error = mHeaderParser.AccumulateAndDecode(block, header);

            if (error == CHIP_ERROR_BUFFER_TOO_SMALL)
                return CHIP_NO_ERROR;    // need more data; silently consume block

            ReturnErrorOnFailure(error); // real parse error

            // Header parsed successfully.
            // block has been advanced to start at the firmware payload bytes.
            mParams.totalFileBytes = header.mPayloadSize;  // for % reporting
            mHeaderParser.Clear();  // parser is no longer active
        }
        return CHIP_NO_ERROR;  // block now points to raw firmware data
    }

This is called at the top of every `HandleProcessBlock()`. On the first few
blocks, `AccumulateAndDecode` buffers data until the complete TLV header is
seen. Once it returns CHIP_NO_ERROR, `mHeaderParser.Clear()` deactivates the
parser and all subsequent calls are instant no-ops (`IsInitialized()` returns
false after Clear).

After the header is parsed:

-   `mParams.totalFileBytes` is set from `header.mPayloadSize`, enabling percent
    calculation in `GetPercentComplete()`.
-   The remaining bytes in `block` are the raw ESP firmware image — these go
    straight to `esp_ota_write()`.

### 10j. SetBlock / ReleaseBlock — block buffer management

Source: OTAImageProcessorImpl.cpp:573–609

    CHIP_ERROR OTAImageProcessorImpl::SetBlock(ByteSpan & block)
    {
        if (block.empty()) { ReleaseBlock(); return CHIP_NO_ERROR; }

        // Only re-allocate if the new block is larger than what we have.
        if (mBlock.size() < block.size())
        {
            if (!mBlock.empty()) ReleaseBlock();
            uint8_t * ptr = Platform::MemoryAlloc(block.size());  // heap alloc
            if (!ptr) return CHIP_ERROR_NO_MEMORY;
            mBlock = MutableByteSpan(ptr, block.size());
        }

        CopySpanToMutableSpan(block, mBlock);   // deep copy
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR OTAImageProcessorImpl::ReleaseBlock()
    {
        if (mBlock.data()) Platform::MemoryFree(mBlock.data());
        mBlock = MutableByteSpan();
        return CHIP_NO_ERROR;
    }

Why the deep copy? The `ByteSpan` passed to `ProcessBlock()` is a view into a
`System::PacketBuffer` owned by the CHIP messaging layer. That buffer may be
reused or freed as soon as `ProcessBlock()` returns. By copying to heap before
scheduling, we guarantee the data is safe when `HandleProcessBlock()` runs later
on the platform task.

The size check (`mBlock.size() < block.size()`) avoids repeated malloc/free if
all blocks happen to be the same size (which is the common case for BDX with a
fixed `MaxBlockSize`).

### 10k. Optional: Encrypted OTA (CONFIG_ENABLE_ENCRYPTED_OTA)

Uses the `espressif/esp_encrypted_img` component:

-   `InitEncryptedOTA(key)` — app calls this once at startup with the RSA-3072
    PEM private key (OTAImageProcessorImpl.cpp:631).
-   `DecryptStart()` (called in HandlePrepareDownload) — creates an
    `esp_decrypt_handle_t` using `esp_encrypted_img_decrypt_start()`.
-   `DecryptBlock(block, decryptedBlock)` (called in HandleProcessBlock before
    `esp_ota_write`) — calls `esp_encrypted_img_decrypt_data()`, which decrypts
    one chunk and returns a newly heap-allocated plain-text buffer. The caller
    must free this buffer after writing.
-   `DecryptEnd()` (called in HandleFinalize) — finalises and verifies the
    decryption.
-   `DecryptAbort()` (called in HandleAbort) — releases the decrypt context.

The encrypted OTA image format is: RSA-encrypted AES key prepended, followed by
AES-CBC encrypted firmware payload. The Provider supplies this pre-encrypted
image; the Requestor decrypts on-the-fly using the private key stored in the
device.

### 10l. Optional: Delta OTA (CONFIG_ENABLE_DELTA_OTA)

Uses the `espressif/esp_delta_ota` component (bsdiff + HEATSHRINK decompressor):

PrepareDownload changes:

-   `esp_ota_begin` with `OTA_SIZE_UNKNOWN` (no sequential-write optimization).
-   `esp_delta_ota_init(&deltaOtaCfg)` → returns a `mDeltaOTAUpdateHandle`. The
    `deltaOtaCfg` has two callbacks: `read_cb` = `DeltaOTAReadCallback` — reads
    bytes from the running partition. `write_cb` = `DeltaOTAWriteCallback` —
    writes reconstructed bytes to the new partition via `esp_ota_write()`.

ProcessBlock changes (OTAImageProcessorImpl.cpp:499):

-   Before feeding to delta engine: `VerifyHeaderData()` checks the 64-byte
    patch header magic (`0xfccdde10`) and verifies the SHA-256 digest of the
    running firmware matches the "base" recorded in the patch. If it doesn't
    match, the patch was generated against a different base firmware — abort.
-   `esp_delta_ota_feed_patch(handle, data, size)` — feeds the compressed diff
    bytes; the engine reconstructs the full image internally, writing via
    `DeltaOTAWriteCallback → esp_ota_write()`.

Finalize changes:

-   `esp_delta_ota_finalize(handle)` — signals the delta engine that the patch
    is complete and finalises the output image.
-   `esp_delta_ota_deinit(handle)` — frees the engine.
-   Then `esp_ota_end()` as normal to validate the reconstructed image.

DeltaOTAReadCallback (OTAImageProcessorImpl.cpp:213): Reads bytes from the
running partition (the old firmware) at any `srcOffset`. The bsdiff engine calls
this to get the "old" data it needs to reconstruct the "new" image from the
diff.

DeltaOTAWriteCallback (OTAImageProcessorImpl.cpp:236): Receives reconstructed
new-image bytes and writes them to the passive partition via `esp_ota_write()`.
Also performs `chip_id` verification (matches `CONFIG_IDF_FIRMWARE_CHIP_ID`) on
the first `esp_image_header_t`.

### 10m. Optional: RCP co-processor OTA (CONFIG_OPENTHREAD_BORDER_ROUTER)

The `OTARcpProcessorDelegate` interface (OTAImageProcessorImpl.h:52) is used
when the ESP32 host runs OpenThread as a Border Router with an ESP32-H2 RCP
connected via UART. The OTA image in that case is a multi-binary concatenation:
RCP firmware first, then host firmware.

    class OTARcpProcessorDelegate
    {
        virtual esp_err_t OnOtaRcpPrepareDownload() = 0;
        virtual esp_err_t OnOtaRcpProcessBlock(
            const uint8_t * buffer, size_t bufLen,
            size_t & rcpOtaReceivedLen) = 0;  // returns how many bytes were RCP data
        virtual esp_err_t OnOtaRcpFinalize() = 0;
        virtual esp_err_t OnOtaRcpAbort() = 0;
    };

The concrete implementation is `OTARcpProcessorImpl` in OTAHelper.cpp (line 59).
It uses the `esp_rcp_ota` library:

    OnOtaRcpPrepareDownload   → esp_rcp_ota_begin(&mRcpOtaHandle)
    OnOtaRcpProcessBlock      → esp_rcp_ota_receive(handle, buf, len, &received)
                                 continues until state == ESP_RCP_OTA_STATE_FINISHED
                                 once RCP firmware is fully received, rcpOtaReceivedLen
                                 tells HandleProcessBlock how many bytes to skip for
                                 esp_ota_write (the rest is host firmware)
    OnOtaRcpFinalize          → esp_rcp_ota_end(handle)
    OnOtaRcpAbort             → esp_rcp_ota_abort(handle)

In HandleProcessBlock with RCP delegate (OTAImageProcessorImpl.cpp:515):

    size_t rcpOtaReceivedLen = 0;
    err = mOtaRcpDelegate->OnOtaRcpProcessBlock(data, size, rcpOtaReceivedLen);
    if (err == ESP_OK && size > rcpOtaReceivedLen)
    {
        // Bytes after rcpOtaReceivedLen are host firmware; write to flash.
        err = esp_ota_write(mOTAUpdateHandle,
                            data + rcpOtaReceivedLen,
                            size - rcpOtaReceivedLen);
    }

The RCP firmware is sent over UART to the co-processor by `esp_rcp_ota_end()` or
during receive (implementation-dependent on the `esp_rcp_ota` library version).

---

11. Attribute Bookkeeping

---

File: src/app/clusters/ota-requestor/OTARequestorAttributes.h

OTARequestorAttributes (OTARequestorAttributes.h:39) acts as an in-memory cache
of the OTA Requestor cluster's attributes. It keeps the actual values and
notifies the cluster server when something changes (so it can mark itself dirty
and trigger reads).

Attributes managed: OTAUpdateStateEnum mUpdateState // kIdle / kQuerying /
kDownloading / ... Nullable<uint8_t> mUpdateStateProgress // 0-100%, null if not
downloading bool mUpdatePossible // can the device be updated right now
ProviderLocationList mProviders // DefaultOTAProviders attribute

SetUpdateState (OTARequestorAttributes.h:55): Marks the UpdateState attribute
dirty (so the cluster server reports it), AND generates a StateTransition event
via the events generator.

SetUpdateStateProgress: Marks UpdateStateProgress dirty whenever the download
percentage changes.

SetStorageAndLoadAttributes: Called once during Init() to pull the persisted
DefaultOTAProviders list and UpdateState back from KVS into the in-memory cache.

AttributeChangeListener interface (OTARequestorAttributes.h:46):
OTARequestorCluster implements this. When an attribute changes,
OTARequestorCluster calls NotifyAttributeChanged() on itself which wakes up the
Matter data-model layer to re-read and re-encode the attribute for any pending
subscriptions.

---

12. End-to-End Flow (annotated)

---

Below is the complete sequence of calls from device boot through successful
update. The column on the right shows the source file:function.

Step 0 — Boot and Init

```
    App creates objects and calls Init:

        DefaultOTARequestorStorage storage;
        storage.Init(persistentStorage);

        DefaultOTARequestorDriver driver;
        BDXDownloader downloader;
        OTARequestorAttributes attributes;
        DefaultOTARequestorEventGenerator eventGen;
        DefaultOTARequestor requestor;

        requestor.Init(server, storage, driver, downloader, attributes, eventGen);
            // DefaultOTARequestor.cpp:117

        driver.Init(&requestor, &imageProcessor);
            // DefaultOTARequestorDriver.cpp:67
            // → IsFirstImageRun() → if true: ConfirmCurrentImage + NotifyUpdateApplied
            // → else: StartPeriodicQueryTimer (24 h default)

        SetRequestorInstance(&requestor);
            // DefaultOTARequestor.cpp:99

Step 1 — Periodic Timer Fires (or AnnounceOTAProvider received)
```

    PeriodicQueryTimerHandler fires:
        DefaultOTARequestorDriver.cpp:372
        → GetNextProviderLocation(providerLocation, listExhausted)
        → requestor.SetCurrentProviderLocation(providerLocation)
        → driver.SendQueryImage()

    SendQueryImage (DefaultOTARequestorDriver.cpp:340):
        → SystemLayer().ScheduleLambda([this] {
              mRequestor->TriggerImmediateQueryInternal(); });

    TriggerImmediateQueryInternal (DefaultOTARequestor.cpp:512):
        → RecordNewUpdateState(kQuerying, kSuccess)
        → ConnectToProvider(kQueryImage)

Step 2 — CASE Session Establishment

```
    ConnectToProvider (DefaultOTARequestor.cpp:352):
        → CASESessionManager::FindOrEstablishSession(providerNodeId, ...)
        → (async) CHIP messaging layer runs CASE handshake over the fabric

    OnConnected callback fires (DefaultOTARequestor.cpp:432):
        case kQueryImage:
            → SendQueryImageRequest(exchangeMgr, session)   // line 725

Step 3 — QueryImage / QueryImageResponse
```

    QueryImage command is encoded and sent over the CASE session.
    Provider receives it, checks version, returns QueryImageResponse.

    Provider side: OTAProviderDelegate::HandleQueryImage() is called.
    Provider fills: imageURI = "bdx://<nodeId>/<fileDesignator>",
                    softwareVersion, updateToken, status = kUpdateAvailable.

    Back on Requestor:
    OnQueryImageResponse (DefaultOTARequestor.cpp:137):
        case kUpdateAvailable:
            ExtractUpdateDescription() → parses BDX URI, extracts nodeId + fileDesig.
            Checks: update.softwareVersion > mCurrentVersion.
            Stores mTargetVersion, mUpdateToken, mFileDesignator.
            → mOtaRequestorDriver->UpdateAvailable(update, delay)

Step 4 — Driver Decides to Download

```
    UpdateAvailable (DefaultOTARequestorDriver.cpp:204):
        → ScheduleDelayedAction(delay, DownloadUpdateTimerHandler, this)

    DownloadUpdateTimerHandler (DefaultOTARequestorDriver.cpp:176):
        → mRequestor->DownloadUpdate()

    DownloadUpdate (DefaultOTARequestor.cpp:562):
        → RecordNewUpdateState(kDownloading, kSuccess)
        → ConnectToProvider(kDownload)

Step 5 — CASE Session (may reuse) + BDX Setup
```

    OnConnected fires:
        case kDownload:
            → StartDownload(exchangeMgr, session)   // line 797

    StartDownload:
        TransferInitData initOptions {
            kReceiverDrive,
            MaxBlockSize = driver->GetMaxDownloadBlockSize(),
            FileDesignator = mFileDesignator
        };
        exchangeCtx = exchangeMgr.NewContext(session, &mBdxMessenger);
        mBdxMessenger.Init(mBdxDownloader, exchangeCtx);
        mBdxDownloader->SetBDXParams(initOptions, 5 min);
        mBdxDownloader->BeginPrepareDownload();
            → mImageProcessor->PrepareDownload()   // platform opens OTA partition

Step 6 — Platform PrepareDownload

```
    Platform's OTAImageProcessorImpl::PrepareDownload():
        esp_ota_begin(partition, size, &handle);  // or equivalent
        // When ready, notify the downloader:
        chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t ctx) {
            auto * dl = reinterpret_cast<OTADownloader *>(ctx);
            dl->OnPreparedForDownload(CHIP_NO_ERROR);
        }, reinterpret_cast<intptr_t>(OTADownloader::GetBDXDownloader()));

    OnPreparedForDownload (BDXDownloader.cpp:131):
        SetState(kInProgress)
        PollTransferSession()
            → mBdxTransfer emits ReceiveInit message
            → mMsgDelegate->SendMessage(ReceiveInit)   // to Provider

Step 7 — BDX Transfer Loop
~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    Provider receives ReceiveInit, responds with SendAccept.
    (Provider is the BDX Sender + Responder; Requestor is Receiver + Initiator.)

    BDXMessenger::OnMessageReceived (SendAccept):
        → mDownloader->OnMessageReceived(payloadHeader, payload)
        → mBdxTransfer.HandleMessageReceived(payloadHeader, payload, ...)
        → PollTransferSession()
            HandleBdxEvent(kAcceptReceived):
                → mBdxTransfer.PrepareBlockQuery()  // emit BlockQuery to Provider

    For each Block received:
        HandleBdxEvent(kBlockReceived):
            → mImageProcessor->ProcessBlock(blockData)
                // Platform: parse OTA header (first call), then write to flash.
                // Update mParams.downloadedBytes.
            → mStateDelegate->OnUpdateProgressChanged(mImageProcessor->GetPercentComplete())
                // Core: SetUpdateStateProgress attribute (cluster reports %)
            // If NOT IsEof: PollTransferSession → emit BlockQuery for next block.

    On last block (IsEof = true):
        HandleBdxEvent(kBlockReceived):
            → mBdxTransfer.PrepareBlockAck()  // emit BlockAckEOF
            → mImageProcessor->Finalize()
                // Platform: flush buffer, verify SHA-256 digest.

    When kMsgToSend(BlockAckEOF) is processed:
        SetState(kComplete, kSuccess)
        → mStateDelegate->OnDownloadStateChanged(kComplete, kSuccess)

Step 8 — Download Complete → ApplyUpdate
```

    OnDownloadStateChanged (DefaultOTARequestor.cpp:614):
        case kComplete:
            → mOtaRequestorDriver->UpdateDownloaded()

    UpdateDownloaded (DefaultOTARequestorDriver.cpp:239):
        → mRequestor->ApplyUpdate()

    ApplyUpdate (DefaultOTARequestor.cpp:573):
        → RecordNewUpdateState(kApplying, kSuccess)
        → StoreCurrentUpdateInfo()   // persist in case of crash before reboot
        → ConnectToProvider(kApplyUpdate)

Step 9 — ApplyUpdateRequest / ApplyUpdateResponse

```
    OnConnected fires:
        case kApplyUpdate:
            → SendApplyUpdateRequest(exchangeMgr, session)   // line 830

    ApplyUpdateRequest::Type args {
        updateToken = mUpdateToken,
        newVersion  = mTargetVersion
    };
    cluster.InvokeCommand(args, this, OnApplyUpdateResponse, OnApplyUpdateFailure);

    Provider receives ApplyUpdateRequest; returns ApplyUpdateResponse.
    Provider's action choices:
        kProceed          → go ahead and apply now
        kAwaitNextAction  → wait delayedActionTime then ask again
        kDiscontinue      → abort, do not apply this image

    OnApplyUpdateResponse (DefaultOTARequestor.cpp:266):
        case kProceed:
            → mOtaRequestorDriver->UpdateConfirmed(delay)
        case kAwaitNextAction:
            → mOtaRequestorDriver->UpdateSuspended(delay)
            → RecordNewUpdateState(kDelayedOnApply, kDelayByProvider)
        case kDiscontinue:
            → mOtaRequestorDriver->UpdateDiscontinued()
            → RecordNewUpdateState(kIdle, kSuccess)

    UpdateConfirmed (DefaultOTARequestorDriver.cpp:245):
        → ScheduleDelayedAction(delay, ApplyTimerHandler, this)

    ApplyTimerHandler (DefaultOTARequestorDriver.cpp:192):
        → mImageProcessor->Apply()
            // Platform: esp_ota_set_boot_partition() + esp_restart()

Step 10 — First Boot of New Image
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    Device reboots into new firmware.

    driver.Init():
        → mImageProcessor->IsFirstImageRun()  // true: new image booted
        → mImageProcessor->ConfirmCurrentImage()
            // Platform: esp_ota_mark_app_valid_cancel_rollback()
        → mRequestor->NotifyUpdateApplied()

Step 11 — NotifyUpdateApplied
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    NotifyUpdateApplied (DefaultOTARequestor.cpp:583):
        → Generate VersionApplied event.
        → ConnectToProvider(kNotifyUpdateApplied)

    OnConnected fires:
        case kNotifyUpdateApplied:
            → SendNotifyUpdateAppliedRequest(exchangeMgr, session)   // line 845

    NotifyUpdateApplied::Type args {
        updateToken = mUpdateToken,
        softwareVersion = mCurrentVersion   // the new running version
    };
    cluster.InvokeCommand(args, this, OnNotifyUpdateAppliedResponse, ...);

    Before sending the command, Reset() is called to clear all state (line 859):
        mProviderLocation.ClearValue();
        mUpdateToken.reduce_size(0);
        RecordNewUpdateState(kIdle, kSuccess);
        mTargetVersion = 0;
        StoreCurrentUpdateInfo();   // persist cleared state

    Provider receives NotifyUpdateApplied — no response required.
    OTA update is complete.  Periodic timer resumes.

---

13. State Machine Diagram
--------------------------

    ┌──────┐ TriggerQuery    ┌──────────┐ kUpdateAvailable  ┌─────────────┐
    │ Idle │────────────────►│ Querying │──────────────────►│ Downloading │
    └──────┘                 └──────────┘                   └─────────────┘
       ▲  ▲                      │ kBusy/kNotAvail                │
       │  │              kDelay  ▼                                │ BDX kComplete
       │  │        ┌──────────────────┐                           ▼
       │  │        │ DelayedOnQuery   │                     ┌──────────┐
       │  │        └──────────────────┘                     │ Applying │
       │  │                                                  └──────────┘
       │  │  kDiscontinue/Error                                   │ kProceed
       │  └─────────────────────────────────────────────────      │
       │                                                    ▼      ▼
       │                                            ┌──────────────────┐
       │  NotifyUpdateApplied sent                  │  (apply + reboot) │
       └────────────────────────────────────────────└──────────────────┘
                                                           ▲ IsFirstImageRun
                                                           │ Confirm + Notify

    Other states:
      DelayedOnUserConsent — DownloadUpdateDelayedOnUserConsent() called
      RollingBack          — set when rollback is detected on boot

    State transitions are recorded via RecordNewUpdateState() which:
      1. Clears/sets UpdateStateProgress attribute.
      2. Updates the cluster attribute (fires subscriptions).
      3. Emits a StateTransition event.
      4. Calls driver->HandleIdleStateEnter/Exit for timer management.

---

14. Timers and Watchdogs
-------------------------

DefaultOTARequestorDriver runs at most one timer at a time:

    EITHER periodic query timer (StartPeriodicQueryTimer)
    OR     watchdog timer       (StartWatchdogTimer)

Timer switch (DefaultOTARequestorDriver.cpp:438):

    StartSelectedTimer(SelectedTimer::kPeriodicQueryTimer):
        StopWatchdogTimer()
        StartPeriodicQueryTimer()    // 24 h default (SetPeriodicQueryTimeout)

    StartSelectedTimer(SelectedTimer::kWatchdogTimer):
        StopPeriodicQueryTimer()
        StartWatchdogTimer()         // 6 h default  (SetWatchdogTimeout)

The switch happens:
  - Idle → non-Idle: HandleIdleStateExit() starts watchdog.
  - non-Idle → Idle: HandleIdleStateEnter() starts periodic query timer.

BDXDownloader also has its own timeout (5 min, kDownloadTimeoutSec):
  - A separate System::Layer timer fires TransferTimeoutCheckHandler.
  - Compares the BDX block counter between consecutive checks.
  - If no new blocks: fires OnDownloadTimeout() → Abort + SetState(kIdle, kTimeOut).

---

15. Error / Recovery Paths
---------------------------

| Event                        | Handler                            | Recovery               |
|------------------------------|------------------------------------|------------------------|
| QueryImage CASE fails        | OnConnectionFailure → RecordError  | Periodic timer retries |
| QueryImage timeout           | OnQueryImageFailure → DisconnectFromProvider → RecordError | Retry |
| Provider returns kBusy       | UpdateNotFound(kBusy)              | Retry ≤3×, then next provider |
| Provider returns kNotAvail   | UpdateNotFound(kNotAvail)          | Next provider          |
| BDX block timeout (5 min)    | OnDownloadTimeout → Abort          | Periodic timer retries |
| BDX StatusReport error       | CleanupOnError(kFailure)           | Periodic timer retries |
| Finalize() digest mismatch   | ProcessBlock returns error → BDXDownloader EndDownload → Abort | Retry |
| Apply() fails                | Driver CancelImageUpdate           | Periodic timer retries |
| Reboot during download       | LoadCurrentUpdateInfo on next boot → state≠kIdle → Reset() → periodic timer |
| Reboot after apply (new img) | IsFirstImageRun → Confirm + Notify |                        |
| New image crashes (rollback) | IsFirstImageRun = false, state may be kApplying → Reset() |  |
| Invalid CASE session         | OnQueryImageFailure(TIMEOUT) → HandleIdleStateEnter(kInvalidSession) → retry ≤1× |

RecordErrorUpdateState (DefaultOTARequestor.cpp:686):
  1. Generates a DownloadError event (with bytes downloaded, % progress).
  2. Calls RecordNewUpdateState(kIdle, kFailure).
  3. → HandleIdleStateEnter() → periodic timer resumes.

---

16. How to Wire Everything Up — ESP32 example app (OTAHelper + main.cpp)
--------------------------------------------------------------------------

Files:
  examples/platform/esp32/ota/OTAHelper.h
  examples/platform/esp32/ota/OTAHelper.cpp
  examples/ota-requestor-app/esp32/main/main.cpp

The ESP32 OTA example app keeps all OTA objects as file-scope static singletons in
OTAHelper.cpp (no heap allocation for the top-level objects themselves):

    // OTAHelper.cpp:49–57
    static DefaultOTARequestor          gRequestorCore;
    static DefaultOTARequestorStorage   gRequestorStorage;
    static CustomOTARequestorDriver     gRequestorUser;  // extends ExtendedOTARequestorDriver
    static BDXDownloader                gDownloader;
    static OTAImageProcessorImpl        gImageProcessor;

`CustomOTARequestorDriver` is a thin subclass of `ExtendedOTARequestorDriver` that
overrides `CanConsent()` to respect a runtime flag set via shell command:

    class CustomOTARequestorDriver : public DeviceLayer::ExtendedOTARequestorDriver
    {                                               // OTAHelper.cpp:43
        bool CanConsent() override {
            return gRequestorCanConsent.ValueOr(
                       ExtendedOTARequestorDriver::CanConsent());
        }
    };

`ExtendedOTARequestorDriver` is a slightly richer driver than `DefaultOTARequestorDriver`
(found in `DefaultOTARequestorDriver.h`) — it also has a `UserConsentDelegate` hook
and slightly different retry policy.

### InitOTARequestor() — the single entry point

Called once from app_main() after the Matter server is started:

    void OTAHelpers::InitOTARequestor()     // OTAHelper.cpp:144
    {
        if (GetRequestorInstance()) return; // already initialised

        // Step 1: register the requestor singleton globally.
        SetRequestorInstance(&gRequestorCore);

        // Step 2: bind storage to the server's persistent KVS.
        gRequestorStorage.Init(Server::GetInstance().GetPersistentStorage());

        // Step 3: initialise the core requestor.
        //   GetOTARequestorAttributes() and GetDefaultOTARequestorEventGenerator()
        //   return references to objects owned by the cluster server object that
        //   CodegenIntegration created automatically from ZAP.
        gRequestorCore.Init(Server::GetInstance(),
                            gRequestorStorage,
                            gRequestorUser,
                            gDownloader,
                            GetOTARequestorAttributes(),
                            GetDefaultOTARequestorEventGenerator());

        // Step 4: give the image processor a back-pointer to the downloader.
        //   The image processor calls mDownloader->OnPreparedForDownload() and
        //   mDownloader->FetchNextData().
        gImageProcessor.SetOTADownloader(&gDownloader);

        // Step 5 [optional]: if Border Router, plug in the RCP delegate.
        #if defined(CONFIG_AUTO_UPDATE_RCP) && defined(CONFIG_OPENTHREAD_BORDER_ROUTER)
        gImageProcessor.SetOtaRcpDelegate(&gOtaRcpDelegate);
        #endif

        // Step 6: give the downloader a pointer to the image processor.
        //   The downloader calls ProcessBlock / Finalize / Abort.
        gDownloader.SetImageProcessorDelegate(&gImageProcessor);

        // Step 7: initialise the driver.
        //   The driver checks IsFirstImageRun() and either confirms + notifies,
        //   resets bad state, or starts the periodic query timer.
        gRequestorUser.Init(&gRequestorCore, &gImageProcessor);

        // Step 8 [optional]: encrypted OTA — pass the RSA private key.
        #if CONFIG_ENABLE_ENCRYPTED_OTA
        gImageProcessor.InitEncryptedOTA(sOTADecryptionKey);
        #endif

        // Step 9 [optional]: user consent delegate.
        if (gUserConsentState != kUnknown) {
            gUserConsentProvider.SetUserConsentState(gUserConsentState);
            gRequestorUser.SetUserConsentDelegate(&gUserConsentProvider);
        }
    }

### Object relationship after InitOTARequestor()

    gRequestorCore (DefaultOTARequestor)
        .mStorage          → gRequestorStorage
        .mOtaRequestorDriver → gRequestorUser
        .mBdxDownloader    → gDownloader
        .mAttributes       → (owned by OTARequestorCluster, via CodegenIntegration)
        .mEventGenerator   → (owned by OTARequestorCluster, via CodegenIntegration)

    gDownloader (BDXDownloader)
        .mImageProcessor   → gImageProcessor

    gImageProcessor (OTAImageProcessorImpl)
        .mDownloader       → gDownloader

    gRequestorUser (CustomOTARequestorDriver)
        .mRequestor        → gRequestorCore
        .mImageProcessor   → gImageProcessor

This forms a closed loop:
  core → driver → core (for action triggers)
  core → downloader → imageProcessor → downloader (for back-pressure)

### main.cpp — app startup sequence

    extern "C" void app_main()              // main.cpp:121
    {
        nvs_flash_init();
        esp_event_loop_create_default();

        // [optional] Shell + OTA shell commands
        chip::LaunchShell();
        OTARequestorCommands::GetInstance().Register();

        // WiFi stack
        ESP32Utils::InitWiFiStack();

        // CHIP device manager (runs the Matter event loop)
        CHIPDeviceManager::GetInstance().Init(&EchoCallbacks);

        // Factory data / DAC provider setup ...

        // Schedule server init (ZAP data model + app server) on the Matter task.
        PlatformMgr().ScheduleWork(InitServer, nullptr);
            // InitServer() calls:
            //   Esp32AppServer::Init()  ← starts Matter server, which calls
            //   InitOTARequestor()      ← via a post-init hook in Esp32AppServer

        // Register for platform OTA state-change events.
        PlatformMgr().AddEventHandler(OTAEventsHandler, nullptr);
    }

### OTAEventsHandler — app-level OTA notifications

    void OTAEventsHandler(const DeviceLayer::ChipDeviceEvent * event, intptr_t)
    {                                           // main.cpp:86
        if (event->Type == DeviceEventType::kOtaStateChanged)
        {
            switch (event->OtaStateChanged.newState)
            {
            case kOtaDownloadInProgress: // posted by HandlePrepareDownload
            case kOtaDownloadComplete:   // posted by HandleFinalize on success
            case kOtaDownloadFailed:     // posted by HandleFinalize on esp_ota_end fail
            case kOtaDownloadAborted:    // posted by HandleAbort
            case kOtaApplyInProgress:    // posted at start of HandleApply
            case kOtaApplyComplete:      // posted after esp_ota_set_boot_partition
            case kOtaApplyFailed:        // posted if esp_ota_set_boot_partition fails
            }
        }
    }

These events are posted by `PostOTAStateChangeEvent()` inside `OTAImageProcessorImpl`
and give the application visibility into the download and apply lifecycle.  The app
can use them to e.g. blink an LED, suppress other activities, or log to external
monitoring.

### Shell commands registered by OTARequestorCommands::Register()

    OTARequestor userConsentState <granted|denied|deferred>
        Sets gUserConsentState so next query includes/excludes consent.

    OTARequestor requestorCanConsent <true|false>
        Overrides CanConsent() return value.

    OTARequestor PeriodicQueryTimeout <seconds>
        Changes the periodic provider query interval at runtime and rekicks timer.

---

17. File & Symbol Quick Reference
-----------------------------------

    File                                        | Key symbols
    --------------------------------------------|------------------------------------------
    src/lib/core/OTAImageHeader.h               | kOTAImageFileIdentifier (0x1BEEF11E),
                                                | OTAImageHeader struct,
                                                | OTAImageHeaderParser::AccumulateAndDecode()
    src/include/platform/OTAImageProcessor.h    | OTAImageProcessorInterface, OTAImageProgress,
                                                | PrepareDownload, ProcessBlock, Finalize,
                                                | Apply, Abort, IsFirstImageRun,
                                                | ConfirmCurrentImage, GetPercentComplete
    src/platform/ESP32/OTAImageProcessorImpl.h  | OTAImageProcessorImpl,
                                                | OTARcpProcessorDelegate,
                                                | SetOTADownloader, SetOtaRcpDelegate,
                                                | InitEncryptedOTA
    src/platform/ESP32/OTAImageProcessorImpl.cpp| HandlePrepareDownload,
                                                | HandleProcessBlock, HandleFinalize,
                                                | HandleApply, HandleAbort,
                                                | ProcessHeader, SetBlock, ReleaseBlock,
                                                | IsFirstImageRun, ConfirmCurrentImage,
                                                | PostOTAStateChangeEvent,
                                                | DeltaOTAReadCallback, DeltaOTAWriteCallback,
                                                | VerifyPatchHeader, VerifyChipId,
                                                | DecryptBlock, DecryptStart, DecryptEnd
    src/app/clusters/ota-requestor/             |
      OTARequestorInterface.h                   | OTARequestorInterface, ProviderLocationList,
                                                | SetRequestorInstance, GetRequestorInstance
      DefaultOTARequestor.h/.cpp                | DefaultOTARequestor, BDXMessenger,
                                                | ConnectToProvider, StartDownload,
                                                | OnQueryImageResponse, OnApplyUpdateResponse,
                                                | StoreCurrentUpdateInfo, LoadCurrentUpdateInfo
      OTARequestorDriver.h                      | OTARequestorDriver, UpdateDescription,
                                                | UpdateNotFoundReason, IdleStateReason
      DefaultOTARequestorDriver.h/.cpp          | DefaultOTARequestorDriver, Init,
                                                | PeriodicQueryTimerHandler, WatchdogTimerHandler,
                                                | GetNextProviderLocation, ScheduleQueryRetry
      OTADownloader.h                           | OTADownloader, State enum,
                                                | BeginPrepareDownload, OnPreparedForDownload,
                                                | FetchNextData, SkipData, EndDownload
      BDXDownloader.h/.cpp                      | BDXDownloader, SetBDXParams,
                                                | PollTransferSession, HandleBdxEvent,
                                                | TransferTimeoutCheckHandler
      OTARequestorStorage.h                     | OTARequestorStorage, Store/Load/Clear*
      DefaultOTARequestorStorage.h/.cpp         | DefaultOTARequestorStorage, Init
      OTARequestorAttributes.h                  | OTARequestorAttributes, SetUpdateState,
                                                | SetUpdateStateProgress, AttributeChangeListener
      OTARequestorCluster.h/.cpp                | OTARequestorCluster,
                                                | GenerateVersionAppliedEvent,
                                                | GenerateDownloadErrorEvent
      CodegenIntegration.h/.cpp                 | gInternalOnSetRequestorInstance (hook)
    src/app/clusters/ota-provider/              |
      OTAProviderCluster.h/.cpp                 | OtaProviderServer, OtaProviderLogic
      ota-provider-delegate.h                   | OTAProviderDelegate, HandleQueryImage,
                                                | HandleApplyUpdateRequest,
                                                | HandleNotifyUpdateApplied
    src/protocols/bdx/                          |
      BdxTransferSession.h/.cpp                 | TransferSession, StartTransfer,
                                                | HandleMessageReceived, PollOutput,
                                                | PrepareBlockQuery, PrepareBlockAck,
                                                | AbortTransfer, OutputEvent, OutputEventType
      BdxMessages.h/.cpp                        | MessageType enum, ReceiveInit, SendAccept,
                                                | Block, BlockEOF, BlockQuery, BlockAck,
                                                | BlockAckEOF, BlockQueryWithSkip
      BdxUri.h/.cpp                             | ParseURI() — "bdx://<nodeId>/<path>"
    examples/platform/esp32/ota/               |
      OTAHelper.h/.cpp                          | OTAHelpers::InitOTARequestor(),
                                                | gRequestorCore, gDownloader, gImageProcessor,
                                                | gRequestorUser, gRequestorStorage,
                                                | CustomOTARequestorDriver, OTARcpProcessorImpl,
                                                | OTARequestorCommands::Register()
    examples/ota-requestor-app/esp32/main/      |
      main.cpp                                  | app_main(), InitServer(),
                                                | OTAEventsHandler() — kOtaDownloadInProgress,
                                                | kOtaDownloadComplete, kOtaApplyInProgress,
                                                | kOtaApplyComplete, etc.
```
