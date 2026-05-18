# Multi-Image OTA — Design Specification

> Status: design draft. No code exists yet.

---

## 1. Overview

The Matter OTA Requestor today downloads a single binary per OTA session and
hands every byte to one OTA image processor implementation. This is
sufficient for devices where only the host firmware changes.

Modern IoT products routinely contain multiple programmable components: a
co-processor, a radio module, a display controller, a peripheral hub. Each
component ships its own firmware and may need updating independently. Running
separate OTA sessions per component — one per BDX transfer — requires
multi-session orchestration that the standard OTA Requestor does not support
and that providers are not required to handle.

This design solves the problem in a single BDX session by bundling all
component firmwares into one `.ota` file with a structured header. The host
device downloads the bundle once, routes each component's bytes to the right
handler, and skips components that are not ready or do not need updating — all
without any changes to the OTA Requestor, Provider protocol, or BDX layer
beyond two small fixes documented in §14.

---

## 2. Glossary

| Term | Definition |
|---|---|
| **Bundle** | A single `.ota` file containing the `MultiImageHeader`, one `SubImageHeader` per component, and all component binaries concatenated. |
| **Main Image Processor** | The OTA image processor implementation the Matter OTA Requestor calls. Owns the full download lifecycle and contains the Dispatcher. |
| **Dispatcher** | The routing engine inside the Main Image Processor. Parses the bundle header, maintains the `imageId → sub-processor` map, and routes or skips each component's bytes. |
| **Sub Image Processor** | An application-provided handler for one component. Implements `IsReadyForOTA(targetVersion)`, `Init()`, and `Write()`. |
| **Image ID** | A `uint8_t` that identifies which sub-processor handles a given component. Stable ABI between firmware and packaging tool. |
| **`SkipData(n)`** | BDX `BlockQueryWithSkip` — tells the Provider to advance its cursor by `n` bytes without delivering them. |
| **Confirmation** | The platform action that marks newly booted firmware as valid and cancels any pending rollback. Triggered by `ConfirmCurrentImage()`. |
| **`softwareVersion`** | The Matter bundle version in the outer OTA header. Represents the state where every component in the bundle is at its expected version. |

---

## 3. OTA File Format

The Matter OTA file header (vendor id, product id, software version, payload
size, …) is unchanged. The **payload** now begins with a `MultiImageHeader` that
describes every binary inside the bundle, followed by the binary data blobs at
the offsets the header specifies. The dispatcher parses the `MultiImageHeader`
first, then routes each binary's bytes to the correct processor — using
`SkipData()` for any binary whose processor is absent or not ready.

### 3.1 Outer layout

```
+---------------------------------------------------------+
| Matter OTA header (chip-ota-image-tool format)          |
|   vendor_id, product_id, software_version, ...         |
+---------------------------------------------------------+  ← offset 0 of payload
| MultiImageHeader  (8 bytes)                             |
|   magic(4)  num_images(1)  reserved(3)                  |
+---------------------------------------------------------+
| SubImageHeader[0]  (60 bytes)                              |
| SubImageHeader[1]  (60 bytes)                              |
| ...                                                     |
| SubImageHeader[N-1]  (60 bytes)                            |
+---------------------------------------------------------+
| Binary 0 data   (at SubImageHeader[0].offset bytes)        |
+---------------------------------------------------------+
| Binary 1 data   (at SubImageHeader[1].offset bytes)        |
+---------------------------------------------------------+
| ...                                                     |
+---------------------------------------------------------+
```

All `offset` values are relative to the start of the OTA payload (byte 0
immediately after the outer Matter OTA header ends). All multi-byte fields are
**little-endian**.

### 3.2 MultiImageHeader

The `MultiImageHeader` is always 8 bytes at the start of the payload:

```c
#define MULTI_IMAGE_HEADER_MAGIC  0x4D494F54u  // "MIOT"

typedef struct __attribute__((packed)) {
    uint32_t magic;         // must equal MULTI_IMAGE_HEADER_MAGIC
    uint8_t  numImages;     // number of SubImageHeader entries (max 255)
    uint8_t  reserved[3];   // must be zero; reserved for future extensions
} MultiImageHeader;    // 8 bytes: 4+1+3
```

Total header size on the wire:

```
headerBytes = sizeof(MultiImageHeader) + numImages * sizeof(SubImageHeader)
             = 8 + numImages * 60
```

The dispatcher accumulates exactly `headerBytes` before it switches from
header-parsing mode to data-routing mode.

### 3.3 Per-image header

Each `SubImageHeader` is a fixed 60 bytes regardless of the binary it describes:

```c
typedef struct __attribute__((packed)) {
    uint8_t  imageId;       // identifies which processor handles this binary (§3.4)
    uint32_t version;       // version of this binary
    uint64_t offset;        // byte offset of binary data within the OTA payload
    uint64_t length;        // byte count of the binary
    uint8_t  sha256[32];    // SHA-256 digest of the binary (mandatory)
    uint8_t  reserved[7];   // must be zero; reserved for future extensions
} SubImageHeader;              // 60 bytes: 1+4+8+8+32+7
```

Field semantics:

| Field      | Notes                                                                                                                                                                                   |
| ---------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `imageId`  | Matched against registered processors at runtime. A bundle may carry `imageId`s not registered on a given device — those entries are skipped via `SkipData()`.             |
| `version`  | Per-binary version number. Informational and for sanity checks only; the OTA Requestor's update decision is driven by the outer Matter `softwareVersion` alone.             |
| `offset`   | Absolute byte offset from payload start. Enables the dispatcher to jump directly to each binary with a single `SkipData()` call rather than streaming all preceding bytes. |
| `length`   | Exact byte count. Used both to know when to stop feeding the processor and to compute the skip distance for unneeded binaries.                                              |
| `sha256`   | Mandatory SHA-256 of the binary at `[offset, offset+length)`. The sub-processor must verify this digest before accepting the update as complete.                            |
| `reserved` | Must be zero on write. Readers must ignore. Enables future field additions without a `version` bump.                                                                        |

> **Alignment note.** `SubImageHeader` is a packed struct. The `version` field
> sits at byte offset 1 and is therefore not naturally 4-byte aligned. All
> code that reads `SubImageHeader` fields must use `memcpy` into a local
> variable rather than direct pointer-cast and field access. Direct access will
> fault or produce incorrect results on platforms that enforce aligned memory
> access.

### 3.4 Image ID space

`imageId` is a `uint8_t` (values 0–255). 256 slots are sufficient for any
realistic multi-image bundle, and the compact size keeps `SubImageHeader`
simple. The namespace is partitioned to avoid collisions:

| Range        | Owner        | Use                                                                                                                            |
| ------------ | ------------ | ------------------------------------------------------------------------------------------------------------------------------ |
| `0`          | —            | Invalid. Rejected at parse time.                                                                                               |
| `1 .. 15`    | Platform     | Reserved for platform-defined well-known images. Specific assignments are an implementation detail and not fixed by this spec. |
| `16 .. 127`  | Reserved     | Held for future framework growth. Must not be used by applications.                                                            |
| `128 .. 255` | Manufacturer | **Manufacturer-defined.** Pick any value in this range; no central registry.                                                   |

The specific well-known IDs within range `1–15` (e.g. which value means
"application firmware", "bootloader", "partition table") are determined by the
implementation and documented alongside the processor registration code. This
spec does not assign them.

Manufacturer-defined IDs in range `128–255` carry no inherent meaning beyond
what the device firmware and packaging tool agree on. There is no list to add to
and no central authority.

The only runtime validation is **duplicate registration** —
`RegisterProcessor(imageId, ...)` fails if `imageId` is already registered. The
`imageId`-to-meaning mapping is a stable ABI contract between the device
firmware and the packaging tool. Repurposing an ID across firmware versions is
an ABI break for that product.

### 3.5 Packaging tool

Producing a bundle is a two-step process:

**Step 1 — multi-image payload tool.** Takes a manifest listing each binary's
image ID, version, and path:

```json
{
    "images": [
        { "id": 1, "version": 14, "path": "build/app.bin" },
        { "id": 128, "version": 17, "path": "build/coproc.bin" }
    ]
}
```

The tool:

1. Reads each binary and computes its SHA-256 digest.
2. Computes each binary's `offset` (header section size + sum of preceding
   binary sizes, with any desired alignment padding).
3. Serialises the `MultiImageHeader` + `SubImageHeader[]` into the header blob.
4. Concatenates header blob + binaries into a raw payload file (e.g. `payload.bin`).

**Step 2 — `chip-ota-image-tool`.** Wraps `payload.bin` with the standard outer
Matter OTA header (vendor ID, product ID, software version, digest, …) to
produce the final `.ota` file ready to serve from a Matter OTA Provider. The
outer header fields are passed to `chip-ota-image-tool` as usual — this spec
does not change that tool or its invocation.

---

## 4. Architecture

### 4.1 Components

The system is built from three roles:

**Main Image Processor** — the single object the Matter OTA Requestor knows
about. It implements the OTA image processor interface and owns the entire OTA
download and apply cycle: it receives every byte block from the BDX session,
drives the overall prepare → process → finalize → apply → abort lifecycle, and
contains the Dispatcher.

**Dispatcher** — the routing engine inside the Main Image Processor. It parses
the `MultiImageHeader` and `SubImageHeader[]` from the incoming byte stream,
maintains the `imageId → sub-processor` map populated by application
registration, and redirects each byte chunk to the sub-processor that matches
the current `SubImageHeader.imageId`. When no sub-processor is registered for an
entry, or when the matched sub-processor returns not-ready, the Dispatcher calls
`SkipData()` to advance past that entry without consuming it.

**Sub Image Processor** — an interface the application registers per image ID.
Three methods form the contract:

-   `IsReadyForOTA(uint32_t targetVersion)` → `OTAReadiness` — called once
    before bytes start, with the `SubImageHeader.version` for this entry. The
    sub-processor compares `targetVersion` against its installed version and
    returns one of three values: `kReady` (proceed), `kAlreadyUpToDate` (already
    at `targetVersion`, skip but counts as verified), `kNotReady` (skip, blocks
    confirmation). Must return quickly — no blocking I/O.
-   `Init(entry)` — called once with the full `SubImageHeader` immediately
    before the first `Write()`. Gives the sub-processor `entry.length` (total
    bytes to expect), `entry.version`, and `entry.sha256` upfront.
-   `Write(block)` — called per chunk. The application does whatever is needed
    with the bytes. The sub-processor knows it has received the last chunk when
    its running byte total reaches `entry.length` (set by `Init()`).

`OTAReadiness` is a three-value enum — not a boolean — because the
confirmation policy (§11.1) requires distinguishing "already up to date" from
"unavailable":

| Value | Dispatcher action | Counts as verified? |
|---|---|---|
| `kReady` | Call `Init()` then `Write()` per chunk | Yes |
| `kAlreadyUpToDate` | `SkipData(entry.length)` | Yes — already at target version |
| `kNotReady` | `SkipData(entry.length)` | **No** — blocks `softwareVersion` confirmation |

The sub-processor owns all state tracking for its component. The framework
defines the interface; applications provide the implementations.

### 4.2 Component relationships

```
   Matter OTA Requestor
          |
          | OTA Image Processor Interface
          v
  +-------+------------------------------------------+
  |              Main Image Processor                 |
  |   records OTAReadiness per entry                  |
  |   blocks confirmation if any = kNotReady          |
  |   +-------------------------------------------+  |
  |   |              Dispatcher                   |  |
  |   |   MultiImageHeader + SubImageHeader[]     |  |
  |   |   imageId → sub-processor map             |  |
  |   |   routes chunks / calls SkipData()        |  |
  |   +--------+----------------------------------+  |
  +------------+-------------------------------------+
               |
               | IsReadyForOTA(targetVersion) → OTAReadiness
               | Init(SubImageHeader) once if kReady
               | Write(block) per chunk
               v
  +------------+-------------------------------------+
  |      Sub Image Processor (interface)             |
  |   IsReadyForOTA(targetVersion)      (app-impl)  |
  |   Init(SubImageHeader &)            (app-impl)  |
  |   Write(block)      → result        (app-impl)  |
  +--------------------------------------------------+
               ^                   ^
               |                   |
       [App firmware]     [Co-processor / any]
       (registered by     (registered by
        platform layer)    application)
```

**Main Image Processor** receives every BDX block. Before any data arrives it
triggers the Dispatcher to begin header accumulation. Once the full header
section is parsed the Dispatcher switches to routing mode. It records the
`OTAReadiness` result for each entry and uses it in `ConfirmCurrentImage()`
(§5.5, §11.1).

**Dispatcher** — for each entry: looks up the sub-processor by `imageId`, calls
`IsReadyForOTA()`. If `kReady`, calls `Init(entry)` then feeds chunks via
`Write()`. If `kAlreadyUpToDate` or `kNotReady` (or no processor registered),
calls `SkipData(entry.length)`. Advances when `entry.length` bytes are
accounted for.

**Sub Image Processor** — `Init()` supplies total length, version, and digest
before the first `Write()`. The sub-processor detects its last chunk when its
running byte count reaches `entry.length`.

**Registration** — applications call a register function before the OTA session
starts, associating an `imageId` with a sub-processor instance. Each `imageId`
may have at most one registered processor; duplicate registration is an error.

---

## 5. End-to-End Flow

### 5.1 Build / packaging

1. CI builds each binary independently (primary firmware, co-processor firmware,
   any other components).
2. The multi-image payload tool reads the manifest (§3.5), computes SHA-256 per
   binary, calculates offsets, serialises the `MultiImageHeader` +
   `SubImageHeader[]`, and concatenates all binaries into a raw payload file.
   `chip-ota-image-tool` then wraps that payload with the outer Matter OTA
   header. Output: a single `.ota` bundle.
3. The `.ota` bundle is uploaded to the OTA Provider (test or production).

The Matter `softwareVersion` in the outer header reflects "the bundle as a
whole" and is bumped whenever any binary inside changes. Per-binary `version`
fields inside the `SubImageHeader` entries are informational and for sanity
checks only; the OTA Requestor's update decision uses only the outer
`softwareVersion`.

### 5.2 Provider → device transfer

1. Provider announces availability; Requestor on device QueryImage's, discovers
   a higher version, and starts BDX.
2. BDX calls into the Main Image Processor via the standard OTA image processor
   interface. This is identical to the single-image flow from the Requestor's
   perspective — the multi-image logic is entirely inside the Main Image
   Processor.

### 5.3 Per-block dispatch

Processing has two phases gated by whether the full header section has been
accumulated.

```
# Persistent state across blocks:
#   headerParsed        : bool   — false until full header section is accumulated
#   headerAccumulator   : buffer — accumulates header bytes across blocks
#   accumulated         : uint64 — bytes in headerAccumulator so far
#   headerNeeded        : uint64 — total header bytes required; starts as
#                                  sizeof(MultiImageHeader), updated once numImages known
#   numImages           : uint8_t
#   subImages[]         : parsed SubImageHeader entries
#   currentIndex        : uint8_t — index of entry being processed
#   entryStarted        : bool   — Init() has been called for currentIndex
#   bytesDelivered      : uint64 — bytes delivered to current sub-processor
#   currentStreamOffset : uint64 — payload cursor (bytes from payload start)

OnBlockReceived(block):
    strip outer Matter OTA header bytes if not yet consumed
    err = ProcessPayload(block)
    if err != OK:
        EndDownload(err)   # aborts BDX session; triggers Abort() on image processor

ProcessPayload(block) → error:

    # ── Phase 1: header accumulation ────────────────────────────────────
    if not headerParsed:

        toConsume = min(headerNeeded - accumulated, block.size())
        append block[0 .. toConsume] to headerAccumulator
        accumulated += toConsume
        block = block[toConsume ..]          # trim consumed bytes from block

        if accumulated < sizeof(MultiImageHeader):
            FetchNextData()
            return OK

        if headerNeeded == sizeof(MultiImageHeader):
            # MultiImageHeader is complete — parse it to learn numImages
            verify magic; return CHIP_ERROR_INVALID_ARGUMENT if magic is wrong
            if numImages == 0:
                headerParsed        = true
                currentStreamOffset = accumulated
                FetchNextData()
                return OK
            headerNeeded = sizeof(MultiImageHeader) + numImages * sizeof(SubImageHeader)
            # consume any SubImageHeader bytes already in hand
            toConsume = min(headerNeeded - accumulated, block.size())
            append block[0 .. toConsume] to headerAccumulator
            accumulated += toConsume
            block = block[toConsume ..]

        if accumulated < headerNeeded:
            FetchNextData()
            return OK

        parse all SubImageHeader entries from headerAccumulator into subImages[]
        headerParsed        = true
        currentIndex        = 0
        entryStarted        = false
        bytesDelivered      = 0
        currentStreamOffset = headerNeeded   # payload cursor starts right after headers
        # block now contains any remaining payload bytes — fall through

    # ── Phase 2: data routing ────────────────────────────────────────────
    while currentIndex < numImages and not block.empty():
        entry = subImages[currentIndex]

        if not entryStarted:
            # skip any alignment padding between currentStreamOffset and entry.offset
            gap = entry.offset - currentStreamOffset
            if gap > 0:
                currentStreamOffset += gap   # advance cursor over gap
                SkipData(gap)                # BlockQueryWithSkip — also the next-block request
                return OK                   # next block arrives starting at entry.offset

            processor = processorMap.Find(entry.imageId)
            readiness = (processor != null) ? processor.IsReadyForOTA(entry.version)
                                            : kNotReady
            recordReadiness(entry.imageId, readiness)  # stored for ConfirmCurrentImage

            if readiness != kReady:
                currentStreamOffset += entry.length  # advance cursor over skipped entry
                SkipData(entry.length)               # BlockQueryWithSkip
                currentIndex++
                entryStarted   = false
                bytesDelivered = 0
                return OK                            # SkipData is the next-block request

            processor.Init(entry)   # pass full SubImageHeader before first Write
            entryStarted = true

        # feed bytes to sub-processor
        toDeliver = min(entry.length - bytesDelivered, block.size())
        err = processor.Write(block[0 .. toDeliver])
        if err != OK:
            return err               # propagated as ProcessBlock() error; BDX aborted

        bytesDelivered      += toDeliver
        currentStreamOffset += toDeliver   # keep cursor in sync with delivered bytes
        block = block[toDeliver ..]        # consume delivered bytes from block

        if bytesDelivered == entry.length:
            # entry complete — Dispatcher advances, not the sub-processor
            currentIndex++
            entryStarted   = false
            bytesDelivered = 0
            continue   # remaining block bytes may belong to next entry

    FetchNextData()   # block exhausted — request next block from Provider
    return OK
```

**Key properties:**
- `currentStreamOffset` is the single source of truth for the payload cursor. It
  is initialised to `headerNeeded` when the header is parsed and incremented on
  every byte consumed — whether delivered via `Write()`, skipped via `SkipData()`,
  or jumped over as an alignment gap.
- `SkipData()` issues a BDX `BlockQueryWithSkip` message, which is also the
  next-block request. After calling `SkipData()` the function must return
  immediately — calling `FetchNextData()` as well would issue a second request.
- `FetchNextData()` must be called explicitly at the end of every normal block
  (when the while loop exits because the block is empty). Without it the BDX
  session stalls.
- `Write()` errors propagate as the return value of `ProcessBlock()`. The BDX
  layer checks this return and calls `Abort()` + ends the session.
- The Dispatcher owns the byte counter (`bytesDelivered`) and advances entries.
  The sub-processor does not signal completion.
- `OTAReadiness` is recorded per entry at `IsReadyForOTA()` time and used
  later at `ConfirmCurrentImage()`.
- `Init(entry)` is called before the first `Write()` so the sub-processor has
  `entry.length` and `entry.sha256` upfront.

### 5.4 Finalize → Apply → Reboot

After BDX delivers all bytes, the OTA Requestor calls `Finalize()` then
`Apply()` on the Main Image Processor:

**`Finalize()`** — called when the byte stream is complete. The Main Image
Processor performs any platform-level verification needed before committing
(e.g., integrity checks, confirming all selected entries received their full
byte count). Sub-processors have already received all their bytes via `Write()`
during the download; no additional callback is issued to them at this point.

**`Apply()`** — commits the update at the platform level and schedules a reboot.
For the primary application image this means making the newly written firmware
the active boot target. If `Apply()` fails the device must not reboot; it stays
on the currently running firmware.

After a successful `Apply()`, a brief delay allows active connections to be torn
down cleanly before the device restarts.

**What sub-processors do at apply time** is determined by the application. If a
co-processor or peripheral requires an explicit "commit" or "activate" command
after all bytes are delivered, that logic belongs in the application's `Write()`
implementation (e.g., on the last chunk) or in a separate application-level hook
invoked after `Apply()`. The framework does not define a commit lifecycle on the
sub-processor interface.

### 5.5 Boot validation and `softwareVersion` confirmation

1. On the next boot the platform starts the newly committed firmware. If the
   firmware fails to start (crash loop, hard fault before confirming), the
   platform's rollback mechanism restores the previously running firmware.
2. On the first successful boot of the new firmware, the OTA Requestor calls
   `IsFirstImageRun()` — returns true. The Requestor then calls
   `ConfirmCurrentImage()`.
3. The multi-image `ConfirmCurrentImage()` performs **two checks** before
   signalling the platform to mark the firmware as confirmed:
   a. The running `softwareVersion` matches the version the Requestor
      downloaded.
   b. Every sub-processor entry recorded as `kNotReady` during the download
      is now verified to be at its expected version. If any `kNotReady` entry
      cannot be verified, confirmation is withheld.
4. If either check fails, `ConfirmCurrentImage()` returns an error. Before
   triggering the rollback reboot, the Main Image Processor invokes the retry
   policy hook (§5.7) so the platform can schedule a `QueryImage` re-trigger
   for immediately after the rollback completes. The device reverts to the old
   `softwareVersion` and the OTA window reopens.

This extends the single-image confirmation flow with step 3b. The Main Image
Processor retains the per-entry `OTAReadiness` map from the download session
across the reboot (persisted in NVS or equivalent) so that the post-boot
check can evaluate it. Once `ConfirmCurrentImage()` succeeds — meaning all
components are verified and the new `softwareVersion` is committed — the Main
Image Processor **must erase the NVS readiness map and `attemptCount`**. This
ensures the next OTA cycle (for `softwareVersion + 1` or any future bundle)
starts with a clean slate and does not inherit stale `kAlreadyUpToDate` entries
from the previous cycle.

> **Why this matters:** see §11.1. Confirming `softwareVersion` before all
> components are verified permanently closes the OTA window for any skipped
> components. The Provider will not offer the bundle again. The active retry
> mechanism (§5.7) ensures the device does not have to wait for the next
> periodic query timer after a rollback.

### 5.6 Processor Readiness and `SkipData`

Before feeding bytes to a sub-processor, the Dispatcher calls `IsReadyForOTA()`
on it. If it returns `kNotReady` or `kAlreadyUpToDate` (or no processor is
registered for that image ID), the Dispatcher calls `SkipData(entry.length)` to
tell the Provider to advance its cursor past that entire binary. Those bytes are
never delivered to the device. The Dispatcher then moves on to the next
`SubImageHeader` entry.

#### Critical semantics

**Skipped means permanently skipped for this OTA cycle.** The Provider will not
re-send those bytes. A component whose processor returns `kNotReady` is not
updated this cycle and blocks `softwareVersion` confirmation. A component
returning `kAlreadyUpToDate` is also skipped but counts as verified — it does
not block confirmation. Think of `kNotReady` as "opt out of this update," not
"defer until later in this session."

**`IsReadyForOTA()` must return immediately.** In ReceiverDrive mode the
Provider waits for the next `BlockQuery` or `BlockQueryWithSkip`. If the
Requestor goes silent for too long the Provider will terminate the session with
a status-report timeout. The readiness check must complete in milliseconds. Any
initialization that takes longer (bus negotiation, boot-mode handshake) must be
done before the OTA session begins.

**Mixed-version state after a `kNotReady` skip.** If one binary is updated and
another is skipped with `kNotReady`, the device ends the OTA cycle with
mismatched versions across its components. The application is responsible for
detecting and handling this (§7.4, §9.6). Do not skip a component unless the
device can tolerate running the old version of that component alongside new
versions of the others.

#### `SkipData()` — session layer requirements

The skip-data operation must be invokable by the Main Image Processor at any
point during a download. The BDX spec defines the skip byte count as a 64-bit
field; the session layer must honour that width to avoid overflow for large
images. See §14.1 for the full requirements.

### 5.7 Retry policy for incomplete cycles

When a download session finishes with one or more `kNotReady` entries, the
affected nodes were not updated this cycle. Without intervention the device will
next attempt an update only when the periodic `QueryImage` timer fires — which
can be many minutes away. If `softwareVersion` is confirmed prematurely it may
never fire at all (see §11.1).

To recover faster, the Main Image Processor can actively re-trigger `QueryImage`
after a platform-chosen delay, without waiting for the periodic timer. The retry
policy — when to retry, how many times, and with what backoff — is entirely the
platform's responsibility and is expressed via an overridable operation on the
Main Image Processor.

#### Retry hook

After any download session ends with at least one `kNotReady` entry, or after
`ConfirmCurrentImage()` withholds confirmation, the Main Image Processor invokes
the `OnPendingNodeUpdates` operation, passing two arguments:

- **Pending image IDs** — the list of image IDs whose sub-processors returned
  `kNotReady` this cycle. `kAlreadyUpToDate` entries are not included; they are
  already verified and do not drive retries.
- **Attempt count** — the number of times this operation has been invoked for
  the current `softwareVersion`. The Main Image Processor increments this counter
  and persists it across reboots so that post-rollback retries do not reset it.

The platform implementation decides what to do:

| Decision | Behaviour |
|---|---|
| Re-trigger immediately | Issue a new `QueryImage` before returning. |
| Re-trigger after a fixed delay | Schedule a `QueryImage` to fire after a platform-chosen interval. |
| Exponential backoff | Increase the delay geometrically with attempt count; e.g. `30s × 2^attemptCount`, capped at a maximum. |
| Retry up to N times then stop | Compare attempt count against a platform-defined limit; stop scheduling and raise an alert when exceeded. |
| Do nothing | Return without action — normal periodic `QueryImage` takes over. |

The default behaviour is to do nothing. Platforms that require fast convergence
for connected nodes must override this operation.

#### When the hook is called

| Event | Hook called? | Notes |
|---|---|---|
| Download completes; all entries `kReady` or `kAlreadyUpToDate` | No | Cycle was complete — no retry needed. |
| Download completes; one or more entries `kNotReady` | **Yes** — before the apply step schedules a reboot | Platform can schedule a re-trigger for after the reboot. |
| `ConfirmCurrentImage()` withholds confirmation (post-reboot) | **Yes** — before the rollback reboot | Re-trigger fires after rollback brings old `softwareVersion` back. |

Calling the hook before the rollback reboot ensures the scheduled re-trigger
fires on the next boot, immediately re-opening the OTA window rather than
waiting for the periodic timer.

#### Limits and failure cases

- **Permanent unavailability.** If a node is decommissioned or has a hardware
  fault, `IsReadyForOTA()` will always return `kNotReady`. The platform must
  implement a max-retry limit and an alerting or exclusion mechanism for this
  case; otherwise retries continue indefinitely. The attempt count is the natural
  signal for detecting this.
- **Node recovers between cycles.** If `IsReadyForOTA()` returned `kNotReady`
  but the node recovers before the retry fires, the next full OTA cycle will
  pick it up cleanly — no special handling needed.
- **No impact on the BDX session in progress.** The retry hook runs after the
  current BDX session has fully ended. It never interrupts or restarts a live
  download.

---

## 6. Threading Model

```
BDX layer (any thread)
   │
   ├─ Main Image Processor (block received)
   │      │
   │      ├─ copy block to internal buffer
   │      └─ schedule work on Matter thread
   │                          ──────────────►
                                              Matter thread
                                                │
                                                ├─ strip outer Matter OTA header (once)
                                                ├─ Phase 1: accumulate MultiImageHeader
                                                ├─ Phase 2: Dispatcher routes blocks
                                                │     ├─ SubImageProcessor.Write()
                                                │     ├─ SkipData() if not ready
                                                │     └─ request next block from BDX
                                                │
                                                └─ Finalize / Apply / Abort run here too
```

All Dispatcher logic and sub-processor `IsReadyForOTA()` / `Write()` calls
execute on the **Matter thread**. Sub-processor implementations therefore do not
need their own synchronization for this path. They must not perform long
blocking operations on the Matter thread — any slow I/O (bus writes, DMA waits)
should be dispatched to a worker task. See §9.3.

---

## 7. Atomicity & Failure Modes

### 7.1 Commit boundary

The only commit point the framework controls is the platform-level step inside
`Apply()` that makes the newly downloaded primary firmware active on the next
boot. Until that step executes, a reboot (power loss, crash) leaves the device
on its old firmware — no partial state is visible to the bootloader.

Sub-processors receive bytes during the download via `Write()`. Whatever
platform-specific action they take with those bytes (e.g., staging to a
secondary partition, buffering for a bus transfer) is the application's
responsibility.

> **Component firmware rollback is out of scope for this framework.** The
> Multi-Image Processor and the platform layer are responsible only for
> rolling back the **primary ESP32 application firmware** (via the ESP-IDF
> OTA rollback mechanism). Rolling back a co-processor, radio module, or any
> other component after it has been flashed is entirely the **application's
> responsibility**. If a sub-processor's write operation must be reversible,
> the application must implement that rollback logic and invoke it from its
> own `Abort()` handling — the framework will not do it.

### 7.2 Binary ordering in the OTA file

The order of binaries in the OTA file determines the order in which
sub-processors receive bytes. If the application's write logic for one binary
must complete before another begins (e.g., a co-processor must be flashed before
the host firmware is staged), the packaging tool must produce the file in that
order. See §3.5.

### 7.3 Failure-mode matrix

| Where it fails                                                 | What survives                                                  | Recovery                                                                                    |
| -------------------------------------------------------------- | -------------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| Mid-download (network, power)                                  | Old firmware in active slot; new data partially written        | Next OTA cycle overwrites cleanly from byte 0.                                              |
| `Finalize()` returns error                                     | Old firmware; sub-processor writes may be partially done       | `Abort()` is called on the Main Image Processor; application cleans up sub-processor state. |
| `Apply()` fails                                                | Old firmware on primary; sub-processor writes may be partially done | Component rollback is the application's responsibility (out of scope for this framework). Next OTA cycle re-flashes as needed. |
| `Apply()` fails; sub-processor write was irreversible (e.g. co-processor already flashed over bus) | Old primary firmware; external component already on new firmware | Split-version state. Boot-time consistency check (§9.6) detects mismatch and re-triggers update or marks device unhealthy. |
| Power loss after `Apply()` commits, before reboot              | New firmware will be booted next                               | Normal supported state — bootloader sees the committed slot.                                |
| New firmware crashes on first boot                             | Old firmware                                                   | Platform rollback restores previous slot before `ConfirmCurrentImage()` is called.          |
| New firmware boots but `ConfirmCurrentImage()` is never called | Old firmware on next reboot                                    | Platform rolls back. Application must confirm.                                              |

### 7.4 What "atomic" means here

Truly atomic across the primary firmware and any external component
(co-processor, peripheral) is impossible without a shared transaction log. The
framework provides **best-effort atomicity**:

-   Within the primary firmware slot: real atomicity at the platform commit
    step.
-   Across primary + external components: depends on application ordering logic
    and boot-time reconciliation (see §9.6).

---

## 8. Responsibilities

### 8.1 This framework provides

-   The **Main Image Processor** implementing the standard OTA image processor
    interface — the single entry point the Matter OTA Requestor calls.
-   The **Dispatcher** — header parsing (`MultiImageHeader` +
    `SubImageHeader[]`), the `imageId → sub-processor` map, byte-level routing,
    and `SkipData()` for absent or not-ready entries.
-   The **Sub Image Processor interface** — the contract (`IsReadyForOTA()` +
    write method) that applications implement per image ID.
-   The **`MultiImageHeader` / `SubImageHeader` binary format** and the
    packaging tool that produces `.ota` files conforming to it.
-   A default sub-processor for the primary application firmware slot,
    registered automatically at startup for the platform-defined application
    image ID.
-   This document.

### 8.2 Platform layer provides

-   The mechanism to write the primary application firmware to its designated
    storage location and make it the active boot target on `Apply()`.
-   Boot validation: detecting a first run of new firmware, confirming it, and
    rolling back if it does not confirm.
-   The platform-level `Abort()` implementation that cleans up any partially
    written primary firmware.

### 8.3 Application provides

For a primary-firmware-only setup: **nothing beyond enabling multi-image OTA**.
The default sub-processor and packaging manifest cover it.

For a multi-component product, the application provides:

1. **One Sub Image Processor implementation per additional binary.** Implements
   `IsReadyForOTA()` and the write method for that binary's destination
   (partition, bus, NVS, etc.). See §9.
2. **Registration** — registers each sub-processor under its chosen image ID
   before the OTA session begins.
3. **Packaging integration** — CI calls the packaging tool with a manifest that
   lists all components and their image IDs.
4. **A boot-time consistency check** for any external component (co-processor,
   peripheral). On boot, query the component's installed version and re-trigger
   update if it does not match the expected version for the running firmware.
   See §9.6.
5. **A versioning policy** — define when the outer `softwareVersion` is bumped
   (recommended: bump on any component change).

### 8.4 OTA Provider / build tooling provides

-   A reachable Matter OTA Provider that serves the `.ota` file.
-   The packaging tool invoked as part of release CI.
-   Operational practice: never publish a bundle with a lower `softwareVersion`
    than the currently deployed one.

---

## 9. Adding a Sub Image Processor

This section describes the steps for adding support for an additional binary
(e.g. a co-processor, a peripheral, a secondary partition) to the multi-image
OTA flow.

### 9.1 Pick an image ID

Pick **any `uint8_t` value in the range 128–255** for a manufacturer-defined
binary. There is no list to add to and no central authority.

Document the chosen value in the product's release notes — it is a stable ABI
contract between the device firmware and the packaging tool. Do not reuse a
value across unrelated binaries in the same product, and do not repurpose a
value across firmware releases.

### 9.2 Implement the Sub Image Processor interface

The Sub Image Processor interface has three methods:

**`IsReadyForOTA(uint32_t targetVersion)` → `OTAReadiness`** — called once
before bytes start. The Dispatcher passes `entry.version` from the
`SubImageHeader` as `targetVersion`. The sub-processor reads its currently
installed version, compares it against `targetVersion`, and returns one of:

| Node state | Return value | Effect |
|---|---|---|
| Component reachable, installed version ≠ `targetVersion` | `kReady` | Dispatcher calls `Init()` then `Write()`. |
| Component installed version == `targetVersion` | `kAlreadyUpToDate` | Dispatcher skips. Counts as verified at confirmation — no update needed. |
| Component busy (initializing, running a critical task) | `kNotReady` | Dispatcher skips. Blocks `softwareVersion` confirmation this cycle. |
| Component unreachable or not responding | `kNotReady` | Dispatcher skips. Blocks confirmation. Application should log. |
| Component in error state requiring manual recovery | `kNotReady` | Dispatcher skips. Blocks confirmation. Update would be unsafe. |

Rules:
- Must return in **milliseconds**. No blocking I/O. Any slow initialization
  (bus negotiation, boot-mode handshake) must happen before the OTA session
  begins.
- The return value is permanent for this OTA cycle. There is no retry within
  the session.
- The sub-processor must log the specific reason so that skips are observable
  and diagnosable.
- Default returns `kReady`. Override whenever readiness depends on runtime state.

**`Init(const SubImageHeader & entry)`** — called once immediately before the
first `Write()`, only when `IsReadyForOTA()` returned `kReady`. The sub-processor
must store at minimum `entry.length` to self-track progress. It should also
record `entry.version` for cross-verification and store `entry.sha256` to verify
integrity on the last chunk.

**Write method** — called per chunk. The Dispatcher guarantees:
- Chunks arrive sequentially from byte 0 of the binary's data.
- Exactly `entry.length` bytes are delivered in total across all calls.
- Chunks contain raw binary bytes only — no headers or framing.

The sub-processor detects its last chunk by comparing its running byte total
against `entry.length` (set in `Init()`). SHA-256 verification, commit, and
bus-transfer completion must happen on the last chunk. What the application does
with the bytes is entirely its own responsibility.

### 9.3 Non-blocking writes

The Dispatcher and Matter thread cannot stall waiting for slow bus
acknowledgments. If the write operation (UART, SPI, I²C, etc.) takes more than a
few milliseconds per chunk:

1. Copy the chunk to a local buffer or DMA descriptor.
2. Return from the write method immediately.
3. Schedule the actual bus transfer to a worker task.
4. When the transfer completes, signal the Main Image Processor to resume
   fetching the next block from BDX.

This pattern applies to any slow transport and keeps the Matter thread
responsive.

### 9.4 Register the processor

Register the sub-processor instance with the Dispatcher before the OTA session
starts, associating it with the chosen image ID. Registration is typically done
at application initialization.

Registration only builds the `imageId → sub-processor` map. It does **not**
determine processing order. Processing order is determined entirely by the order
of `SubImageHeader[]` entries in the OTA file, which is set at packaging time
(§9.5). A processor registered first can have an image ID that appears last in
the file.

Each image ID may have at most one registered processor. Registering a duplicate
image ID is an error.

### 9.5 Wire up packaging

Add the binary and its image ID to the product's release manifest and ensure the
CI build passes both to the packaging tool. The order of entries in the manifest
determines the order of binaries in the OTA file, which determines the delivery
order. Place binaries in the order that matches your application's write
dependencies.

### 9.6 Boot-time consistency check

For any binary that targets an external component (co-processor, peripheral),
implement a boot-time check before the application initializes Matter:

1. Query the component's currently installed version.
2. Compare it against the version expected by the running firmware.
3. If they do not match, either re-trigger the update from a known-good source
   (e.g., a blob bundled in the primary firmware) or mark the device as
   unhealthy and withhold Matter advertising until the inconsistency is
   resolved.

This is the recovery path for the case where the primary firmware was committed
but the external component's bytes were skipped or its write failed silently.

---

## 10. Build Configuration

### 10.1 Build flag

Multi-image OTA is disabled by default. A single build flag enables the entire
subsystem:

```
chip_enable_multi_ota_requestor = true
```

When disabled, the build uses the standard single-image processor. The two paths
are mutually exclusive — at most one OTA image processor implementation can be
linked.

### 10.2 Platform requirements

The target platform must have:

-   At least one alternate firmware slot so the new firmware can be written
    without overwriting the currently running image.
-   Boot validation support: the ability to mark a new firmware as confirmed
    after a successful first boot, and to roll back to the previous firmware if
    confirmation does not arrive.

Specific platform configuration (partition tables, bootloader flags, etc.) is
documented alongside the platform integration code.

---

## 11. Versioning Strategy

There are three distinct version numbers. Don't conflate them:

| Number                              | Where it lives                                | Who reads it                                   | Semantics                                                                                                         |
| ----------------------------------- | --------------------------------------------- | ---------------------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| Matter `softwareVersion`            | Outer OTA header & `BasicInformation` cluster | OTA Requestor / Provider                       | "Is there a newer bundle?" — represents the state where every component in the bundle is at its expected version. |
| Per-binary `SubImageHeader.version` | `SubImageHeader` entries                      | Sub-processors, boot-time check                | Expected installed version of each individual component for this bundle.                                          |
| Component-reported version          | Read from the component at runtime            | Boot-time consistency check, `IsReadyForOTA()` | Ground truth of what is actually installed on that component right now.                                           |

### 11.1 `softwareVersion` confirmation policy

This is the most critical rule in the multi-image versioning strategy.

**The `softwareVersion` must not be confirmed until every component described in
the bundle is verified to be at its expected version.**

The `softwareVersion` is not just a "host firmware version" — it represents the
state of the entire bundle. Confirming it signals to the OTA Requestor and
Provider that the device is fully at this version. Once confirmed, the Requestor
will not initiate another download cycle for the same version.

**Consequence of premature confirmation:** If the host firmware is updated and
`softwareVersion` is confirmed, but one or more connected nodes were skipped,
those nodes are permanently stuck on their old firmware with no mechanism to
receive the missed update through the normal OTA path. The Provider sees the
device at the latest version and will not offer the bundle again.

**The correct policy:**

At `ConfirmCurrentImage()` time, before marking the new `softwareVersion` as
confirmed, the application must verify that every registered sub-processor's
component is at its expected version (`SubImageHeader.version` for its image
ID). Only if all components match should confirmation proceed.

| Component verification result                                      | Action                                                                                                                                                                                       |
| ------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| All components at expected version                                 | Confirm `softwareVersion`. **Erase NVS readiness map and `attemptCount`.** OTA cycle is complete.                                                                                            |
| One or more components not at expected version                     | Do **not** confirm. Invoke the retry hook (§5.7). Platform rollback restores the previous primary firmware. Device reverts to old `softwareVersion`. Retry re-opens the OTA window.          |
| Component was skipped because it was already at the target version | Counts as verified — no update was needed.                                                                                                                                                   |

**Implication for `IsReadyForOTA()`:** A sub-processor returning `kNotReady`
means that component will not be updated this cycle and **blocks**
`softwareVersion` confirmation. A sub-processor returning `kAlreadyUpToDate`
means the component is already at the target version — it is skipped but counts
as verified, so confirmation can proceed. The `OTAReadiness` enum encodes this
distinction directly; the confirmation logic needs no additional state
inspection. See §9.2.

**Recovering without waiting for the periodic timer:** When confirmation is
withheld, the Main Image Processor invokes `OnPendingNodeUpdates` (§5.7) before
the rollback reboot. The platform implementation can schedule a re-trigger so
the device re-queries the Provider as soon as it comes back up on the old
firmware, without waiting for the next periodic `QueryImage` timeout.

### 11.2 Recommended discipline

-   The outer `softwareVersion` strictly increases on every release and is
    bumped whenever any component in the bundle changes.
-   The per-component `SubImageHeader.version` is the authoritative expected
    version for that component in this bundle. Use it as the target in the
    boot-time verification check.
-   The component-reported runtime version is the ground truth. The boot-time
    check compares runtime versions against the expected table for the running
    bundle, not against the previous bundle.
-   Never release a bundle where any component's `SubImageHeader.version` is
    lower than what is already deployed — this would cause sub-processors
    reporting "already at target version" to incorrectly pass verification
    against a downgraded expectation.

---

## 12. Testing

### 12.1 Unit tests

Feed synthesized byte streams into the Dispatcher and verify routing behaviour
using stub sub-processors. Tests should cover:

-   `MultiImageHeader` parsing: valid magic/version, invalid magic rejected,
    `numImages` = 0 handled gracefully.
-   Boundary cases: block ends exactly at a binary boundary.
-   Block split across two binaries: last bytes of binary A and first bytes of
    binary B arrive in the same block.
-   `IsReadyForOTA()` returning `kNotReady`: verify `SkipData()` is called with
    `entry.length`, `Init()` and `Write()` are never called, and the entry is
    recorded as `kNotReady` in the readiness map.
-   `IsReadyForOTA()` returning `kAlreadyUpToDate`: verify `SkipData()` is
    called with `entry.length`, `Init()` and `Write()` are never called, and the
    entry is recorded as `kAlreadyUpToDate` (counts as verified).
-   No registered processor for an image ID: same skip behaviour as `kNotReady`.
-   `Init()` receives the correct `SubImageHeader` fields before the first
    `Write()` call.
-   SHA-256 digest mismatch on the last chunk: verify the sub-processor rejects
    the update and `Finalize()` returns an error.
-   `Apply()` failure: verify the device does not reboot and stays on the old
    firmware.

**Confirmation policy tests** — these are the most critical class:

-   All sub-processors return `kReady` and all writes succeed →
    `ConfirmCurrentImage()` succeeds and `softwareVersion` is confirmed.
-   All sub-processors return `kAlreadyUpToDate` →
    `ConfirmCurrentImage()` succeeds (every entry counts as verified).
-   One sub-processor returns `kNotReady`, all others `kReady` →
    `ConfirmCurrentImage()` returns error; platform rollback is triggered;
    `softwareVersion` is **not** confirmed.
-   Mix of `kReady` + `kAlreadyUpToDate` with no `kNotReady` →
    `ConfirmCurrentImage()` succeeds.
-   `kNotReady` entry recorded during download; on the next boot the same entry
    is now resolvable → verify confirmation re-evaluates the saved readiness map
    (persisted across reboot) and succeeds only when all entries are verified.

**Retry policy tests:**

-   Download ends with one `kNotReady` entry → verify `OnPendingNodeUpdates()`
    is called with that image ID in `pendingImageIds` and `attemptCount = 1`.
-   `OnPendingNodeUpdates` schedules a re-trigger; verify the re-trigger fires
    after the configured delay and a new BDX session begins.
-   `attemptCount` increments correctly across rollback reboots; verify NVS
    persistence by simulating power-cycle between attempts.
-   Platform sets `kMaxRetries = 3`; verify `OnPendingNodeUpdates()` stops
    scheduling retries after the third call and raises an alert instead.
-   `kAlreadyUpToDate` entry is **not** included in `pendingImageIds`; verify
    it does not trigger a retry.

### 12.2 Integration tests

A hardware- or emulator-based test that:

1. Builds a primary firmware image and a stub secondary image.
2. Packages them into a two-component `.ota` bundle.
3. Serves the bundle via a Matter OTA Provider.
4. Runs the device under test, confirms it downloads, applies, reboots, confirms
   the new firmware, and reports the updated version.

### 12.3 Failure injection

Inject failures at: mid-write (return error from the write method),
`Finalize()`, `Apply()`. For each case, verify the device remains on the
previously running firmware after the failure.

---

## 13. Open Questions / Future Work

1. **Resume on disconnect.** BDX retries restart from byte 0; there is no
   per-binary progress checkpoint. If the device reboots mid-download it
   re-downloads from the beginning. Acceptable for most products.

---

## 14. Implementation Requirements

This section describes the requirements that an OTA session layer and platform
layer must satisfy to implement the design described in this document. No
specific programming language, API, or SDK is mandated — platforms choose their
own implementation style.

### 14.1 OTA Session Layer Requirements

#### R1 — Skip-data byte count must be 64-bit

The BDX specification defines `BlockQueryWithSkip.BytesToSkip` as a 64-bit
unsigned integer. The OTA session layer's skip-data operation must accept a
64-bit byte count. A 32-bit parameter overflows for any component image larger
than ~4 GB. Any existing implementation that uses a 32-bit count must widen it
before the Dispatcher can invoke it reliably.

#### R2 — Skip-data must be invokable by the Main Image Processor

The Dispatcher calls skip-data during active byte-stream routing whenever a
sub-processor returns `kNotReady` or `kAlreadyUpToDate`. The session layer must
expose the skip-data operation so the Main Image Processor can call it directly
at any point during a download, not only at block boundaries driven by the
session layer itself.

### 14.2 Platform Layer Requirements

#### R3 — Sub Image Processor interface: three operations

The Sub Image Processor interface must expose three operations:

1. **Readiness query** (`IsReadyForOTA(uint32_t targetVersion)`) — returns
   `OTAReadiness`. Called once before any bytes are delivered, with
   `entry.version` as `targetVersion`. The sub-processor uses this to decide
   whether the component is already at the target version (`kAlreadyUpToDate`),
   needs updating (`kReady`), or cannot be updated this cycle (`kNotReady`).
   Must return in milliseconds.
2. **Initialisation** (`Init`) — called once with the full `SubImageHeader`
   immediately before the first write, and only when the readiness query
   returned `kReady`. The sub-processor must store the binary's byte length
   to self-track progress and detect the last chunk, and must store the
   `sha256` field to verify integrity when the last chunk is received.
3. **Write** (`Write`) — called per chunk with raw binary bytes. The
   Dispatcher guarantees exactly `length` bytes total across all calls, in
   order, with no headers or framing.

The default readiness is `kReady`. Implementations whose component is always
present and always ready need only implement the write operation.

#### R4 — Dispatcher routing behaviour

For each `SubImageHeader` entry the Dispatcher must:

1. Look up the registered sub-processor by `imageId`. Treat a missing
   registration as `kNotReady`.
2. Call the readiness query and record the result. The result is persisted for
   use at `ConfirmCurrentImage()` time.
3. If the result is `kNotReady` or `kAlreadyUpToDate`: invoke skip-data for
   `entry.length` bytes and advance to the next entry.
4. If the result is `kReady`: call the initialisation operation with the full
   `SubImageHeader`, then feed chunks via the write operation until
   `entry.length` bytes have been delivered. The Dispatcher owns the byte
   counter and signals completion — the sub-processor does not.

#### R5 — Retry hook: `OnPendingNodeUpdates`

The Main Image Processor must expose an overridable `OnPendingNodeUpdates`
operation that platforms use to implement a retry policy. The operation receives:

- The list of image IDs whose sub-processors returned `kNotReady` this cycle.
- The attempt count — number of times this operation has been called for the
  current `softwareVersion`, persisted across reboots.

The default behaviour is to do nothing (fall back to the periodic `QueryImage`
timer). A platform override may schedule an immediate or delayed re-trigger,
implement exponential backoff, enforce a max-retry limit, or raise an alert for
persistent failures. The calling convention and scheduling mechanism are left to
the platform.

### 14.3 Compatibility Notes

#### N1 — Single-image processor storage erasure

An existing single-image processor that erases its storage destination at the
start of a download session is not affected by the multi-image design. Each sub-
processor manages its own storage independently; no sub-processor is aware of
what other sub-processors do.

#### N2 — Single-image processor header expectations

An existing single-image processor that expects the outer Matter OTA header at
the start of the byte stream is not compatible with the sub-processor `Write`
contract. The Main Image Processor strips the outer header before any bytes
reach a sub-processor. Sub-processors must not be written to re-parse it.

---

### 14.4 Pros and Cons of the SkipData Approach

#### Pros

| #   | Benefit                                                                                                                                                           |
| --- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| P1  | **Single BDX session.** No multi-session orchestration, no changes to the `DefaultOTARequestor` state machine.                                                    |
| P2  | **Spec-compliant.** `BlockQueryWithSkip` (0x15) is a standard BDX message. Any compliant Provider handles it.                                                     |
| P3  | **RAM stays constant.** All sub-processors share one block buffer. Registered sub-processors are just pointers in a map; no extra RAM per component.              |
| P4  | **Independent retry semantics.** If `IsReadyForOTA()` returns `kNotReady` today, the next OTA cycle offers another attempt with no persistent state to manage.    |
| P5  | **Minimal session layer changes.** Only two requirements on the OTA session layer: skip-data must be invokable by the Main Image Processor, and its byte count must be 64-bit (§14.1). |
| P6  | **Backward compatible.** Default `IsReadyForOTA()` returns `kReady`, so existing single-image processors are unaffected.                                          |

#### Cons

| #   | Limitation                                                                                                                                                                                                                                                |
| --- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| C1  | **Skip = no update this cycle.** `IsReadyForOTA()` is a yes/no decision before bytes start flowing. There is no "wait and retry." If a component needs 10 seconds to enter update mode it misses this OTA cycle. Pre-session synchronization is required. |
| C2  | **Processing order locked to file layout.** Components are processed in the order they appear in the OTA file. The packaging tool must produce the file in the order that matches the application's write dependencies.                                   |
| C3  | **No mid-session pause.** The Dispatcher cannot pause the BDX session and resume later. Provider idle timeout terminates the session if `BlockQuery` or `BlockQueryWithSkip` is not sent promptly.                                                        |
| C4  | **Mixed-version state on skip.** A device where one component was skipped runs a new version of some components alongside old versions of others. The application must detect and handle this at boot time (see §9.6).                                    |
| C5  | **Provider must support `BlockQueryWithSkip`.** Most compliant Matter OTA Providers handle it, but a minimal or non-compliant Provider may not. Verify Provider compliance before relying on skip.                                                        |
