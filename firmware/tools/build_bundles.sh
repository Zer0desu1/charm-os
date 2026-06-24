#!/usr/bin/env bash
# ============================================================================
# build_bundles.sh - Compile the Vecta module-store bundles (slim .bin images).
#
#   Each "bundle" is a firmware .bin built with CHARM_BASE=1 (every feature
#   module OFF) plus a hand-picked set of MOD_<ID>=1 flags. The phone Store tab
#   maps a module id -> the .bin of the bundle that contains it (see
#   AmoledSenderExpo/store/modules.json) and OTA-flashes it on "Yükle".
#
#   Output: ../../AmoledSenderExpo/store/charm-<bundle>.bin  (next to
#   modules.json, ready to push to your host / attach to a GitHub release).
#
#   Requires PlatformIO CLI (`pio`) on PATH. Run from anywhere:
#       bash Vecta/tools/build_bundles.sh            # build all bundles
#       bash Vecta/tools/build_bundles.sh social nav # build only these
# ============================================================================
set -euo pipefail

# Resolve repo paths relative to this script.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VECTA_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="$(cd "$VECTA_DIR/.." && pwd)/AmoledSenderExpo/store"
ENV="esp32-s3-devkitc-1"
BUILD_BIN="C:/Users/Gaming/.vecta_build/$ENV/firmware.bin"   # build_dir from platformio.ini

# Locate the PlatformIO CLI. It's often installed by the VSCode extension and
# not on the bash PATH, so fall back to the known install locations.
PIO=""
for c in \
  "/c/Users/Gaming/.platformio/penv/Scripts/pio.exe" \
  "/c/Users/Gaming/AppData/Local/Programs/Python/Python311/Scripts/pio.exe" \
  "${HOME:-}/.platformio/penv/Scripts/pio.exe"; do
  [ -n "$c" ] && [ -x "$c" ] && { PIO="$c"; break; }
done
[ -z "$PIO" ] && PIO="$(command -v pio || true)"
if [ -z "$PIO" ]; then
  echo "!! pio (PlatformIO CLI) not found. Install it or add it to PATH." >&2
  exit 1
fi
echo "Using pio: $PIO"

mkdir -p "$OUT_DIR"

# --- bundle definitions ------------------------------------------------------
# name  -> space-separated module ids enabled on top of CHARM_BASE.
# 'clock' is added everywhere so each bundle boots to a usable watch face.
# (Module ids must match registerModule() ids in Vecta.ino and the MOD_<ID>
#  flags in mod_config.h.)
declare -A BUNDLES=(
  [base]="__BASE__"  # CHARM_BASE: no feature modules (only Settings) - "reset" image
  [social]="clock charm badge notify lovebox collar draw"
  [fun]="clock game rps reflex ttt tama"
  [nav]="clock maps weather nav compass"
  [all]="__FULL__"   # full default build: every module compiled in
)

build_one() {
  local name="$1" ids="$2" flags=""
  echo "============================================================"
  echo ">> Building bundle: charm-$name  ($ids)"
  echo "============================================================"

  if [ "$ids" = "__FULL__" ]; then
    flags=""   # no CHARM_BASE -> mod_config.h defaults all modules ON
  elif [ "$ids" = "__BASE__" ]; then
    flags="-DCHARM_BASE=1"   # every feature module OFF (only Settings)
  else
    flags="-DCHARM_BASE=1"
    for id in $ids; do
      # uppercase the id for the MOD_<ID> flag name
      local up; up="$(echo "$id" | tr '[:lower:]' '[:upper:]')"
      flags="$flags -DMOD_$up=1"
    done
  fi

  echo "   build flags: ${flags:-<none / full>}"
  PLATFORMIO_BUILD_FLAGS="$flags" "$PIO" run -d "$VECTA_DIR" -e "$ENV"

  if [ ! -f "$BUILD_BIN" ]; then
    echo "!! ERROR: expected build output not found: $BUILD_BIN" >&2
    exit 1
  fi
  cp "$BUILD_BIN" "$OUT_DIR/charm-$name.bin"
  local sz; sz=$(stat -c%s "$OUT_DIR/charm-$name.bin" 2>/dev/null || wc -c <"$OUT_DIR/charm-$name.bin")
  echo ">> charm-$name.bin  ($sz bytes)  ->  $OUT_DIR/"
}

# --- which bundles to build --------------------------------------------------
targets=("$@")
if [ ${#targets[@]} -eq 0 ]; then
  targets=(base social fun nav all)
fi

for name in "${targets[@]}"; do
  if [ -z "${BUNDLES[$name]:-}" ]; then
    echo "!! unknown bundle '$name' (have: ${!BUNDLES[*]})" >&2
    exit 1
  fi
  build_one "$name" "${BUNDLES[$name]}"
done

echo
echo "Done. Bundles in: $OUT_DIR"
echo "Next: push these .bin files + modules.json to your host, then set"
echo "MODULE_STORE_URL in AmoledSenderExpo/lib/device.ts."
