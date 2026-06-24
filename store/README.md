# Charm module store — manifest + slim firmware bundles

The phone app's **Mağaza / Store** tab installs modules by OTA-flashing a prebuilt
firmware `.bin` over Wi-Fi. This folder holds the **manifest** (`modules.json`) that
tells the app which `.bin` to flash for each module.

## 1. How the pieces fit

```
phone Store tab ──reads──▶ modules.json (this folder, hosted)
                              │  each module id → a firmware .bin URL
                              ▼
                         charm-<bundle>.bin (hosted)
                              │  built from the firmware with MOD_* flags
                              ▼  OTA POST /update over Wi-Fi
                         the watch reflashes & reboots
```

A slim `.bin` is a **bundle** of modules (core + a few), not a single module. Several
module ids in `modules.json` can share the same `bin` URL.

## 2. Build a slim firmware bundle

Module selection is compile-time in [`../../Vecta/mod_config.h`](../../Vecta/mod_config.h).
Every feature `MOD_<ID>` defaults to `1`. Set the ones you don't want to `0` — either
by editing `mod_config.h`, or by passing build flags (build flags win).

### The BASE firmware (no modules — flash this first / at the factory)

`CHARM_BASE=1` turns every feature module OFF, leaving only connectivity + the
on-device Settings. The watch boots, runs its SoftAP + web server (OTA), the phone
provisions Wi-Fi, and the user installs everything else from the Store.

```bash
arduino-cli compile \
  --fqbn esp32:esp32:esp32s3 \
  --build-property "build.extra_flags=-DCHARM_BASE=1" \
  --output-dir ./out/base \
  ../../Vecta/Vecta.ino
# → ./out/base/Vecta.ino.bin  → charm-base.bin   (the factory / first-flash image)
```

Add a few modules on top of the base with extra `-DMOD_x=1` flags, e.g.
`-DCHARM_BASE=1 -DMOD_CLOCK=1 -DMOD_TAMA=1`. (Settings stays unless you pass
`-DMOD_SETTINGS=0`.)

**Plain Arduino IDE:** edit `mod_config.h`, flip flags to `0`, compile, then
`Sketch → Export Compiled Binary`. The `.bin` lands next to the sketch.

**arduino-cli (repeatable, one bundle per command):**

```bash
# "fun" bundle: only games + pet on top of the core (charm/clock/settings)
arduino-cli compile \
  --fqbn esp32:esp32:esp32s3 \
  --build-property "build.extra_flags=-DMOD_NOTIFY=0 -DMOD_MAPS=0 -DMOD_WEATHER=0 -DMOD_NAV=0 -DMOD_MUSIC=0 -DMOD_LYRICS=0 -DMOD_READER=0 -DMOD_ASSISTANT=0 -DMOD_XIAOZHI=0 -DMOD_OPENCLAW=0 -DMOD_MIRROR=0 -DMOD_CAMERA=0 -DMOD_ALBUM=0 -DMOD_LOVEBOX=0 -DMOD_DRAW=0 -DMOD_COLLAR=0 -DMOD_BADGE=0" \
  --output-dir ./out/fun \
  ../../Vecta/Vecta.ino
# → ./out/fun/Vecta.ino.bin  → rename to charm-fun.bin
```

> The ESP32 Arduino core builds with `-ffunction-sections -fdata-sections
> -Wl,--gc-sections`, so modules you don't `registerModule()` (because their
> `MOD_*` is 0) get dropped from the final image at link time. CORE modules
> (`charm`, `clock`, `settings`) are always included so the watch still boots,
> joins Wi-Fi, and can receive the next OTA.

Build whatever bundles you want to sell, e.g. `charm-core`, `charm-social`,
`charm-fun`, `charm-nav`, `charm-all`.

## 3. Host the bins + manifest

Upload each `charm-*.bin` and `modules.json` to any static host (a GitHub Release
is easiest):

- Tag a release, attach the `.bin` files → their "raw" download URLs go in the
  `bin` fields of `modules.json`.
- Put `modules.json` somewhere with a stable URL (release asset or
  `raw.githubusercontent.com/<you>/<repo>/<branch>/store/modules.json`).

## 4. Point the app at it

In [`../lib/device.ts`](../lib/device.ts) set:

```ts
export const MODULE_STORE_URL = 'https://YOUR_HOST/charm/modules.json';
```

That's it. The Store tab now lists every catalog module; ones already on the
device show **Yüklü**, the rest show **Yükle** and OTA-flash the matching bundle.
Until you host real bins, the **📁 Dosyadan .bin yükle** button installs a `.bin`
you pick manually.

## 5. Notes / caveats

- `modules.json` accepts either `{ "modules": [ ... ] }` or a bare `[ ... ]`.
- A module the device already has (reported by `GET /modules`) shows as installed
  and isn't offered for install.
- Modules whose web endpoints live in `net.h` (e.g. `maps`, `album`) keep their
  shared data even when their UI is stripped; that's fine, just a few KB.
- After an OTA the device reboots (~6 s); the app re-reads the installed set.
