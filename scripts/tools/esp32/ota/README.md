# ESP32 Matter OTA — quick reference

Commands for building OTA images (full and **delta**) and driving an update with
`chip-tool`, for the ESP32 multi-image OTA framework.

All commands below are run from this directory
(`scripts/tools/esp32/ota`). `REPO` refers to the connectedhomeip root
(`../../../../` from here). Replace `esp32c3` with your target and the node-ids /
VID / PID with your values.

---

## 0. Tools in this directory

| File | Purpose |
|------|---------|
| `esp_delta_ota_patch_gen.py` | Generate / verify a delta patch (bsdiff + heatshrink). |
| `esp32_multi_ota_tool.py`    | Wrap one or more binaries into a Matter multi-image `.ota`. |
| `images.csv`                 | Example manifest for a **full** app image. |
| `images_patch.csv`           | Example manifest for a **delta** app image. |

The manifest CSV columns are `id,version,path`. The application image **must** be
present with `id = 1`.

---

## 1. Build the base and new firmware

Build with `CONFIG_ENABLE_OTA_REQUESTOR=y`, `CONFIG_ENABLE_MULTI_IMAGE_OTA=y`,
`CONFIG_ENABLE_DELTA_OTA=y` (for delta) and the software version set via
`CONFIG_APP_PROJECT_VER_FROM_CONFIG=y` + `CONFIG_DEVICE_SOFTWARE_VERSION_NUMBER`.

```bash
# v1 (base) — version 1. Flash this, commission it, and KEEP this exact binary.
idf.py fullclean && idf.py build
cp build/chip-lighting-app.bin scripts/tools/esp32/ota/chip-lighting-app-v1.bin

# v2 (new) — bump CONFIG_DEVICE_SOFTWARE_VERSION_NUMBER=2, then:
idf.py fullclean && idf.py build
cp build/chip-lighting-app.bin scripts/tools/esp32/ota/chip-lighting-app-v2.bin
```

> The delta base must be **byte-identical** to the image currently running on the
> device — keep the exact `v1.bin` you flashed; do not rebuild it.

---

## 2a. Create a delta patch

```bash
python esp_delta_ota_patch_gen.py create_patch \
    --chip esp32c3 \
    --base_binary chip-lighting-app-v1.bin \
    --new_binary  chip-lighting-app-v2.bin \
    --patch_file_name chip-lighting-app-patch.bin
```

This prepends a 64-byte CHIP patch header (`magic 0xfccdde10` + the SHA-256 of the
base image + reserved) and self-verifies by applying the patch. The patch is
typically several times smaller than the full image.

Verify an existing patch without flashing:

```bash
python esp_delta_ota_patch_gen.py verify_patch \
    --base_binary chip-lighting-app-v1.bin \
    --patch_file_name chip-lighting-app-patch.bin \
    --new_binary chip-lighting-app-v2.bin
```

---

## 2b. Wrap into a Matter multi-image `.ota`

The multi-image processor expects the multi-image container, so use
`create-multi` with a manifest (works for both full and delta — the device tells
them apart automatically).

```bash
# Full image  (images.csv  -> id,version,path = 1,2,chip-lighting-app-v2.bin)
./esp32_multi_ota_tool.py create-multi \
    -v 0xFFF1 -p 0x8000 -vn 2 -vs "2.0" \
    images.csv chip-test-image.ota

# Delta patch (images_patch.csv -> 1,2,chip-lighting-app-patch.bin)
./esp32_multi_ota_tool.py create-multi \
    -v 0xFFF1 -p 0x8000 -vn 2 -vs "2.0" \
    images_patch.csv chip-test-patch-image.ota
```

`-vn` (software version) **must** equal the compiled
`CONFIG_DEVICE_SOFTWARE_VERSION_NUMBER` of the new image, or the requestor fails
to confirm after reboot.

Inspect any image:

```bash
./esp32_multi_ota_tool.py show chip-test-patch-image.ota
```

---

## 2c. Encrypted OTA (alternative / additional to delta)

`esp_encrypted_img` is hybrid RSA+AES. The device holds an **RSA-3072 private key**
(embedded in the firmware) and decrypts each chunk. The build **auto-encrypts** the
app with the matching **public key** — you do not run the encrypt tool manually for
the full image.

Both key files must live in the project dir (`examples/lighting-app/esp32/`):
`esp_image_encryption_key.pem` (private, embedded to decrypt) and
`esp_image_encryption_public_key.pem` (public, used at build time to encrypt).

```bash
# 1. Generate an RSA-3072 keypair into the project dir (once)
cd ../../../../examples/lighting-app/esp32
openssl genrsa -out esp_image_encryption_key.pem 3072
openssl rsa -in esp_image_encryption_key.pem -pubout -out esp_image_encryption_public_key.pem

# 2. Build with CONFIG_ENABLE_ENCRYPTED_OTA=y. This embeds the private key AND produces
#    build/chip-lighting-app-encrypted.bin (the encrypted app image, via create_esp_enc_img).
#    Build v1 + flash first (device gets the private key, runs v1), then build v2 (version 2).
idf.py build

# 3. Package the encrypted image as a multi-image OTA (SHA in the header is over the
#    encrypted bytes — which is what the dispatcher verifies). Run from scripts/tools/esp32/ota:
cp build/chip-lighting-app-encrypted.bin <repo>/scripts/tools/esp32/ota/
printf 'id,version,path\n1,2,chip-lighting-app-encrypted.bin\n' > images_enc.csv
./esp32_multi_ota_tool.py create-multi -v 0xFFF1 -p 0x8000 -vn 2 -vs "2.0" \
    images_enc.csv chip-test-enc-image.ota
```

Serve and trigger via `chip-tool` exactly as for delta (sections 3–4).

- Both `v1` and `v2` builds must embed the **same private key**, or the running device
  can't decrypt the new image.
- **RSA-3072** specifically (the component default).
- The encrypted image is the **full** image (no size reduction — that is delta's job).
- **Encrypted + delta:** the build only auto-encrypts the full app, so encrypt the
  *patch* manually and build with both configs — the device decrypts each chunk, then
  feeds the plaintext to the patcher:
  ```bash
  python <repo>/examples/.../espressif__esp_encrypted_img/tools/esp_enc_img_gen.py \
      encrypt chip-lighting-app-patch.bin esp_image_encryption_public_key.pem patch-enc.bin
  ```

---

## 3. Serve the image with the OTA Provider

Build `chip-ota-provider-app` (Linux) once from `REPO`, then:

```bash
./chip-ota-provider-app \
    --discriminator 22 --secured-device-port 5565 \
    --KVS /tmp/provider.kvs \
    --filepath chip-test-patch-image.ota
```

---

## 4. chip-tool commands

Assume requestor (the ESP32) node-id `1`, provider node-id `2`.

```bash
# Commission the provider app (on the same fabric as the requestor)
chip-tool pairing onnetwork-long 2 20202021 22

# Grant the provider ACL so the requestor can read from it
chip-tool accesscontrol write acl '[
  {"fabricIndex":1,"privilege":5,"authMode":2,"subjects":[112233],"targets":null},
  {"fabricIndex":1,"privilege":3,"authMode":2,"subjects":null,"targets":null}
]' 2 0

# Tell the requestor where the provider is (triggers the update)
#   args: <provider-node-id> <vendor-id> <announcement-reason> <endpoint> <requestor-node-id> <endpoint>
chip-tool otasoftwareupdaterequestor announce-otaprovider 2 0 0 0 1 0
```

Optional — set the default provider on the requestor instead of announcing:

```bash
chip-tool otasoftwareupdaterequestor write default-otaproviders '[
  {"fabricIndex":1,"providerNodeID":2,"endpoint":0}
]' 1 0
```

Check progress / result:

```bash
# Update state: kIdle/kQuerying/kDownloading/kApplying ...
chip-tool otasoftwareupdaterequestor read update-state 1 0

# After reboot, confirm the new version
chip-tool basicinformation read software-version 1 0
```

---

## 5. Gotchas

1. **Base must match the running image.** The patch header carries the base's
   SHA-256; the device compares it to `esp_partition_get_sha256(running)`. A
   rebuilt or differently-configured base is rejected ("patch base does not
   match").
2. **`--chip` must match the target** — the reconstructed image's `chip_id` is
   verified.
3. **Version coordination** — `-vn` == compiled
   `CONFIG_DEVICE_SOFTWARE_VERSION_NUMBER`, and it must be greater than the
   running version (requires `CONFIG_APP_PROJECT_VER_FROM_CONFIG=y` and a
   `fullclean` rebuild).
4. **Two app partitions** (`ota_0`/`ota_1`) + `otadata` are required.
5. **detools / heatshrink** — the patch uses heatshrink compression; do not change
   the compression flags, the device's `esp_delta_ota` expects it. The patch is
   larger than a minimal diff because no architecture-aware transform is applied,
   so address shifts from a version bump inflate it — still much smaller than the
   full image.
