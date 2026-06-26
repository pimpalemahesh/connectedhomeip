#!/usr/bin/env bash
#
# gen_multi_ota.sh — one-shot builder for the ESP32 multi-image (delta + encrypted) OTA.
#
# Runs the full repetitive pipeline and verifies at each stage:
#   1. Copy the fresh lighting-app build out of build/ into binarys/.
#   2. Create a delta patch (v1 base -> v2 new) and self-verify it.
#   3. Encrypt the patch with the public key.
#   4. Wrap everything into a Matter multi-image .ota from the CSV manifest.
#   5. Show the resulting .ota so you can eyeball the header/sub-images.
#
# All paths are relative to this script's directory (scripts/tools/esp32/ota).
# Edit the CONFIG block below if your VID/PID/version/target change.
#
# Usage:  ./gen_multi_ota.sh

set -euo pipefail

# ---------------------------------------------------------------------------
# CONFIG — change these as needed
# ---------------------------------------------------------------------------
CHIP="esp32c3"                 # delta patch target (must match the device)
VENDOR_ID="0xFFF1"
PRODUCT_ID="0x8001"
VERSION_NUM="2"                # must equal CONFIG_DEVICE_SOFTWARE_VERSION_NUMBER
VERSION_STR="v2"
DIGEST="sha256"

# Source build artifact (the freshly built lighting-app image)
BUILD_BIN="../../../../examples/lighting-app/esp32/build/chip-lighting-app.bin"

# Working files (all under binarys/ so the CSV's relative paths resolve)
BASE_BIN="binarys/chip-lighting-app-v1.bin"            # base / currently-running image
NEW_BIN="binarys/chip-lighting-app-v2.bin"            # new image to diff against
PATCH_BIN="binarys/chip-lighting-app-patch.bin"
PATCH_ENC_BIN="binarys/chip-lighting-app-patch-encrypted.bin"
PUBLIC_KEY="keys/public.pem"
MANIFEST="binarys/multi_images.bin"                   # CSV manifest (id,version,path)
OUT_OTA="chip-test-multi-images.ota"

# Where the fresh build artifact should be copied. Defaults to the base image
# (matches the documented flow); set to "$NEW_BIN" if you just built the new one.
COPY_DEST="$BASE_BIN"
# ---------------------------------------------------------------------------

cd "$(dirname "$0")"

# Pretty step logging
step()  { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }
run()   { printf '\033[2m$ %s\033[0m\n' "$*"; "$@"; }
fail()  { printf '\n\033[1;31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

# --- ensure the ESP-IDF python env (provides esptool) ----------------------
if ! python -c 'import esptool' 2>/dev/null; then
    if [ -n "${IDF_PATH:-}" ] && [ -f "$IDF_PATH/export.sh" ]; then
        step "Activating ESP-IDF environment ($IDF_PATH)"
        # shellcheck disable=SC1091
        . "$IDF_PATH/export.sh" >/dev/null
    fi
fi
python -c 'import esptool' 2>/dev/null || fail \
    "'esptool' not found. Activate ESP-IDF first:  . \$IDF_PATH/export.sh   (or get_idf)"

# --- preflight checks ------------------------------------------------------
[ -f "$BUILD_BIN" ]   || fail "build artifact not found: $BUILD_BIN (run idf.py build first)"
[ -f "$NEW_BIN" ]     || fail "new binary not found: $NEW_BIN"
[ -f "$PUBLIC_KEY" ]  || fail "public key not found: $PUBLIC_KEY"
[ -f "$MANIFEST" ]    || fail "CSV manifest not found: $MANIFEST"

# --- 1. copy the fresh build artifact --------------------------------------
step "1/5  Copy build artifact -> $COPY_DEST"
run cp "$BUILD_BIN" "$COPY_DEST"

# --- 2. create + verify the delta patch ------------------------------------
step "2/5  Create delta patch ($BASE_BIN -> $NEW_BIN)"
run python esp_delta_ota_patch_gen.py create_patch \
    --chip "$CHIP" \
    --base_binary "$BASE_BIN" \
    --new_binary "$NEW_BIN" \
    --patch_file_name "$PATCH_BIN"

step "2b   Verify delta patch reconstructs the new image"
run python esp_delta_ota_patch_gen.py verify_patch \
    --base_binary "$BASE_BIN" \
    --patch_file_name "$PATCH_BIN" \
    --new_binary "$NEW_BIN"

# --- 3. encrypt the patch --------------------------------------------------
step "3/5  Encrypt patch -> $PATCH_ENC_BIN"
run python esp_enc_img_gen.py encrypt "$PATCH_BIN" "$PUBLIC_KEY" "$PATCH_ENC_BIN"

# --- 4. wrap into the multi-image OTA --------------------------------------
step "4/5  Create multi-image OTA -> $OUT_OTA"
run python esp32_multi_ota_tool.py create-multi \
    -v "$VENDOR_ID" -p "$PRODUCT_ID" \
    -vn "$VERSION_NUM" -vs "$VERSION_STR" \
    -da "$DIGEST" \
    "$MANIFEST" "$OUT_OTA"

# --- 5. show the result for verification ------------------------------------
step "5/5  Inspect the generated OTA"
run python esp32_multi_ota_tool.py show "$OUT_OTA"

printf '\n\033[1;32mDone. Generated %s (%s bytes)\033[0m\n' \
    "$OUT_OTA" "$(stat -c%s "$OUT_OTA")"
