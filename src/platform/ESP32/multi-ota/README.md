# ESP32 Multi-Image OTA — Design

> Status: design draft. No code in this directory yet.
>
> Target platform: ESP32 family (ESP32, ESP32-S3, ESP32-C3, ESP32-C6, ESP32-H2, …).
> Underlying APIs: ESP-IDF `esp_ota_*`, `esp_partition_*`, `esp_app_format`.

This document describes a Matter multi-image OTA implementation for ESP32 in the
style of the Silabs and NXP TLV-processor pattern. It is the planning document
for the code that will live in this directory; the layout, class names, and
flow described here are the contract.

---

## 1. Goals & Non-Goals

### Goals

- Allow a single Matter OTA transfer to deliver **multiple binaries** to a
  device — for example, application firmware *and* a UART co-processor's
  firmware, in one image.
- Reuse the existing Matter OTA Requestor unchanged. The OTA spec on the wire
  does not change.
- Keep the per-component logic (where bytes go, how to commit, how to roll
  back) in small, isolated classes that manufacturers can add without forking
  the framework.
- Be opt-in. The current single-image `OTAImageProcessorImpl` continues to
  build and work for products that don't need this.

### Non-Goals

- Replace `esp_ota_*`. The application firmware processor is a thin wrapper
  around it.
- Define a new OTA cluster or change the Matter OTA Provider/Requestor
  protocol. Multiplexing happens *inside* the BDX payload.
- Provide a generic component registry that survives reboots. Component
  registration is static (compiled in).
- Cross-platform abstraction. This module targets ESP32 only. Other platforms
  have their own implementations.

---

## 2. Use Cases

| # | Scenario | Components in one OTA bundle |
|---|---|---|
| 1 | App-only update (single image) | App |
| 2 | App + bootloader update | Bootloader, App |
| 3 | App + partition table refresh | Partition Table, App |
| 4 | App + factory-data refresh (DAC/PAI/CD rotation) | App, Factory Data |
| 5 | App + UART/SPI/I²C co-processor firmware | App, Coproc-FW |
| 6 | App + multiple co-processors (e.g., audio MCU + sensor MCU) | App, Coproc-A, Coproc-B |
| 7 | Coproc-only update (no app change) | Coproc-FW |

Use cases (5)–(7) are why this module exists. Use cases (1)–(4) work today
with the standard single-image processor; with this module they continue to
work but go through the same dispatcher.

---

## 3. OTA File Format

The Matter OTA file header (vendor id, product id, software version, payload
size, …) is unchanged. The **payload** is now a sequence of TLV sections.

### 3.1 Outer layout

```
+---------------------------------------------------------+
| Matter OTA header (chip-ota-image-tool format)          |
+---------------------------------------------------------+
| TLV section 1   [ tag(4) | length(4) | value(length) ]  |
| TLV section 2   [ tag(4) | length(4) | value(length) ]  |
| ...                                                     |
| TLV section N   [ tag(4) | length(4) | value(length) ]  |
+---------------------------------------------------------+
```

- All multi-byte fields are **little-endian** (matching ESP-IDF and matching
  Silabs/NXP).
- Sections are concatenated with no padding.
- The order is the order of installation. See §7 (Atomicity).

### 3.2 Per-section value layout

Inside each TLV's `value` is a fixed 132-byte component descriptor followed by
the actual component bytes:

```
+------------------------------------------------------------+
| Descriptor (132 bytes)                                     |
|   uint32_t version                                         |
|   char     versionString[64]                               |
|   char     buildDate[64]                                   |
+------------------------------------------------------------+
| Component bytes (length - 132)                             |
+------------------------------------------------------------+
```

The descriptor is logged at runtime and can be used for sanity checks (e.g.
"the coproc firmware claims version X — does that match the manifest?").

### 3.3 Tag space

The tag is a **`uint32_t`** on the wire (4 bytes inside the TLV header) — so
the namespace is 4 billion values. There is no "max number of components."
The dispatcher stores processors in a `std::map<uint32_t, OTATlvProcessor*>`,
so any number of components can be registered.

To keep the namespace organized, we partition it:

| Range | Owner | Use |
|---|---|---|
| `0` | — | Invalid. Rejected so we can catch malformed images. |
| `1 .. 15` | Framework / spec | **Well-known tags** for components defined by this module. |
| `16 .. 65535` | Reserved | Held for future framework/spec growth. |
| `≥ 65536` (`0x10000`) | Manufacturer | **Manufacturer-defined.** Pick any value; no cap. |

Well-known tags are exposed as named constants for readability:

```cpp
namespace WellKnownTag {
    inline constexpr uint32_t kApplication    = 1;  // ESP-IDF app slot.
    inline constexpr uint32_t kBootloader     = 2;  // 2nd-stage bootloader.
    inline constexpr uint32_t kPartitionTable = 3;  // partitions.bin.
    inline constexpr uint32_t kFactoryData    = 4;  // Credential rotation.
    // 5..15 reserved for future framework use.
}
```

Manufacturer-defined tags are just integers ≥ 65536 — there is no need to
add them to a central enum. A product with five coprocs can do:

```cpp
constexpr uint32_t kAudioMcuTag       = 0x00010001;
constexpr uint32_t kSensorHubTag      = 0x00010002;
constexpr uint32_t kDisplayDriverTag  = 0x00010003;
constexpr uint32_t kZigbeeRadioTag    = 0x00010004;
constexpr uint32_t kBleAudioTag       = 0x00010005;
```

The only runtime validation is **duplicate registration** —
`RegisterProcessor(tag, ...)` fails if `tag` is already in the map. Range
validation is intentionally absent so manufacturers aren't bottlenecked
behind framework changes.

The tag-number-to-meaning mapping is a stable contract between the device
firmware and the build-time packaging tool. Bumping or repurposing a tag —
including a manufacturer-defined one — is an ABI break for that product.

### 3.4 Packaging tool

A Python tool (to live at `scripts/tools/esp32/ota/multi_ota_image_tool.py`)
takes a JSON manifest like:

```json
{
  "ota_version": "0.0.2",
  "components": [
    { "tag": 1, "version": 2,  "version_string": "app-2.0",     "path": "build/app.bin" },
    { "tag": 8, "version": 17, "version_string": "coproc-1.17", "path": "coproc.bin"   }
  ]
}
```

…and produces a single `.ota` file consumable by the standard Matter OTA
Provider (`chip-ota-provider-app`). The tool will be a thin layer over the
existing `src/app/ota_image_tool.py`: it builds the inner TLV stream, then
hands the result to the existing tool to add the outer Matter header.

---

## 4. Architecture

### 4.1 Files in this directory

| File | Purpose |
|---|---|
| `OTAMultiImageProcessorImpl.{h,cpp}` | Dispatcher. Implements `OTAImageProcessorInterface`. Owns the `tag → processor` map, parses TLV headers, drives lifecycle. |
| `OTATlvProcessor.{h,cpp}` | Base class for every concrete processor. Defines the tag enum, error codes, the `Process()` state machine, and `OTADataAccumulator`. |
| `OTAAppFirmwareProcessor.{h,cpp}` | Concrete processor for the application image. Wraps `esp_ota_*`. Always provided by this module. |
| `OTAHooks.cpp` | Registration hook. `OtaHookInit/Reset/Abort` declared as **weak symbols** so applications can override without forking. Default `OtaHookInit` registers `OTAAppFirmwareProcessor` for tag 1. |
| `BUILD.gn`, `args.gni` | GN integration. Build flags: `chip_enable_multi_ota_requestor`, `chip_enable_multi_ota_encryption` (future). |
| `README.md` | This document. |

Optional future files (not in v1):
- `OTABootloaderProcessor.{h,cpp}` — writes 2nd-stage bootloader to its
  partition; only safe with bootloader OTA support enabled in ESP-IDF.
- `OTAPartitionTableProcessor.{h,cpp}` — writes partitions.bin atomically.
- `OTAFactoryDataProcessor.{h,cpp}` — writes NVS-encoded factory data.

Manufacturer-specific processors (e.g. UART coproc) live in the application
tree, not in this directory. See §9.

### 4.2 Class responsibilities

```
                  +--------------------------------------+
   OTA Requestor  |   OTAImageProcessorInterface         |  ← public Matter contract
                  +-------------------+------------------+
                                      |
                  +-------------------v------------------+
                  |   OTAMultiImageProcessorImpl         |
                  |   - mProcessorMap (tag → processor)  |
                  |   - mCurrentProcessor                |
                  |   - mAccumulator (TLV header buffer) |
                  |   - mHeaderParser (Matter header)    |
                  |   PrepareDownload / ProcessBlock /   |
                  |   Finalize / Apply / Abort           |
                  +-------------------+------------------+
                                      |  delegates per-block
                                      v
                  +--------------------------------------+
                  |   OTATlvProcessor (abstract)         |
                  |   Process(block) state machine       |
                  |   ProcessInternal(block) = 0         |
                  |   ApplyAction() = 0                  |
                  |   AbortAction() = 0                  |
                  |   FinalizeAction() = 0               |
                  |   ExitAction()   default no-op       |
                  +-------------------+------------------+
                                      ^
                          +-----------+-----------+
                          |                       |
            +-------------+-------+   +-----------+--------------+
            | OTAAppFirmware      |   | OTAUartCoprocFirmware    |
            | Processor           |   | Processor (manufacturer) |
            | wraps esp_ota_*     |   | streams over UART        |
            +---------------------+   +--------------------------+
```

**Dispatcher (`OTAMultiImageProcessorImpl`)** knows nothing about flash,
co-processors, or factory data. It only knows TLV framing and lifecycle.

**Base processor (`OTATlvProcessor`)** owns the per-component byte counter
(`mLength`, `mProcessedLength`), descriptor accumulation, and the contract
that "I return `CHIP_OTA_CHANGE_PROCESSOR` when I'm done so the dispatcher
can move on."

**Concrete processors** own the platform-specific work for one component
type. Adding a new component = writing one new subclass.

**OTAHooks.cpp** is the single registration entry point. The default
implementation is a weak symbol; manufacturers either accept the default or
provide their own `OtaHookInit` that registers additional processors.

---

## 5. End-to-End Flow

### 5.1 Build / packaging

1. CI builds each component as usual:
   - `app.bin` from the project's `idf.py build`.
   - `coproc.bin` from a separate build (or pulled from a vendor release).
2. `multi_ota_image_tool.py` reads the JSON manifest, prepends each
   component with its 132-byte descriptor, frames each as a TLV, concatenates
   them, and hands the result to `ota_image_tool.py` to wrap with the Matter
   OTA header. Output: `device.ota`.
3. `device.ota` is uploaded to the OTA Provider (test or production).

The Matter `softwareVersion` in the outer header should reflect "the bundle
as a whole" — bumped whenever any component inside changes. Per-component
version numbers in the descriptors are informational/debugging only; they
do not feed into the OTA Requestor's update-decision logic.

### 5.2 Provider → device transfer

1. Provider announces availability; Requestor on device QueryImage's,
   discovers a higher version, and starts BDX.
2. BDX calls into our dispatcher's `OTAImageProcessorInterface` methods.
   This is identical to the single-image flow today.

### 5.3 Per-block dispatch

```
HandleProcessBlock(block):
    if outer Matter header not yet parsed:
        ProcessHeader(block)   # eats the Matter OTA header bytes
    ProcessPayload(block)

ProcessPayload(block):
    loop:
        if mCurrentProcessor == nullptr:
            mAccumulator.Accumulate(block)    # collect 8 bytes
            if not enough bytes yet:
                return BUFFER_TOO_SMALL       # → ask BDX for more
            SelectProcessor(tlvHeader)        # tag → processor
            mCurrentProcessor->Init()

        status = mCurrentProcessor->Process(block)

        if status == CHIP_OTA_CHANGE_PROCESSOR:
            # this component is fully consumed
            mAccumulator.Clear() / Init(8)
            mCurrentProcessor = nullptr
            if block.size() == 0:
                return CHIP_NO_ERROR          # need to fetch more bytes
            continue                          # next TLV's bytes are right here
        else:
            return status                     # still in-progress, or error
```

The state machine inside `OTATlvProcessor::Process()` does:
1. Compute how many of the available bytes belong to me
   (`min(mLength - mProcessedLength, block.size())`).
2. Hand exactly those to `ProcessInternal()`.
3. If I'm done, call `ExitAction()` and return `CHIP_OTA_CHANGE_PROCESSOR`.

### 5.4 Finalize → Apply → Reboot

After BDX delivers all bytes, the Requestor calls `Finalize()` then
`Apply()`:

```
HandleFinalize():
    for every processor with WasSelected():
        processor.FinalizeAction()      # flush half-full buffers, finish coproc DFU

HandleApply():                          # collect-all-then-commit pattern (NXP-inspired)
    for every processor with WasSelected():
        if processor.ApplyAction() != CHIP_NO_ERROR:
            AbortAllProcessors()        # roll back everything
            requestor.Reset()
            return                      # NO REBOOT — stay on old firmware

    # Single atomic commit point — only reached if every Apply succeeded
    err = esp_ota_set_boot_partition(mNextOtaPartition)
    if err != ESP_OK:
        AbortAllProcessors()
        return

    # Schedule a delayed reboot so subscriptions can be torn down cleanly
    PlatformMgr().HandleServerShuttingDown()
    SystemLayer().StartTimer(kRebootDelayMs, [] { esp_restart(); })
```

Key properties:

- Apply order is the order processors were registered (so register the most
  critical or most reversible component first; see §7).
- The **atomic commit** is `esp_ota_set_boot_partition`. Until this line runs,
  the bootloader still chooses the old app on next reboot.
- Co-processor commit happens *inside* the coproc processor's `ApplyAction`
  (e.g., send "boot new image" command over UART). It happens *before*
  `esp_ota_set_boot_partition`, which means: if the app slot can't be
  committed, the coproc has already been told to boot — see §7.

### 5.5 Boot validation

1. Bootloader boots the new app (app slot was flipped by
   `esp_ota_set_boot_partition`). If the app fails to start (boot loop, hard
   fault before commit), ESP-IDF's anti-rollback rolls back to the previous
   slot.
2. On first boot of the new image, the OTA Requestor calls `IsFirstImageRun()`
   — returns true. Requestor then calls `ConfirmCurrentImage()`.
3. `ConfirmCurrentImage()` checks that `running.softwareVersion ==
   requestor.GetTargetVersion()`, then calls
   `esp_ota_mark_app_valid_cancel_rollback()`. If it doesn't,
   ESP-IDF rolls back on the next reboot.

This is the same pattern the existing single-image `OTAImageProcessorImpl`
uses; nothing new.

---

## 6. Threading Model

```
BDX layer (whatever thread)
   │
   ├─ OTAMultiImageProcessorImpl::ProcessBlock(block)
   │      │
   │      ├─ SetBlock(block)              # heap-copy into mBlock
   │      └─ PlatformMgr().ScheduleWork(HandleProcessBlock, this)
   │                                      ──────────────►
                                                          Matter thread
                                                            │
                                                            ├─ HandleProcessBlock(this)
                                                            │     ├─ ProcessHeader / ProcessPayload
                                                            │     └─ HandleStatus
                                                            │           ├─ FetchNextData (→ BDX)
                                                            │           └─ or error → CancelImageUpdate
                                                            │
                                                            └─ Apply / Finalize / Abort run here too
```

All processor methods (`Init`, `ProcessInternal`, `ApplyAction`,
`AbortAction`, `FinalizeAction`, `ExitAction`) execute on the **Matter
thread**. Concrete implementations therefore do not need their own
synchronization for this path. They *do* need to be careful about long
blocking calls (see §9.4).

---

## 7. Atomicity & Failure Modes

### 7.1 Two-stage commit

The dispatcher uses a **collect-all-then-commit** approach, borrowed from NXP:

1. Every processor's `ApplyAction()` runs in registration order. Each may do
   its own internal commit (e.g., send "switch to new image" over UART).
2. **After every `ApplyAction` returns OK**, the dispatcher calls
   `esp_ota_set_boot_partition()` exactly once. This is the only step that
   makes the new app the next boot target.
3. If any `ApplyAction` fails, every selected processor's `AbortAction()` is
   invoked and the device does not reboot.

### 7.2 Component ordering

Order matters. The recommended convention is **most-reversible first,
least-reversible last** so a failure stops the chain before the
hard-to-undo step. Concretely:

```
[ Coproc FW ]   ← applied first; if coproc DFU fails, app is never committed
[ App FW    ]   ← committed last via esp_ota_set_boot_partition
```

Alternative: if the coproc has an A/B slot mechanism that can roll itself
back independently, app-first is also reasonable.

### 7.3 Failure-mode matrix

| Where it dies | What survives | Recovery |
|---|---|---|
| Mid-download (network, power) | Old app in active slot, OTA partition partially written | Next QueryImage retries — OTA partition is overwritten cleanly. |
| `FinalizeAction()` returns error | Old app, partial coproc state | `Abort()` is called; each processor cleans up. |
| `ApplyAction()` for coproc fails | Old app on main MCU, possibly half-flashed coproc | Coproc processor's `AbortAction` should put the coproc back into a recoverable state (e.g., DFU mode pending next boot). The main MCU does not reboot. |
| `esp_ota_set_boot_partition` fails after coproc Apply succeeded | Old main MCU app + new coproc fw | This is the hardest case. Mitigations: (a) coproc supports A/B; (b) main MCU on boot detects coproc-version mismatch and re-flashes coproc from a known-good blob in main flash. |
| Power loss after `esp_ota_set_boot_partition`, before reboot | New app will be booted | This is a normal, supported state — the bootloader sees the flipped slot. |
| New app crashes during boot | Old app | ESP-IDF anti-rollback restores the previous slot; `IsFirstImageRun` won't be invoked because the new image never confirmed. |
| New app boots but `ConfirmCurrentImage` is not called | Old app on next reboot | ESP-IDF rolls back. App is responsible for confirming. |

### 7.4 What "atomic" means here

Truly atomic across the main MCU and an external chip is impossible without a
shared transaction log between them. We get **best-effort atomicity**:
- Within the main MCU: real atomicity via `esp_ota_set_boot_partition`.
- Across main + coproc: ordering + per-processor abort + boot-time
  reconciliation (manufacturer-implemented; see §9.7).

---

## 8. Responsibilities

### 8.1 This module provides

- The dispatcher (`OTAMultiImageProcessorImpl`) implementing
  `OTAImageProcessorInterface`.
- The base class (`OTATlvProcessor`) and helpers (`OTADataAccumulator`,
  TLV header struct, tag enum, error codes).
- A reference application processor (`OTAAppFirmwareProcessor`) that wraps
  `esp_ota_*` for the standard ESP32 app slot.
- The default `OtaHookInit` (weak symbol) that registers the app processor
  for tag 1.
- Build integration via `BUILD.gn` and `args.gni`.
- This document.

### 8.2 ESP-IDF platform code provides

- `esp_ota_*` for app-slot writing and boot-partition flipping.
- `esp_partition_*` for partition discovery.
- `esp_ota_mark_app_valid_cancel_rollback()` / `esp_ota_check_rollback_is_possible()`
  for boot validation.
- `esp_restart()` for the eventual reboot.

These are not wrapped or duplicated; concrete processors call them directly.

### 8.3 Application / manufacturer provides

For a basic app-only setup: **nothing**. Enable
`chip_enable_multi_ota_requestor`, ship a single-component bundle, and the
default hook handles it.

For a multi-component product, the manufacturer provides:

1. **One concrete `OTATlvProcessor` subclass per non-app component.** E.g.
   `OTAUartCoprocFirmwareProcessor` that streams bytes to a secondary chip.
   See §9 for the contract.
2. **An override of `OtaHookInit`** that registers each custom processor
   under its chosen tag, in the desired apply order. Linker resolves the
   override over the weak default.
3. **Build-time packaging integration.** The manufacturer's CI must call
   `multi_ota_image_tool.py` with a JSON manifest of components.
4. **A boot-time consistency check**, if the product uses an external chip.
   The main MCU should query the coproc's version on boot and trigger
   re-flash if it doesn't match the expected pair.
5. **A versioning policy.** When and how the outer `softwareVersion` is
   bumped — typically: bump on any component change.
6. **Tests.** Unit tests per processor and an integration test that ships a
   two-component bundle through `chip-ota-provider-app`.

### 8.4 OTA Provider / build tooling provides

- A reachable Matter OTA Provider that hosts the `.ota` file.
- The `multi_ota_image_tool.py` script, run as part of release CI.
- Operational practice: never publish a bundle with a downgraded
  `softwareVersion`.

---

## 9. Adding a New Processor (Manufacturer Workflow)

This section is the practical "I have a UART-attached coproc, what do I
write?" guide.

### 9.1 Pick a tag

Pick **any `uint32_t` value ≥ 65536** for a manufacturer-defined component.
There is no list to add to and no cap on the number of components. Define
the value as a `constexpr` next to the processor class, e.g.:

```cpp
constexpr uint32_t kUartCoprocTag = 0x00010001;
```

Document the value in the product's release notes — it's a stable contract
with the packaging tool. Don't reuse values across unrelated components in
the same product, and don't repurpose a value across releases.

### 9.2 Subclass `OTATlvProcessor`

Minimum required overrides:

```cpp
class OTAUartCoprocFirmwareProcessor : public OTATlvProcessor {
public:
    CHIP_ERROR ApplyAction()    override;   // commit on the coproc side
    CHIP_ERROR AbortAction()    override;   // undo any partial state
    CHIP_ERROR FinalizeAction() override;   // flush at end of download

private:
    CHIP_ERROR ProcessInternal(ByteSpan & block) override;  // stream bytes over UART
    CHIP_ERROR ProcessDescriptor(ByteSpan & block);
    bool mDescriptorProcessed = false;
    bool mDfuStarted          = false;
};
```

### 9.3 Implement the four required methods

| Method | Responsibility |
|---|---|
| `ProcessInternal(block)` | First call: eat the 132-byte descriptor (use `mAccumulator`). Second call: put coproc into DFU mode (GPIO + start frame). Subsequent calls: frame the bytes per the coproc bootloader's protocol and send over UART; wait for ACK. |
| `FinalizeAction()`        | Send "end of image / verify CRC" frame to the coproc. Do **not** reboot the coproc here. |
| `ApplyAction()`           | Send "commit & boot new image" frame to the coproc. Return success/failure. |
| `AbortAction()`           | Reset coproc, leave it in a known-recoverable state (e.g. trigger its rescue bootloader on next boot). |

### 9.4 Don't block on UART

Fast UARTs and large coproc images can stall the Matter thread. If a frame
takes more than a few ms to ack:

1. Schedule the actual UART write to a worker task / DMA.
2. From `ProcessInternal`, return `CHIP_OTA_FETCH_ALREADY_SCHEDULED`.
3. When the UART layer drains, call
   `OTAMultiImageProcessorImpl::FetchNextData(0)` to resume BDX.

This is the same pattern the Silabs WiFi processor uses for SPI to the
SiWx917.

### 9.5 Register the processor

Override the weak `OtaHookInit`:

```cpp
extern "C" CHIP_ERROR OtaHookInit() {
    auto & dispatcher = OTAMultiImageProcessorImpl::GetDefaultInstance();

    // Register coproc FIRST so it applies before the main app commit.
    static OTAUartCoprocFirmwareProcessor sCoproc;
    sCoproc.RegisterDescriptorCallback(OTAMultiImageProcessorImpl::ProcessDescriptor);
    ReturnErrorOnFailure(
        dispatcher.RegisterProcessor(kUartCoprocTag, &sCoproc));

    // Then the application processor (default).
    static OTAAppFirmwareProcessor sApp;
    sApp.RegisterDescriptorCallback(OTAMultiImageProcessorImpl::ProcessDescriptor);
    ReturnErrorOnFailure(
        dispatcher.RegisterProcessor(WellKnownTag::kApplication, &sApp));

    return CHIP_NO_ERROR;
}
```

The static-storage discipline (function-local `static`) gives the processor
program lifetime without heap allocation.

### 9.6 Wire up packaging

Add the coproc binary and tag to the product's release manifest:

```json
{
  "ota_version": "1.4.0",
  "components": [
    { "tag": 8, "version": 17, "version_string": "coproc-1.17", "path": "build/coproc.bin" },
    { "tag": 1, "version": 14, "version_string": "app-1.4.0",   "path": "build/app.bin"    }
  ]
}
```

Note: the order in the JSON array determines the order in the OTA file,
which determines the order of `ApplyAction` calls.

### 9.7 Boot-time consistency check

In the application's `main`, before initializing Matter:

```cpp
uint32_t coprocVersion = 0;
if (uart_get_coproc_version(&coprocVersion) != ESP_OK
    || !IsCompatible(kAppVersion, coprocVersion)) {
    // Re-flash coproc from a known-good blob shipped inside the app, or
    // mark device unhealthy and refuse to advertise on Matter.
    RecoverCoproc();
}
```

This is the recovery story for case "main app committed but coproc didn't"
in §7.3.

---

## 10. Build Configuration

### 10.1 GN args

```
# args.gni (in this directory)
declare_args() {
    chip_enable_multi_ota_requestor  = false   # master switch
    chip_enable_multi_ota_encryption = false   # AES-CTR over each TLV (future)
}
```

### 10.2 Wiring into the example application

In the example's top-level `BUILD.gn`:

```
import("${chip_root}/src/platform/ESP32/multi-ota/args.gni")

deps += []
if (chip_enable_multi_ota_requestor) {
    deps += [ "${chip_root}/src/platform/ESP32/multi-ota" ]
} else {
    deps += [ "${chip_root}/src/platform/ESP32:OTAImageProcessorImpl" ]  # legacy
}
```
  
The two paths are mutually exclusive — they both define
`OTAImageProcessorInterface` but at most one can be linked.

### 10.3 sdkconfig requirements

The product's `sdkconfig.defaults` must enable:

- `CONFIG_PARTITION_TABLE_TWO_OTA` (or three) — at least one OTA app slot.
- `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` — for the boot-validation safety net.
- `CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK` — recommended.

---

## 11. Versioning Strategy

There are three distinct version numbers. Don't conflate them:

| Number | Where it lives | Who reads it | Semantics |
|---|---|---|---|
| Matter `softwareVersion` | Outer OTA header & `BasicInformation` cluster | OTA Requestor / Provider | "Is there a newer bundle?" — bumped whenever any component changes. |
| Per-component `descriptor.version` | 132-byte descriptor inside each TLV | Logging, sanity checks | Per-component release version. Free-form per manufacturer. |
| Coproc-reported version | Read at runtime over UART (or other transport) | Boot-time consistency check | Ground truth of what's actually installed on the coproc right now. |

### 11.1 Recommended discipline

- The outer `softwareVersion` strictly increases on every release.
- The per-component versions are informational; do not branch on them in
  the OTA Requestor.
- The boot-time check compares **installed** versions against an expected
  table compiled into the app, not against the descriptor in the OTA file.

---

## 12. Testing

### 12.1 Unit tests

For each processor: feed synthesized byte streams into `Process()` and
verify the resulting `esp_ota_*` / UART side effects (use fakes).
Tests should cover:

- Descriptor parsing.
- Boundary cases (block ends exactly at TLV boundary).
- Block split across two TLVs.
- `ApplyAction` failure path → `AbortAction` is called.
- `FinalizeAction` no-op when the processor was never selected.

### 12.2 Integration tests

A QEMU- or hardware-based test that:

1. Builds an app + a stub coproc image.
2. Packages them with `multi_ota_image_tool.py`.
3. Serves them via `chip-ota-provider-app`.
4. Runs the device under test, confirms it downloads, applies, reboots,
   and confirms the new image.

### 12.3 Failure injection

Hook into the coproc processor to fail at: `Init`, mid-`ProcessInternal`,
`FinalizeAction`, `ApplyAction`. For each, verify the device stays on the
old app (i.e., `esp_ota_set_boot_partition` was never called).

---

## 13. Open Questions / Future Work

1. **Encryption.** Silabs and NXP support AES-CTR over each TLV's value
   (gated by a build flag). v1 leaves this out; the hooks (`mIVOffset`,
   `vOtaProcessInternalEncryption`) can be added later without an ABI
   break.
2. **Per-component signatures.** Today the outer Matter OTA file is
   integrity-protected by the OTA Provider's signing. If a manufacturer
   wants a second-layer signature (e.g., the coproc vendor's signing key
   over `coproc.bin`), the per-processor `ApplyAction` is the place to
   verify it. Not built in.
3. **Resume on disconnect.** BDX retries; we don't currently checkpoint
   per-component progress. If the device reboots mid-download, it
   re-downloads from byte 0. Acceptable for most products.
4. **Bootloader update.** ESP-IDF supports bootloader OTA only with
   specific configurations; the `OTABootloaderProcessor` is left out of v1.
5. **Concurrent DFU sources.** If a product also exposes serial DFU or
   USB DFU, a `DFUSync`-style mutex should be added (Nordic pattern).
6. **Reboot delay tuning.** NXP ships a configurable delay; we'll start
   with a fixed value (e.g., 2 seconds) and revisit if subscribers
   complain.

---

## 14. References

- Silabs reference implementation: `src/platform/silabs/multi-ota/`.
- NXP reference implementation: `src/platform/nxp/common/ota/`.
- Nordic delegating implementation: `src/platform/nrfconnect/OTAImageProcessorImpl.cpp`.
- Matter OTA Requestor interface: `src/include/platform/OTAImageProcessor.h`.
- Existing single-image ESP32 OTA: `src/platform/ESP32/OTAImageProcessorImpl.{h,cpp}`.
- ESP-IDF OTA API: `components/app_update/include/esp_ota_ops.h`.
- Matter OTA image tool: `src/app/ota_image_tool.py`.
