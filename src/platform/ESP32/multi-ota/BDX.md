# BDX — Bulk Data Exchange Protocol

> A plain-English walkthrough of Matter's BDX protocol with ASCII flow
> charts, grounded in the actual `connectedhomeip` implementation under
> `src/protocols/bdx/`.
>
> Companion docs in this directory:
> - `README.md` — ESP32 multi-image OTA design.
> - `OTA_FLOW.txt` — end-to-end Matter OTA flow.

---

## 1. What is BDX, in one paragraph

Matter clusters carry small, structured commands and attribute reads.
That works for "turn light on" or "set color temperature." It does **not**
work for "send me a 1.5 MB firmware image." BDX is the protocol that
fills that gap: it lets one Matter node stream a **file** to another
Matter node over the same secure session that the cluster commands ride
on.

BDX is used today for two things in `connectedhomeip`:

1. **OTA software updates** — Provider sends `.ota` files to Requestor.
2. **Diagnostic log retrieval** — devices upload log dumps on demand.

There is no separate transport. No HTTP, no FTP, no separate port. BDX
runs over whatever Matter messaging is in use (TCP, UDP+MRP, BLE, …) as
long as it's reliable.

---

## 2. The four roles (and why there are four)

Every BDX session has **two parties**, but each party has **two
properties**, so there are four role names floating around. Don't let
them confuse you — it's a 2×2 matrix.

```
                    Direction of BYTES
                    -------------------
                            |
           +----------------+----------------+
           |                                  |
       SENDER                              RECEIVER
   (has the file)                     (will store the file)


                  Who STARTED the session?
                  ------------------------
                            |
           +----------------+----------------+
           |                                  |
       INITIATOR                          RESPONDER
   (sent the first message)         (replied to the first message)
```

- **Sender / Receiver** — direction of bytes. Always fixed for a session.
- **Initiator / Responder** — who started talking first. Fixed for a session.

So a session is one of two shapes:

| Shape | Initiator role | Responder role | First message |
|---|---|---|---|
| **Upload** | Sender | Receiver | `SendInit` |
| **Download** | Receiver | Sender | `ReceiveInit` |

Matter OTA is always a **download** — the device (Receiver) initiates,
the Provider (Sender) responds. So you'll see `ReceiveInit` in OTA
traces.

There's also a **third** axis (only for synchronous mode):

```
                  Who PACES the transfer?
                  -----------------------
                            |
           +----------------+----------------+
           |                                  |
        DRIVER                            FOLLOWER
   (sends BlockQuery /                  (responds to whatever
    Block to advance                     Driver sends; can never
    the transfer; can be                 be a sleepy device)
    a sleepy device)
```

- The **Driver** controls the rate of transfer. It can be either the
  Sender or the Receiver — that's negotiated at session start.
- The **Follower** waits and reacts.

Why "Driver"? Because it lets a battery-powered device control the pace.
A solar-powered sensor can wake up, ask for one block, ack it, sleep for
an hour, wake up, ask for the next, etc. The peer that's plugged in just
follows.

---

## 3. Synchronous vs Asynchronous mode

```
+------------------------------------+----------------------------------+
|         SYNCHRONOUS                |          ASYNCHRONOUS            |
+------------------------------------+----------------------------------+
| Each Block is acked before the     | Sender just streams bytes.       |
| next one is sent (or queried).     | No BlockQuery, no BlockAck.      |
|                                    |                                  |
| Application controls flow.         | Underlying transport (e.g. TCP)  |
|                                    | controls flow.                   |
|                                    |                                  |
| Works with sleepy devices.         | Provisional (per spec). Today    |
|                                    | only used over Matter-over-TCP.  |
|                                    |                                  |
| Two sub-modes:                     | Single mode:                     |
|   - Sender drive   (sender pushes) |   Sender just sends Block,       |
|   - Receiver drive (receiver pulls)|   Block, ..., BlockEOF, gets one |
|                                    |   final BlockAckEOF.             |
+------------------------------------+----------------------------------+
```

OTA today on most platforms uses **Sender Drive** synchronous mode: the
Provider sends a `Block` and waits for a `BlockAck` from the Requestor
before sending the next one. Simple, reliable, doesn't depend on TCP.

---

## 4. The message catalog

There are 11 BDX messages. Each has a fixed numeric opcode. Don't try to
memorize them — they only ever appear in three groups.

```
GROUP A: Negotiation (2 messages)
+-----+----------------------+
| 0x01 | SendInit             |  Initiator says "I want to upload."
| 0x04 | ReceiveInit          |  Initiator says "I want to download."
+-----+----------------------+

GROUP B: Acceptance (2 messages)
+-----+----------------------+
| 0x02 | SendAccept           |  Responder says "OK, upload accepted."
| 0x05 | ReceiveAccept        |  Responder says "OK, download accepted."
+-----+----------------------+

GROUP C: Transfer (5 messages)
+-----+----------------------+
| 0x10 | BlockQuery           |  Driving Receiver: "give me block N"
| 0x15 | BlockQueryWithSkip   |  Driving Receiver: "skip M bytes, then give me block N"
| 0x11 | Block                |  Sender:           "here's block N"
| 0x12 | BlockEOF             |  Sender:           "here's the LAST block"
| 0x13 | BlockAck             |  Receiver:         "got block N"
| 0x14 | BlockAckEOF          |  Receiver:         "got LAST block; transfer done"
+-----+----------------------+
```

Each one is a tiny binary frame — typically the message ID plus a 4-byte
block counter and the data. No HTTP-style headers.

---

## 5. Anatomy of `SendInit` / `ReceiveInit`

The first message (whichever flavor it is) carries everything the
Responder needs to decide whether to accept the transfer.

```
+--------------------------------------------------------------+
|                  SendInit / ReceiveInit                      |
+--------------------------------------------------------------+
| PTC (Proposed Transfer Control)        1 byte                |
|   bit 0..3 : version (4-bit; current = 0)                   |
|   bit 4    : SENDER_DRIVE supported                         |
|   bit 5    : RECEIVER_DRIVE supported                       |
|   bit 6    : ASYNC supported                                |
+--------------------------------------------------------------+
| RC (Range Control)                     1 byte                |
|   bit 0    : DEFLEN     (definite length present?)           |
|   bit 1    : STARTOFS   (start offset present?)              |
|   bit 4    : WIDERANGE  (offsets/lengths are 64-bit?)        |
+--------------------------------------------------------------+
| PMBS (Proposed Max Block Size)         2 bytes               |
|   "I support blocks up to N bytes."                          |
+--------------------------------------------------------------+
| STARTOFS  (optional)                   4 or 8 bytes          |
|   "Start sending from byte X" (for resume).                  |
+--------------------------------------------------------------+
| LEN       (optional)                   4 or 8 bytes          |
|   For SendInit: "I will send N bytes total."                 |
|   For ReceiveInit: "I want at most N bytes."                 |
+--------------------------------------------------------------+
| FDL  (File Designator Length)          2 bytes               |
+--------------------------------------------------------------+
| FD   (File Designator)                 FDL bytes             |
|   The "file name" — opaque string both sides agree on.       |
|   For OTA, this is the URI/token returned in QueryImageResp. |
+--------------------------------------------------------------+
| MDATA  (optional Metadata)             rest of message       |
|   TLV-encoded application metadata.                          |
+--------------------------------------------------------------+
```

> The `PTC` field can offer **multiple** drive modes — `SENDER_DRIVE | RECEIVER_DRIVE`
> means "I support either, you pick." The Responder picks one in its
> `SendAccept` / `ReceiveAccept`.

---

## 6. Anatomy of `SendAccept` / `ReceiveAccept`

The Responder commits to one set of parameters. Only one drive mode bit
is set; only one version is named; the negotiated max block size is ≤
the proposed one.

```
+--------------------------------------------------------------+
|                  SendAccept / ReceiveAccept                  |
+--------------------------------------------------------------+
| TC (Transfer Control)                  1 byte                |
|   Like PTC but exactly ONE drive bit set, version chosen.    |
+--------------------------------------------------------------+
| RC  (only in ReceiveAccept)            1 byte                |
+--------------------------------------------------------------+
| MBS (Max Block Size, agreed)           2 bytes               |
+--------------------------------------------------------------+
| LEN (optional, ReceiveAccept only)     4 or 8 bytes          |
|   Sender's authoritative file size.                          |
+--------------------------------------------------------------+
| MDATA (optional)                       rest of message       |
+--------------------------------------------------------------+
```

After this message, both sides know the exact transfer parameters.
Bytes can start flowing.

---

## 7. End-to-end: SENDER DRIVE (synchronous)

This is the OTA case. Provider drives, Requestor follows.

```
   Provider                                    Requestor
   (Sender / Initiator / Driver)               (Receiver / Responder / Follower)
   |                                           |
   |  SendInit(SENDER_DRIVE, version=1,        |
   |           maxBlockSize=1024,              |
   |           length=1500000, fileDesignator) |
   |  ---------------------------------------->|
   |                                           |
   |                                           |  Verify file designator,
   |                                           |  vendor ID, etc.
   |                                           |
   |                  SendAccept(SENDER_DRIVE, |
   |                  version=1, mbs=1024,     |
   |                  no MDATA)                |
   |  <----------------------------------------|
   |                                           |
   |  Block(blockCounter=0, data[1024])        |
   |  ---------------------------------------->|
   |                                  ProcessBlock(data); persist
   |                  BlockAck(blockCounter=0) |
   |  <----------------------------------------|
   |                                           |
   |  Block(blockCounter=1, data[1024])        |
   |  ---------------------------------------->|
   |                  BlockAck(blockCounter=1) |
   |  <----------------------------------------|
   |                                           |
   |             ... loop until last block ... |
   |                                           |
   |  BlockEOF(blockCounter=N, data[partial])  |
   |  ---------------------------------------->|
   |                                  ProcessBlock; Finalize()
   |               BlockAckEOF(blockCounter=N) |
   |  <----------------------------------------|
   |                                           |
   |               Session ends.                |
```

Properties:
- Provider sets the pace. Requestor's only job is to receive and ack.
- Each ack tells the Provider "you can free that block now."
- A missing ack within timeout triggers session abort.

---

## 8. End-to-end: RECEIVER DRIVE (synchronous)

Use this when the Receiver is sleepy / battery-powered. The Receiver
asks for one block at a time using `BlockQuery`. The Sender just waits.

```
   Receiver (sleepy)                            Sender
   (Initiator / Driver)                         (Responder / Follower)
   |                                            |
   |  ReceiveInit(RECEIVER_DRIVE,               |
   |              version=1, mbs=512,           |
   |              fileDesignator)               |
   |  ----------------------------------------->|
   |                                            |
   |               ReceiveAccept(RECEIVER_DRIVE,|
   |               mbs=512, length=1500000)     |
   |  <-----------------------------------------|
   |                                            |
   |  BlockQuery(blockCounter=0)                |
   |  ----------------------------------------->|
   |               Block(blockCounter=0, data)  |
   |  <-----------------------------------------|
   |                                            |
   |  ... Receiver may now sleep ...            |
   |                                            |
   |  BlockQuery(blockCounter=1)                |
   |  -----------------------------------------> ←  (BlockQuery(K+1) implicitly
   |               Block(blockCounter=1, data)  |    ACKs Block(K))
   |  <-----------------------------------------|
   |                                            |
   |             ... loop ...                   |
   |                                            |
   |  BlockQuery(blockCounter=N)                |
   |  ----------------------------------------->|
   |          BlockEOF(blockCounter=N, data)    |
   |  <-----------------------------------------|
   |                                            |
   |  BlockAckEOF(blockCounter=N)               |
   |  ----------------------------------------->|
   |                                            |
```

Key trick: a `BlockQuery(K+1)` is an **implicit ack** of `Block(K)`. The
Receiver may also send an explicit `BlockAck(K)` if it wants to tell the
Sender "you can free that block, I'll go to sleep now and ask for K+1
later."

---

## 9. End-to-end: ASYNCHRONOUS

Used over Matter-over-TCP today. No flow-control messages — the Sender
just streams.

```
   Sender (any)                                 Receiver
   |                                            |
   |  SendInit(ASYNC | SENDER_DRIVE,            |
   |           mbs=8192)                        |
   |  ----------------------------------------->|
   |               SendAccept(ASYNC, mbs=4096)  |
   |  <-----------------------------------------|
   |                                            |
   |  Block(0, data[4096])                      |
   |  ----------------------------------------->|
   |  Block(1, data[4096])                      |
   |  ----------------------------------------->|
   |  Block(2, data[4096])                      |
   |  ----------------------------------------->|
   |          ... no acks at all ...            |
   |  BlockEOF(N, data[800])                    |
   |  ----------------------------------------->|
   |               BlockAckEOF(N)               |
   |  <-----------------------------------------|
   |                                            |
```

Only the **final** message gets an ack — that's how the Sender knows
the file made it. Block size can be larger than the link MTU because TCP
handles fragmentation.

---

## 10. Resume — `BlockQueryWithSkip`

Suppose the Receiver got blocks 0-4, then had a power blip. It can
restart the same session and skip ahead:

```
   Receiver (Driver)                            Sender (Follower)
   |                                            |
   |  ReceiveInit(...)                          |
   |  ----------------------------------------->|
   |               ReceiveAccept(...)           |
   |  <-----------------------------------------|
   |                                            |
   |  BlockQuery(blockCounter=0)                |
   |  ----------------------------------------->|
   |               Block(0, data[256])          |
   |  <-----------------------------------------|
   |                                            |
   |  BlockQuery(blockCounter=1)                |
   |  ----------------------------------------->|
   |               Block(1, data[256])          |
   |  <-----------------------------------------|
   |                                            |
   |  BlockQueryWithSkip(blockCounter=2,        |
   |                     bytesToSkip=2921)      |
   |  -----------------------------------------> ← Sender advances cursor
   |                                            |   2921 bytes forward
   |               Block(2, data[256])          |
   |  <-----------------------------------------|   (those 256 bytes are
   |                                            |    AFTER the skip)
```

A `BlockQueryWithSkip` is just like a `BlockQuery` plus "advance the
file cursor by N bytes first."

There's a separate, simpler way to resume: open a fresh session with a
non-zero `STARTOFS` in the `ReceiveInit`. The skip variant is for
mid-session jumps (e.g., the higher layer just realized it already has
those bytes).

---

## 11. Block ordering rules (the strict part)

> The spec is **very picky** about this. Get it wrong and the peer aborts.

| Direction | Rule |
|---|---|
| `BlockQuery` counters | Must be ascending and **strictly +1** from the previous query. |
| `Block` / `BlockEOF` counters | Must be ascending and **strictly +1** from the previous Block. |
| `BlockAck` / `BlockAckEOF` counters | Must equal the counter of the Block being acknowledged. |
| Counter wraparound | Counters are 32-bit. `0xFFFF_FFFF + 1 = 0x0000_0000` is legal. |

If a peer ever receives a counter that violates this, it sends:
```
StatusReport(FAILURE, BDX, BAD_BLOCK_COUNTER)
```
and the session is dead.

> **Common pitfall:** the data length of a `Block` does **not** have to
> equal `MaxBlockSize`. Don't compute the file offset from
> `counter * mbs`. Each Block can carry up to MBS bytes, but possibly
> less. Track offset by summing actual bytes, not by counter.

---

## 12. Status codes (when things go wrong)

If anything goes wrong, the peer sends a `StatusReport` with the BDX
protocol id and a specific code:

| Code | Meaning | Typical cause |
|---|---|---|
| `0x0012` LENGTH_TOO_LARGE | Initiator wants to send N bytes, Responder can't store that much. | Tiny device, big file. |
| `0x0013` LENGTH_TOO_SHORT | Proposed length is shorter than Responder expects. | Mismatch in catalog. |
| `0x0014` LENGTH_MISMATCH | At `BlockEOF`, total bytes seen ≠ negotiated length. | Sender truncated. |
| `0x0015` LENGTH_REQUIRED | Responder needs a known length up front. | Used for some streaming sinks. |
| `0x0016` BAD_MESSAGE_CONTENTS | A frame is malformed. | Bug or corruption. |
| `0x0017` BAD_BLOCK_COUNTER | Counter out of order (see §11). | Bug or replay attack. |
| `0x0018` UNEXPECTED_MESSAGE | Got a `Block` before `SendAccept`, etc. | State machine error. |
| `0x0019` RESPONDER_BUSY | "Try again in ≥60 seconds." | Provider has too many active OTAs. |
| `0x001F` TRANSFER_FAILED_UNKNOWN_ERROR | I/O failure. | Disk full, flash dead. |
| `0x0050` TRANSFER_METHOD_NOT_SUPPORTED | Drive mode mismatch. | Initiator-only async, Responder no-async. |
| `0x0051` FILE_DESIGNATOR_UNKNOWN | "Never heard of that file." | Stale token. |
| `0x0052` START_OFFSET_NOT_SUPPORTED | Resume request rejected. | Sender can only stream from byte 0. |
| `0x0053` VERSION_NOT_SUPPORTED | No common protocol version. | Very old peer. |
| `0x005F` UNKNOWN | Catch-all internal error. | — |

A `StatusReport` from either side ends the session immediately.

---

## 13. Where this lives in `connectedhomeip`

```
src/protocols/bdx/
├── BdxMessages.{h,cpp}            Message structs + parse/encode for the 11 frames.
├── BdxTransferSession.{h,cpp}     The state machine. Drives one BDX session
│                                  through Init → Accept → blocks → EOF → done.
├── TransferFacilitator.{h,cpp}    Glue between Matter Exchange and the session;
│                                  routes incoming exchange messages into
│                                  TransferSession events. Synchronous variant.
├── AsyncTransferFacilitator.{h,cpp}  Same idea but for async transfers.
├── BdxTransferServer.{h,cpp}      Server-side helper for nodes that act as a
│                                  Sender (e.g. Provider apps).
├── BdxTransferProxy*.{h,cpp}      Thin abstraction layer the diagnostic-log
│                                  cluster uses to feed bytes into BDX.
├── BdxTransferDiagnosticLog*.{h,cpp}  Diagnostic-log integration.
├── BdxUri.{h,cpp}                 Parses / formats the "matter://" file
│                                  designator URI used by OTA.
├── StatusCode.{h,cpp}             The status-code enum + helpers.
└── DiagnosticLogs.h               Shared header for the diagnostic-log feature.
```

Key class to know: **`TransferSession`** in `BdxTransferSession.{h,cpp}`.
Everything else is a wrapper around it. It owns the actual state machine
and parses/emits frames.

---

## 14. How OTA uses BDX (concrete tie-in)

The Matter OTA Requestor side wraps BDX with a higher-level
`OTADownloader` interface (`src/app/clusters/ota-requestor/`). The
concrete implementation is `BDXDownloader.{h,cpp}`.

Looking at one OTA download in BDX terms:

```
1. Application calls
        OTARequestor → QueryImage (cluster command, NOT BDX)
   Provider responds with QueryImageResponse containing a fileDesignator
   like "bdx://<provider-node-id>/<file-token>".

2. Requestor's BDXDownloader::BeginPrepareDownload extracts the URI,
   opens a Matter exchange to the Provider, and calls
        TransferSession::StartTransfer(...) with role = Receiver, drive = Sender.
   This sends a ReceiveInit on the wire.

3. Provider replies with ReceiveAccept. Requestor's TransferSession
   surfaces an event "kAcceptReceived" up to BDXDownloader, which calls
        OTAImageProcessorInterface::PrepareDownload().

4. Provider sends Block messages. TransferSession parses each, surfaces
   "kBlockReceived" events up. BDXDownloader hands the data to
        OTAImageProcessorInterface::ProcessBlock(block).
   ImageProcessor sends BlockAck via TransferSession::PrepareBlockAck.

5. Eventually Provider sends BlockEOF. ImageProcessor::Finalize. The
   final BlockAckEOF goes out. Session ends.

6. ApplyUpdateRequest / NotifyUpdateApplied happen at the Provider
   cluster level — NOT BDX.
```

**Important:** BDX itself does not know what's in the bytes. It doesn't
parse the Matter OTA header, doesn't verify the digest, doesn't route to
multi-image processors. All of that happens **above** BDX in the image
processor. BDX just delivers the bytes faithfully and in order.

---

## 15. Mental model — one diagram to remember

```
   Application layer              Cluster layer       Wire / transport

  +---------------------+        +-------------+
  | OTAImageProcessor   |        | OTA cluster |  (commands: QueryImage,
  | (your code)         |<----+  | server      |   ApplyUpdate, NotifyApplied)
  +---------------------+     |  +-------------+
            ^                 |
            | ProcessBlock    |
            |                 |
  +---------------------+     |
  | BDXDownloader       |     |
  | (cluster wrapper)   |     |
  +---------------------+     |
            ^                 |
            | TransferSession |
            | events          |
  +---------------------+     |    +---------------+
  | TransferSession     |     +--> | Matter        |---->  network
  | (BDX state machine) |          | Exchange      |
  +---------------------+          +---------------+
            ^
            | reads / writes BDX frames
            |
  +---------------------+
  | BdxMessages         |
  | (frame parse/encode)|
  +---------------------+
```

Three boxes worth remembering:

- **`TransferSession`** — the actual BDX brain. One per active transfer.
- **`BDXDownloader`** — the Requestor-side OTA glue. Talks to
  `TransferSession` and to `OTAImageProcessorInterface`.
- **`OTAImageProcessorInterface`** — your platform's flash writer.

---

## 16. Common gotchas / things to double-check

1. **Don't compute file offset from block counter.** Use accumulated
   data lengths. A block can carry less than `MaxBlockSize`.
2. **`BlockEOF` may be empty.** If the entire file fits in the
   negotiated block size, the only data-bearing message can be a single
   `BlockEOF` with `blockCounter=0`. Handle the zero-block case.
3. **`BlockAckEOF` is mandatory** in both sync and async modes. The
   Sender depends on it to know the file made it; without it, the
   Sender can't be sure.
4. **Counters wrap modulo 2^32.** Don't break on `0xFFFF_FFFF → 0`.
5. **One BDX session per Matter exchange.** You can't multiplex two
   files on the same exchange.
6. **Reliability is the transport's job.** BDX assumes the message it
   sent was actually delivered. If you're on UDP, MRP must be enabled
   (the reliable flag in `ExchangeFlags`). On TCP, that's automatic.
7. **The session is encrypted.** PASE or CASE only — no unsecured BDX.
8. **`fileDesignator` is application-specific.** For OTA it's the URI
   from `QueryImageResponse`. For diagnostic logs it's a different
   format. Both ends must agree on its meaning.

---

## 17. Quick reference card

```
NEGOTIATION (always synchronous)
  Initiator → Responder : SendInit  (upload)   |  ReceiveInit  (download)
  Responder → Initiator : SendAccept           |  ReceiveAccept

TRANSFER (sender drive, sync)
  Sender   → Receiver : Block / Block / ... / BlockEOF
  Receiver → Sender   : BlockAck / BlockAck / ... / BlockAckEOF

TRANSFER (receiver drive, sync)
  Receiver → Sender   : BlockQuery(0)
  Sender   → Receiver : Block(0)
  Receiver → Sender   : BlockQuery(1)             ← implicit ack of Block(0)
  Sender   → Receiver : Block(1)
  ...
  Receiver → Sender   : BlockQuery(N)
  Sender   → Receiver : BlockEOF(N)
  Receiver → Sender   : BlockAckEOF(N)

TRANSFER (async)
  Sender   → Receiver : Block, Block, ..., BlockEOF
  Receiver → Sender   :                              BlockAckEOF

RESUME (mid-session)
  Receiver → Sender   : BlockQueryWithSkip(K, bytesToSkip)

ERRORS
  Either   →          : StatusReport(FAILURE, BDX, <code>)  → session ends
```

---

## 18. References

- Matter Core Specification — Bulk Data Exchange Protocol section.
- `src/protocols/bdx/` — implementation in this repo.
- `src/app/clusters/ota-requestor/BDXDownloader.{h,cpp}` — example
  consumer of the BDX layer.
- RFC 1350 — TFTP, the spiritual predecessor BDX borrows semantics from.
