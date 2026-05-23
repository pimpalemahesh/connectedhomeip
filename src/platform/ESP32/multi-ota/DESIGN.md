# Multi-Image OTA — Design Specification

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
without any changes to the OTA Requestor, Provider protocol, or BDX layer.

---

## 2. Glossary

| Term                    | Definition                                                                                                                                          |
| ----------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Bundle**              | A single `.ota` file containing a `MultiImageHeader` (which embeds one `SubImageHeader` per component in its `subImages[]` field) followed by all component binaries concatenated. |
| **Main Image Processor**| The OTA image processor implementation the Matter OTA Requestor calls. Owns the full download lifecycle and contains the Dispatcher.                |
| **Dispatcher**          | The routing engine inside the Main Image Processor. Parses the bundle header, maintains the `imageId → sub-processor` map, and routes or skips each component's bytes. |
| **Sub Image Processor** | An application-provided handler for one component. Implements `Init(entry)`, `IsReadyForOTA()`, `Write()`, `Confirm()`, and `Abort(context)`.      |
| **Image ID**            | A 4-byte unsigned integer (uint32, `0x00000000`–`0xFFFFFFFF`) that identifies which sub-processor handles a given component. Stable ABI between firmware and packaging tool. |
| **`SkipData(n)`**       | BDX `BlockQueryWithSkip` — tells the Provider to advance its cursor by `n` bytes without delivering them.                                          |
| **Confirmation**        | The platform action that marks newly booted firmware as valid and cancels any pending rollback. Triggered by `ConfirmCurrentImage()`.               |
| **`softwareVersion`**   | The Matter bundle version in the outer OTA header. Represents the state where every component in the bundle is at its expected version.             |

---

## 3. OTA File Format

The Matter OTA file header (vendor id, product id, software version, payload
size, …) is unchanged. The **payload** now begins with a `MultiImageHeader` that
describes every binary inside the bundle — including an embedded `subImages[]`
array of `SubImageHeader` entries — followed by the binary data blobs at the
offsets the header specifies. The dispatcher parses the full `MultiImageHeader`
(fixed fields then `subImages[]`) first, then routes each binary's bytes to the
correct processor — using `SkipData()` for any binary whose processor is absent
(assumed up to date) or not ready.

### 3.1 Outer layout

```
+---------------------------------------------------------+
| Matter OTA header (chip-ota-image-tool format)          |
|   vendor_id, product_id, software_version, ...          |
+---------------------------------------------------------+  ← offset 0 of payload
| MultiImageHeader                                        |
|   magic(4)  num_images(1)  reserved(3)                  |
|   subImages[0]   : SubImageHeader  (48 bytes)           |
|   subImages[1]   : SubImageHeader  (48 bytes)           |
|   ...                                                   |
|   subImages[N-1] : SubImageHeader  (48 bytes)           |
+---------------------------------------------------------+
| Binary 0 data   (at subImages[0].offset bytes)          |
+---------------------------------------------------------+
| Binary 1 data   (at subImages[1].offset bytes)          |
+---------------------------------------------------------+
| ...                                                     |
+---------------------------------------------------------+
```

All `offset` values are relative to the start of the OTA payload (byte 0
immediately after the outer Matter OTA header ends). All multi-byte fields are
**little-endian**.

### 3.2 MultiImageHeader

`MultiImageHeader` is the single header structure at the start of the payload.
It contains a fixed-size preamble followed by a variable-length array of
`SubImageHeader` entries (`subImages[]`). All fields are little-endian:

**Fixed preamble (8 bytes):**

| Offset | Size | Field       | Semantics                                            |
| ------ | ---- | ----------- | ---------------------------------------------------- |
| 0      | 4    | `magic`     | Must equal `0x4D494F54` ("MIOT") — bundle identifier |
| 4      | 1    | `numImages` | Number of entries in `subImages[]` (0–255)           |
| 5      | 3    | `reserved`  | Must be zero; reserved for future extensions         |

**Variable-length field:**

| Offset | Size              | Field        | Semantics                                    |
| ------ | ----------------- | ------------ | -------------------------------------------- |
| 8      | `numImages × 48`  | `subImages[]`| Array of `SubImageHeader` entries; see §3.3  |

Total `MultiImageHeader` size on the wire:

```
sizeof(MultiImageHeader) = 8 + numImages * sizeof(SubImageHeader)
                         = 8 + numImages * 48
```

The dispatcher accumulates exactly `sizeof(MultiImageHeader)` bytes before it
switches from header-parsing mode to data-routing mode. Because `numImages` is
not known until the first 8 bytes are read, parsing proceeds in two steps:
1. Accumulate the 8-byte preamble; read `magic` and `numImages`.
2. Accumulate the remaining `numImages × 48` bytes to populate `subImages[]`.

### 3.3 SubImageHeader (element of `MultiImageHeader.subImages[]`)

Each entry in `MultiImageHeader.subImages[]` is a `SubImageHeader` — a fixed
48-byte structure regardless of the binary it describes. All fields are
little-endian:

| Offset | Size | Field     | Semantics                                                      |
| ------ | ---- | --------- | -------------------------------------------------------------- |
| 0      | 4    | `imageId` | Identifies which sub-processor handles this binary (§3.4)      |
| 4      | 4    | `version` | Expected installed version of this binary                      |
| 8      | 4    | `offset`  | Byte offset of binary data from payload start                  |
| 12     | 4    | `length`  | Exact byte count of the binary                                 |
| 16     | 32   | `sha256`  | SHA-256 digest of the binary — mandatory                      |

Field semantics:

| Field      | Notes                                                                                                                                                                                   |
| ---------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `imageId`  | Matched against registered processors at runtime. A bundle may carry `imageId`s not registered on a given device — those entries are skipped via `SkipData()` and assumed to be already up to date (do not block confirmation). |
| `version`  | Per-binary version number. Informational and for sanity checks only; the OTA Requestor's update decision is driven by the outer Matter `softwareVersion` alone.             |
| `offset`   | Absolute byte offset from payload start. Enables the dispatcher to jump directly to each binary with a single `SkipData()` call rather than streaming all preceding bytes. |
| `length`   | Exact byte count. Used both to know when to stop feeding the processor and to compute the skip distance for unneeded binaries.                                              |
| `sha256`  | Mandatory SHA-256 of the binary at `[offset, offset+length)`. The sub-processor must verify this digest before accepting the update as complete. |

### 3.4 Image ID space

`imageId` is a 4-byte unsigned integer (uint32, values `0x00000000`–`0xFFFFFFFF`).
The namespace is partitioned to avoid collisions:

| Range                        | Owner        | Use                                                                                                                                    |
| ---------------------------- | ------------ | -------------------------------------------------------------------------------------------------------------------------------------- |
| `0x00000000`                 | —            | Invalid. Rejected at parse time.                                                                                                       |
| `0x00000001 .. 0x000000FF`   | Platform     | Reserved for platform-defined well-known images. Specific assignments are an implementation detail and not fixed by this spec.         |
| `0x00000100 .. 0xFFFFFFFF`   | Manufacturer | **Manufacturer-defined.** Pick any value in this range; no central registry.                                                           |

The specific well-known IDs within range `0x01`–`0xFF` (e.g. which value means
"application firmware", etc.) are determined by the implementation and documented
alongside the processor registration code. This spec does not assign them.

Manufacturer-defined IDs in range `0x00000100`–`0xFFFFFFFF` carry no inherent
meaning beyond what the device firmware and packaging tool agree on. There is no
list to add to and no central authority.

The only runtime validation is **duplicate registration** —
`RegisterProcessor(imageId, ...)` fails if `imageId` is already registered. The
`imageId`-to-meaning mapping is a stable ABI contract between the device
firmware and the packaging tool. Repurposing an ID across firmware versions is
an ABI break for that product.

### 3.5 Packaging tool

A packaging tool takes a manifest that describes each binary — its image ID,
version, and file path — and produces a single `.ota` file. The tool:

1. Reads each binary and computes its SHA-256 digest.
2. Computes each binary's `offset` (header section size + sum of preceding
   binary sizes).
3. Builds the `MultiImageHeader` (preamble + `subImages[]`).
4. Concatenates the header blob and binaries, then wraps the result with the
   standard outer Matter OTA header (vendor ID, product ID, software version,
   digest).

Output: a single `.ota` file ready to serve from a Matter OTA Provider.
The manifest format, tool invocation, and outer header parameters are
implementation details documented in IMPLEMENTATION.md.

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
the `MultiImageHeader` (preamble then `subImages[]`) from the incoming byte
stream, maintains the `imageId → sub-processor` map populated by application
registration, and redirects each byte chunk to the sub-processor that matches
the current `subImages[i].imageId`. When no sub-processor is registered for an
entry, the Dispatcher treats it as `kAlreadyUpToDate` (assumed up to date) and
calls `SkipData()` to advance past it. When the matched sub-processor returns
not-ready, `SkipData()` is also called but the entry blocks confirmation.

**Sub Image Processor** — an interface the application registers per image ID.
Five methods form the contract:

-   `Init(entry)` — called once with the full `SubImageHeader` for each entry,
    before `IsReadyForOTA()`. Must be light-weight: store the fields needed for
    the readiness decision (`entry.version`) and for subsequent writes
    (`entry.length`, `entry.sha256`). Do not start heavy I/O here — defer that
    to the first `Write()` call so resources are only allocated when `kReady` is
    confirmed.
-   `IsReadyForOTA()` → `DeviceReadinessState` — called immediately after
    `Init()`, with no parameters. The sub-processor uses the version stored
    during `Init()` to decide: `kReady` (proceed), `kAlreadyUpToDate` (already
    at target version, skip but counts as verified), `kNotReady` (skip, blocks
    confirmation). Must return quickly — no blocking I/O.
-   `Write(block)` — called per chunk, only when `IsReadyForOTA()` returned
    `kReady`. On the first chunk the sub-processor performs heavy initialisation
    (e.g. `esp_ota_begin`). The sub-processor knows it has received the last
    chunk when its running byte total reaches `entry.length` (stored in
    `Init()`).
-   `Confirm()` — called on every sub-processor that had `Init()` called, during
    `ConfirmCurrentImage()` (§5.1). For `kReady` sub-processors: verify the
    component is now running `entry.version`. For `kNotReady` sub-processors:
    re-check whether the component is now at its expected version (the post-reboot
    attempt). Returns an error if the check fails; any failure withholds
    `softwareVersion` confirmation. `kAlreadyUpToDate` sub-processors may return
    success immediately.
-   `Abort(context)` — called on every sub-processor that had `Init()` called
    whenever the OTA session ends without completing normally — whether due to an
    error or a deliberate cancellation. `AbortContext` carries two fields:

    | Field    | Values                | Meaning                                                        |
    | -------- | --------------------- | -------------------------------------------------------------- |
    | `reason` | `kError`, `kCancelled`| `kError`: session ended due to a failure; `kCancelled`: deliberate abort by the application |
    | `error`  | platform error code   | The specific error that caused the abort; unset when `reason` is `kCancelled` |

    The sub-processor may use `context.reason` to log or handle the two cases
    differently. It must discard any partially written data and release all
    resources acquired since `Init()`. Must not block.

`DeviceReadinessState` is a three-value enum — not a boolean — because the
confirmation policy (§12.1) requires distinguishing "already up to date" from
"unavailable":

| Value              | Dispatcher action                      | Counts as verified?                             |
| ------------------ | -------------------------------------- | ----------------------------------------------- |
| `kReady`           | Call `Write()` per chunk               | Yes                                             |
| `kAlreadyUpToDate` | `SkipData(entry.length)`               | Yes — already at target version                 |
| `kNotReady`        | `SkipData(entry.length)`               | **No** — blocks `softwareVersion` confirmation  |

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
detecting and handling this (§7.4, §10.6). Do not skip a component unless the
device can tolerate running the old version of that component alongside new
versions of the others.

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
  |   records DeviceReadinessState per entry          |
  |   blocks confirmation if any = kNotReady          |
  |   +-------------------------------------------+  |
  |   |              Dispatcher                   |  |
  |   |   MultiImageHeader (incl. subImages[])    |  |
  |   |   imageId → sub-processor map             |  |
  |   |   routes chunks / calls SkipData()        |  |
  |   +--------+----------------------------------+  |
  +------------+-------------------------------------+
               |
               | Init(SubImageHeader) — always, light
               | IsReadyForOTA() → DeviceReadinessState
               | Write(block) per chunk — only when kReady
               | Confirm() — post-reboot per-entry check
               | Abort(AbortContext) — on error or cancel
               v
  +------------+-------------------------------------+
  |      Sub Image Processor (interface)             |
  |   Init(SubImageHeader)              (app-impl)  |
  |   IsReadyForOTA()                   (app-impl)  |
  |   Write(block)      → result        (app-impl)  |
  |   Confirm()         → result        (app-impl)  |
  |   Abort(AbortContext)               (app-impl)  |
  +--------------------------------------------------+
               ^                   ^
               |                   |
       [App firmware]     [Co-processor / any]
       (registered by     (registered by
        platform layer)    application)
```

**Main Image Processor** receives every BDX block. Before any data arrives it
triggers the Dispatcher to begin `MultiImageHeader` accumulation. Once the full
`MultiImageHeader` (preamble + `subImages[]`) is parsed the Dispatcher switches
to routing mode. It records the `DeviceReadinessState` result for each entry and uses it
in `ConfirmCurrentImage()` (§5.1, §12.1).

**Dispatcher** — for each entry in `multiImageHeader.subImages[]`: looks up the
sub-processor by `imageId`. If no processor is registered, treats it as
`kAlreadyUpToDate` (assumed up to date). Otherwise calls `Init(entry)` (light)
then `IsReadyForOTA()` (no params). If `kReady`, feeds chunks via `Write()`. If
`kAlreadyUpToDate` or `kNotReady`, calls `SkipData(entry.length)`. Advances when
`entry.length` bytes are accounted for.

**Sub Image Processor** — `Init()` is called first (light: stores entry fields).
`IsReadyForOTA()` uses the stored version to decide readiness. On the first
`Write()` the sub-processor performs heavy initialisation. It detects its last
chunk when its running byte count reaches `entry.length`.

**Registration** — applications call a register function before the OTA session
starts, associating an `imageId` with a sub-processor instance. Each `imageId`
may have at most one registered processor; duplicate registration is an error.

---

## 5. Confirmation and Retry

### 5.1 Boot validation and `softwareVersion` confirmation

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
   b. For each entry that had a registered processor with `Init()` called:
      - If the processor returned `kReady`: `Confirm()` is called to verify the
        update was actually applied.
      - If the processor returned `kNotReady`: `Confirm()` is called as a
        post-reboot re-check to see if the component is now at its expected
        version.
      - If the processor returned `kAlreadyUpToDate`: `Confirm()` is called;
        the processor should return success immediately since no update was
        needed.
      - If no processor was registered for an entry (recorded as
        `kAlreadyUpToDate` by the Dispatcher): no `Confirm()` call is made;
        the entry is counted as verified automatically.
      If any sub-processor's `Confirm()` returns an error, confirmation is
      withheld.
4. If either check fails, `ConfirmCurrentImage()` returns an error. Before
   triggering the rollback reboot, the Main Image Processor invokes the retry
   policy hook (§5.2) so the platform can schedule a `QueryImage` re-trigger
   for immediately after the rollback completes. The device reverts to the old
   `softwareVersion` and the OTA window reopens.

This extends the single-image confirmation flow with step 3b. The Main Image
Processor persists two things in NVS across the reboot (keyed by the
`softwareVersion` being applied):

- The per-entry `DeviceReadinessState` map, so that `ConfirmCurrentImage()`
  knows which processors to call `Confirm()` on and which to skip.
- The `SubImageHeader` for every entry whose registered processor had `Init()`
  called. On the next boot, before invoking `Confirm()`, the framework
  re-calls `Init(entry)` on each sub-processor using the persisted header.
  This restores `entry.version` (and `entry.sha256`, `entry.length`, etc.)
  that the sub-processor stored during the download. Sub-processor
  implementations must not assume that `entry` fields are available in
  `Confirm()` from the same power cycle as the download.

Once `ConfirmCurrentImage()` succeeds — meaning all components are verified
and the new `softwareVersion` is committed — the Main Image Processor **must
erase the NVS readiness map, the persisted `SubImageHeader` entries, and
`attemptCount`**. This ensures the next OTA cycle (for `softwareVersion + 1`
or any future bundle) starts with a clean slate and does not inherit stale
entries from the previous cycle.

### 5.2 Retry policy for incomplete cycles

When a download session finishes with one or more `kNotReady` entries, the
affected nodes were not updated this cycle. Without intervention the device will
next attempt an update only when the periodic `QueryImage` timer fires — which
can be many minutes away. If `softwareVersion` is confirmed prematurely it may
never fire at all (see §12.1).

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
  and persists it in NVS, keyed by `softwareVersion`, so that post-rollback
  retries do not reset it. When the Main Image Processor begins a new OTA session
  for a different `softwareVersion`, it erases all NVS entries from the previous
  version (readiness map, persisted headers, and attempt counter) before
  proceeding, ensuring no stale state carries over between bundles.

The platform implementation decides what to do:

| Decision                       | Behaviour                                                                                              |
| ------------------------------ | ------------------------------------------------------------------------------------------------------ |
| Re-trigger immediately         | Issue a new `QueryImage` before returning.                                                             |
| Re-trigger after a fixed delay | Schedule a `QueryImage` to fire after a platform-chosen interval.                                      |
| Exponential backoff            | Increase the delay geometrically with attempt count; e.g. `30s × 2^attemptCount`, capped at a maximum.|
| Retry up to N times then stop  | Compare attempt count against a platform-defined limit; stop scheduling and raise an alert when exceeded.|
| Do nothing                     | Return without action — normal periodic `QueryImage` takes over.                                       |

The default behaviour is to do nothing. Platforms that require fast convergence
for connected nodes must override this operation.

#### When the hook is called

| Event                                                           | Hook called?                                        | Notes                                                             |
| --------------------------------------------------------------- | --------------------------------------------------- | ----------------------------------------------------------------- |
| Download completes; all entries `kReady` or `kAlreadyUpToDate` | No                                                  | Cycle was complete — no retry needed.                             |
| Download completes; one or more entries `kNotReady`             | **Yes** — before the apply step schedules a reboot  | Platform can schedule a re-trigger for after the reboot.          |
| `ConfirmCurrentImage()` withholds confirmation (post-reboot)    | **Yes** — before the rollback reboot                | Re-trigger fires after rollback brings old `softwareVersion` back.|

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
                                                ├─ Phase 1: accumulate MultiImageHeader (preamble + subImages[])
                                                ├─ Phase 2: Dispatcher routes blocks
                                                │     ├─ SubImageProcessor.Write()
                                                │     ├─ SkipData() if not ready
                                                │     └─ request next block from BDX
                                                │
                                                └─ Finalize / Apply / Abort run here too
```

All Dispatcher logic and sub-processor `Init()` / `IsReadyForOTA()` / `Write()`
calls execute on the **Matter thread**. Sub-processor implementations therefore do not
need their own synchronization for this path. They must not perform long
blocking operations on the Matter thread — any slow I/O (bus writes, DMA waits)
should be dispatched to a worker task. See §10.3.

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
| `Apply()` fails; sub-processor write was irreversible (e.g. co-processor already flashed over bus) | Old primary firmware; external component already on new firmware | Split-version state. Boot-time consistency check (§10.6) detects mismatch and re-triggers update or marks device unhealthy. |
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
    and boot-time reconciliation (see §10.6).

---

## 8. Responsibilities

### 8.1 This framework provides

-   The **Main Image Processor** implementing the standard OTA image processor
    interface — the single entry point the Matter OTA Requestor calls.
-   The **Dispatcher** — header parsing (`MultiImageHeader` including its
    `subImages[]`), the `imageId → sub-processor` map, byte-level routing,
    and `SkipData()` for absent entries (assumed up to date) and not-ready entries.
-   The **Sub Image Processor interface** — the contract (`IsReadyForOTA()` +
    write method) that applications implement per image ID.
-   The **`MultiImageHeader` binary format** (preamble + embedded `SubImageHeader`
    array) and the packaging tool that produces `.ota` files conforming to it.
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
   (partition, bus, NVS, etc.). See §10.
2. **Registration** — registers each sub-processor under its chosen image ID
   before the OTA session begins.
3. **Packaging integration** — CI calls the packaging tool with a manifest that
   lists all components and their image IDs.
4. **A boot-time consistency check** for any external component (co-processor,
   peripheral). On boot, query the component's installed version and re-trigger
   update if it does not match the expected version for the running firmware.
   See §10.6.
5. **A versioning policy** — define when the outer `softwareVersion` is bumped
   (recommended: bump on any component change).

### 8.4 OTA Provider / build tooling provides

-   A reachable Matter OTA Provider that serves the `.ota` file.
-   The packaging tool invoked as part of release CI.
-   Operational practice: never publish a bundle with a lower `softwareVersion`
    than the currently deployed one.

---

## 9. Implementation Requirements

This section describes the requirements a platform implementation must satisfy.
No existing SDK code needs to change — the multi-image OTA processor is a new
`OTAImageProcessorInterface` implementation that uses the existing BDX
downloader and session layer as-is.

### 9.1 Platform Layer Requirements

#### R1 — Sub Image Processor interface: five operations

The Sub Image Processor interface must expose five operations:

1. **Initialisation** (`Init(entry)`) — called once with the full `SubImageHeader`
   for each entry, before the readiness query. Must be light-weight: store the
   fields needed for the readiness decision (`entry.version`) and for subsequent
   writes (`entry.length`, `entry.sha256`). Must not start heavy I/O (bus
   negotiation, partition erase, etc.) — defer that to the first `Write()`.
2. **Readiness query** (`IsReadyForOTA()`) — returns `DeviceReadinessState`.
   Called immediately after `Init()`, with no parameters. The sub-processor uses
   the version stored during `Init()` to decide: `kReady` (proceed),
   `kAlreadyUpToDate` (skip, counts as verified), `kNotReady` (skip, blocks
   confirmation). Must return in milliseconds.
3. **Write** (`Write`) — called per chunk, only when `IsReadyForOTA()` returned
   `kReady`. On the first chunk the sub-processor performs heavy initialisation
   using the stored entry fields. The Dispatcher guarantees exactly `length`
   bytes total across all calls, in order, with no headers or framing.
   SHA-256 verification is the sub-processor's responsibility: the framework
   delivers `entry.sha256` via `Init()` but does not compute or check the
   digest itself. The sub-processor must accumulate its own digest (e.g., using
   a SHA-256 library) and verify it matches `entry.sha256` when the last chunk
   arrives; if the digest does not match, `Write()` must return an error on
   that last chunk.
4. **Confirm** (`Confirm()`) — called during `ConfirmCurrentImage()` on every
   sub-processor that had `Init()` called. For `kReady` sub-processors: verify
   the component is now running `entry.version`. For `kNotReady` sub-processors:
   post-reboot re-check of component version. Returns an error if verification
   fails; any failure withholds `softwareVersion` confirmation.
5. **Abort** (`Abort(context)`) — called on every sub-processor that had `Init()`
   called whenever the session ends without completing normally. `context` is an
   `AbortContext` carrying the reason (error or cancellation). Must discard
   partial writes, release all resources acquired since `Init()`, and not block.

The default readiness is `kReady`. Implementations whose component is always
present and always ready need only implement `Write`, `Confirm`, and `Abort`;
the others have safe defaults.

#### R2 — Dispatcher routing behaviour

For each entry in `MultiImageHeader.subImages[]` the Dispatcher must:

1. Look up the registered sub-processor by `imageId`. Treat a missing
   registration as `kAlreadyUpToDate` — the component is assumed to be already
   at its expected version on this device.
2. If a processor is found: call `Init(entry)` (light), then call
   `IsReadyForOTA()` (no params) and record the result. If no processor is
   found: record `kAlreadyUpToDate`. The result is persisted for use at
   `ConfirmCurrentImage()` time.
3. If the result is `kNotReady` or `kAlreadyUpToDate`: invoke skip-data for
   `entry.length` bytes and advance to the next entry.
4. If the result is `kReady`: feed chunks via the write operation until
   `entry.length` bytes have been delivered. The Dispatcher owns the byte
   counter and signals completion — the sub-processor does not.

#### R3 — Retry hook: `OnPendingNodeUpdates`

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

### 9.2 Compatibility Notes

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

## 10. Adding a Sub Image Processor

This section describes the steps for adding support for an additional binary
(e.g. a co-processor, a peripheral, a secondary partition) to the multi-image
OTA flow.

### 10.1 Pick an image ID

Pick **any value in the range `0x00000100`–`0xFFFFFFFF`** for a manufacturer-defined
binary. There is no list to add to and no central authority.

Document the chosen value in the product's release notes — it is a stable ABI
contract between the device firmware and the packaging tool. Do not reuse a
value across unrelated binaries in the same product, and do not repurpose a
value across firmware releases.

### 10.2 Implement the Sub Image Processor interface

The Sub Image Processor interface has five methods:

**`Init(entry)`** — called once for each entry, before
`IsReadyForOTA()`. Must be light-weight: store `entry.version`, `entry.length`,
and `entry.sha256` for later use. Do not start heavy I/O here (bus negotiation,
partition erase, etc.) — defer that to the first `Write()` call.

**`IsReadyForOTA()` → `DeviceReadinessState`** — called immediately after
`Init()`, with no parameters. The sub-processor uses the version stored during
`Init()` to decide:

| Node state                                                          | Return value       | Effect                                                                   |
| ------------------------------------------------------------------- | ------------------ | ------------------------------------------------------------------------ |
| Component reachable, installed version ≠ stored `entry.version`    | `kReady`           | Dispatcher calls `Write()` per chunk.                                    |
| Component installed version == stored `entry.version`               | `kAlreadyUpToDate` | Dispatcher skips. Counts as verified at confirmation — no update needed. |
| Component busy (initializing, running a critical task)              | `kNotReady`        | Dispatcher skips. Blocks `softwareVersion` confirmation this cycle.      |
| Component unreachable or not responding                             | `kNotReady`        | Dispatcher skips. Blocks confirmation. Application should log.           |
| Component in error state requiring manual recovery                  | `kNotReady`        | Dispatcher skips. Blocks confirmation. Update would be unsafe.           |

Rules:
- Must return in **milliseconds**. No blocking I/O. Any slow initialization
  (bus negotiation, boot-mode handshake) must happen before the OTA session
  begins.
- The return value is permanent for this OTA cycle. There is no retry within
  the session.
- The sub-processor must log the specific reason so that skips are observable
  and diagnosable.
- Default returns `kReady`. Override whenever readiness depends on runtime state.

**Write method** — called per chunk. The Dispatcher guarantees:
- Chunks arrive sequentially from byte 0 of the binary's data.
- Exactly `entry.length` bytes are delivered in total across all calls.
- Chunks contain raw binary bytes only — no headers or framing.

The sub-processor detects its last chunk by comparing its running byte total
against `entry.length` (set in `Init()`). SHA-256 verification, commit, and
bus-transfer completion must happen on the last chunk. What the application does
with the bytes is entirely its own responsibility.

**`Confirm()`** — called during `ConfirmCurrentImage()` on every sub-processor
that had `Init()` called. The framework re-calls `Init(entry)` with the persisted
`SubImageHeader` before calling `Confirm()`, so `entry.version` and other fields
stored in `Init()` are available (§5.1).

| Sub-processor state at download time | Expected action in `Confirm()`                              |
| ------------------------------------- | ------------------------------------------------------------ |
| `kReady`                              | Verify the component is now running `entry.version`. Return error if not. |
| `kNotReady`                           | Re-check whether the component is now at `entry.version` (post-reboot attempt). Return error if not. |
| `kAlreadyUpToDate`                    | Return success immediately — no update was needed.           |

Any failure withholds `softwareVersion` confirmation and triggers a rollback
reboot. The retry hook (§5.2) is invoked before that reboot.

**`Abort(context)`** — called on every sub-processor that had `Init()` called
whenever the OTA session ends without completing normally. The `AbortContext`
struct (defined in §4.1) carries the reason (`kError` or `kCancelled`) and, for
errors, the platform error code. The sub-processor must:

- Discard any partially written data.
- Release all resources acquired since `Init()` (partition handles, bus locks,
  DMA descriptors, etc.).
- Not block.

If the write target is an external component (co-processor, peripheral) and the
write is not reversible (e.g., the co-processor was already flashed before
`Abort()` is called), the sub-processor must record the inconsistency. The
boot-time consistency check (§10.6) is the recovery path for this case.

### 10.3 Non-blocking writes

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

### 10.4 Register the processor

Register the sub-processor instance with the Dispatcher before the OTA session
starts, associating it with the chosen image ID. Registration is typically done
at application initialization.

Registration only builds the `imageId → sub-processor` map. It does **not**
determine processing order. Processing order is determined entirely by the order
of `MultiImageHeader.subImages[]` entries in the OTA file, which is set at packaging time
(§10.5). A processor registered first can have an image ID that appears last in
the file.

Each image ID may have at most one registered processor. Registering a duplicate
image ID is an error.

### 10.5 Wire up packaging

Add the binary and its image ID to the product's release manifest and ensure the
CI build passes both to the packaging tool. The order of entries in the manifest
determines the order of binaries in the OTA file, which determines the delivery
order. Place binaries in the order that matches your application's write
dependencies.

### 10.6 Boot-time consistency check

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

## 11. Build Configuration

### 11.1 Build flag

Multi-image OTA is disabled by default. A single build flag enables the entire
subsystem:

```
chip_enable_multi_ota_requestor = true
```

When disabled, the build uses the standard single-image processor. The two paths
are mutually exclusive — at most one OTA image processor implementation can be
linked.

### 11.2 Platform requirements

The target platform must have:

-   At least one alternate firmware slot so the new firmware can be written
    without overwriting the currently running image.
-   Boot validation support: the ability to mark a new firmware as confirmed
    after a successful first boot, and to roll back to the previous firmware if
    confirmation does not arrive.

Specific platform configuration (partition tables, bootloader flags, etc.) is
documented alongside the platform integration code.

---

## 12. Versioning

Three distinct version numbers exist in this system:

| Number                              | Where it lives                                | Who reads it                                   | Semantics                                                                                                         |
| ----------------------------------- | --------------------------------------------- | ---------------------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| Matter `softwareVersion`            | Outer OTA header & `BasicInformation` cluster | OTA Requestor / Provider                       | "Is there a newer bundle?" — represents the state where every component in the bundle is at its expected version. |
| Per-binary `SubImageHeader.version` | `MultiImageHeader.subImages[]` entries        | Sub-processors, boot-time check                | Expected installed version of each individual component for this bundle.                                          |
| Component-reported version          | Read from the component at runtime            | Boot-time consistency check, `IsReadyForOTA()` | Ground truth of what is actually installed on that component right now.                                           |

### 12.1 Discipline

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

## 13. Testing

### 13.1 Unit tests

Feed synthesized byte streams into the Dispatcher and verify routing behaviour
using stub sub-processors. Tests should cover:

-   `MultiImageHeader` parsing: valid magic/version, invalid magic rejected,
    `numImages` = 0 handled gracefully.
-   Boundary cases: block ends exactly at a binary boundary.
-   Block split across two binaries: last bytes of binary A and first bytes of
    binary B arrive in the same block.
-   `IsReadyForOTA()` returning `kNotReady`: verify `Init()` is called once,
    `SkipData()` is called with `entry.length`, `Write()` is never called, and
    the entry is recorded as `kNotReady` in the readiness map.
-   `IsReadyForOTA()` returning `kAlreadyUpToDate`: verify `Init()` is called
    once, `SkipData()` is called with `entry.length`, `Write()` is never called,
    and the entry is recorded as `kAlreadyUpToDate` (counts as verified).
-   No registered processor for an image ID: verify `SkipData()` is called with
    `entry.length`, the entry is recorded as `kAlreadyUpToDate` (not `kNotReady`),
    and confirmation is **not** blocked.
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

### 13.2 Integration tests

A hardware- or emulator-based test that:

1. Builds a primary firmware image and a stub secondary image.
2. Packages them into a two-component `.ota` bundle.
3. Serves the bundle via a Matter OTA Provider.
4. Runs the device under test, confirms it downloads, applies, reboots, confirms
   the new firmware, and reports the updated version.

### 13.3 Failure injection

Inject failures at: mid-write (return error from the write method),
`Finalize()`, `Apply()`. For each case, verify the device remains on the
previously running firmware after the failure.

---

## 14. Open Questions / Future Work

1. **Resume on disconnect.** BDX retries restart from byte 0; there is no
   per-binary progress checkpoint. If the device reboots mid-download it
   re-downloads from the beginning. Acceptable for most products.

2. **`numImages = 0` validity.** The format allows `numImages = 0`, producing an
   8-byte preamble with no sub-images. The spec currently accepts this (the
   Dispatcher would switch immediately to routing mode with nothing to route).
   Whether a zero-image bundle should be rejected at parse time or silently
   allowed is unresolved. A future revision should make this explicit.

3. **`numImages` upper bound.** No maximum is defined. A bundle with 255 entries
   would require 8 + 255 × 48 = 12,248 bytes of NVS storage for persisted
   headers alone. Devices with constrained NVS should define a platform-specific
   limit and reject bundles that exceed it.

4. **Delta / compressed binaries.** All binaries are assumed to be raw flat
   images. Delta encoding or compression would reduce transfer size but requires
   per-component decompression in `Write()`. Out of scope for this revision;
   nothing in the format prevents a future `imageId` namespace assignment for
   compressed variants.

5. **Provider compliance for `BlockQueryWithSkip`.** C5 in Appendix A notes that
   a non-compliant Provider may reject skip messages. There is currently no
   negotiation mechanism. A future extension could add a capability bit in the
   bundle header so that Providers can indicate skip support, allowing the
   Dispatcher to fall back to read-and-discard if skip is unsupported.

---

## Appendix A. Design Rationale — SkipData Approach

#### Pros

| #   | Benefit                                                                                                                                                           |
| --- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| P1  | **Single BDX session.** No multi-session orchestration, no changes to the `DefaultOTARequestor` state machine.                                                    |
| P2  | **Spec-compliant.** `BlockQueryWithSkip` (0x15) is a standard BDX message. Any compliant Provider handles it.                                                     |
| P3  | **RAM stays constant.** All sub-processors share one block buffer. Registered sub-processors are just pointers in a map; no extra RAM per component.              |
| P4  | **Independent retry semantics.** If `IsReadyForOTA()` returns `kNotReady` today, the next OTA cycle offers another attempt with no persistent state to manage.    |
| P5  | **No session layer changes.** The existing BDX downloader and session layer are used as-is. The multi-image processor is a new `OTAImageProcessorInterface` implementation only. |
| P6  | **Backward compatible.** Default `IsReadyForOTA()` returns `kReady`, so existing single-image processors are unaffected.                                          |

#### Cons

| #   | Limitation                                                                                                                                                                                                                                                |
| --- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| C1  | **Skip = no update this cycle.** `IsReadyForOTA()` is a yes/no decision before bytes start flowing. There is no "wait and retry." If a component needs 10 seconds to enter update mode it misses this OTA cycle. Pre-session synchronization is required. |
| C2  | **Processing order locked to file layout.** Components are processed in the order they appear in the OTA file. The packaging tool must produce the file in the order that matches the application's write dependencies.                                   |
| C3  | **No mid-session pause.** The Dispatcher cannot pause the BDX session and resume later. Provider idle timeout terminates the session if `BlockQuery` or `BlockQueryWithSkip` is not sent promptly.                                                        |
| C4  | **Mixed-version state on skip.** A device where one component was skipped runs a new version of some components alongside old versions of others. The application must detect and handle this at boot time (see §10.6).                                    |
| C5  | **Provider must support `BlockQueryWithSkip`.** Most compliant Matter OTA Providers handle it, but a minimal or non-compliant Provider may not. Verify Provider compliance before relying on skip.                                                        |
