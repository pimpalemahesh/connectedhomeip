# Multi-Image OTA — host unit tests

Standalone host unit tests for the ESP32 multi-image OTA framework. They are
built with the repo's normal `gn`/`ninja` toolchain but are **not** part of any
CHIP aggregate test group (`//src:tests`, `//:check`) and never run in CI. They
are reachable only through an opt-in gn arg, so they don't touch the
connectedhomeip test suite.

## Build & run

```sh
gn gen out/host --args='chip_build_esp32_multi_ota_tests=true'
ninja -C out/host multi_ota_tests

./out/host/tests/TestMultiOTAImageHeader
./out/host/tests/TestEncryptedOTAHelper
./out/host/tests/TestAppImageProcessor
./out/host/tests/TestMultiImageOTAProcessor
```

To memory-check the encrypted-buffer ownership, add `is_asan=true` to the gn
args and rerun.

## What's covered

| Test binary | Source under test | Notes |
|---|---|---|
| `TestMultiOTAImageHeader` | `MultiOTAImageHeader.cpp` | Header parse incl. byte-by-byte accumulation across chunks; magic / numImages / reserved / per-entry validation; trailing-data span handling. No mocks. |
| `TestEncryptedOTAHelper` | `EncryptedOTAHelper.cpp` | Key init validation, decrypt session state machine, per-chunk transform, buffered (no-output) chunks, error propagation, end/abort teardown. `esp_encrypted_img` mocked. |
| `TestAppImageProcessor` | `AppImageProcessor.cpp` (base path) | `Init`/`Write`/`Finish`/`Apply`/`Abort` lifecycle, first-chunk `esp_ota_begin`, error surfacing, apply-before-write rejection, abort teardown. `esp_ota`/`esp_partition`/`esp_system` mocked. |
| `TestMultiImageOTAProcessor` | `MultiImageOTAProcessorImpl.cpp` | Synchronous public surface: `RegisterProcessor` (success / null / zero-tag / duplicate / distinct) and the default `ShouldApplyUpdate` policy. `<platform/ESP32/ESP32Utils.h>` shadowed by a host stub. |

## Mocks (`mocks/`)

Minimal host stand-ins for the ESP-IDF headers the production files include —
`esp_err.h`, `esp_encrypted_img.h`, `esp_ota_ops.h`, `esp_partition.h`,
`esp_system.h`, `esp_app_format.h`, `esp_delta_ota.h`, and a stub
`platform/ESP32/ESP32Utils.h`. Behavior is driven and inspected through the
`gEspEncMock` / `gEspOtaMock` control blocks.

## Not covered here (and why)

- **Dispatcher routing / `SkipData` arithmetic / SHA-256 verification per
  sub-image.** This runs on the Matter thread via `PlatformMgr().ScheduleWork`
  (`HandleProcessBlock`), so a host test needs an event-loop + mock-downloader
  harness. The four production paths (full / delta / encrypted / enc+delta) are
  validated on hardware; a host harness for the skip path can be added on top of
  these mocks if wanted.
- **`AppImageProcessor` delta and encrypted paths** (`CONFIG_ENABLE_DELTA_OTA` /
  `CONFIG_ENABLE_ENCRYPTED_OTA`). The `esp_delta_ota` mock (with a write-callback
  that replays reconstructed bytes) and the `EncryptedOTAHelper` mock are already
  in place to grow a delta/encrypted suite later.
