// ============================================================================
// mod_config.h - Slim-build module selection (compile-time).
//
//   Each optional module is registered into the launcher only if its MOD_<ID>
//   flag is 1. Turn modules OFF here (or override from the build system with a
//   -DMOD_<ID>=0 flag - build flags win, because each default below is only
//   applied #ifndef) to produce a SLIM firmware image.
//
//   --- BASE firmware (ships with NO modules) ---------------------------------
//   Set CHARM_BASE to 1 (here, or -DCHARM_BASE=1) to build the minimal "base"
//   firmware: every feature module defaults to OFF, leaving only connectivity +
//   on-device Settings. The watch boots, runs its SoftAP + web server (OTA), and
//   the phone provisions Wi-Fi and installs modules from the Store over the air.
//   Add specific modules back on top of the base with e.g. -DMOD_CLOCK=1.
//
//   Why this saves flash: the module .h files stay #include'd (net.h and a few
//   modules reference each other's symbols, so the code must still compile), but
//   when a module is not registerModule()'d nothing references its enter/tick
//   entry points. The ESP32 Arduino core builds with -ffunction-sections
//   -fdata-sections -Wl,--gc-sections, so that now-unreferenced code is dropped
//   from the final .bin at link time.
//
//   The phone "Store" tab (see AmoledSenderExpo) maps each module id to a
//   prebuilt .bin of a variant that includes it; "installing" OTA-flashes it.
//   So: build one .bin per bundle you want to offer, host them, and point the
//   manifest (MODULE_STORE_URL) at the list. See store/README.md.
// ============================================================================
#pragma once

// Master switch: 1 = build the BASE firmware (no feature modules by default).
#ifndef CHARM_BASE
#  define CHARM_BASE 0
#endif

// Default state every feature module inherits unless individually overridden.
#if CHARM_BASE
#  define MOD_DEFAULT 0
#else
#  define MOD_DEFAULT 1
#endif

// --- connectivity / recovery core ------------------------------------------
// Settings (on-device Wi-Fi / Bluetooth / OTA / theme) stays in even on the base
// build so the device is configurable and recoverable on its own. Drop it too
// with -DMOD_SETTINGS=0 for a launcher that relies entirely on the phone+SoftAP.
#ifndef MOD_SETTINGS
#  define MOD_SETTINGS 1
#endif

// --- feature modules (default MOD_DEFAULT: on normally, off in a base build) -
#ifndef MOD_CHARM
#  define MOD_CHARM MOD_DEFAULT
#endif
#ifndef MOD_CLOCK
#  define MOD_CLOCK MOD_DEFAULT
#endif
#ifndef MOD_TAMA
#  define MOD_TAMA MOD_DEFAULT
#endif
#ifndef MOD_COLLAR
#  define MOD_COLLAR MOD_DEFAULT
#endif
#ifndef MOD_GAME
#  define MOD_GAME MOD_DEFAULT
#endif
#ifndef MOD_BADGE
#  define MOD_BADGE MOD_DEFAULT
#endif
#ifndef MOD_NOTIFY
#  define MOD_NOTIFY MOD_DEFAULT
#endif
#ifndef MOD_AUDIO
#  define MOD_AUDIO MOD_DEFAULT
#endif
#ifndef MOD_FIDGET
#  define MOD_FIDGET MOD_DEFAULT
#endif
#ifndef MOD_FINDER
#  define MOD_FINDER MOD_DEFAULT
#endif
#ifndef MOD_BUDDY
#  define MOD_BUDDY MOD_DEFAULT
#endif
#ifndef MOD_COMPASS
#  define MOD_COMPASS MOD_DEFAULT
#endif
#ifndef MOD_DRAW
#  define MOD_DRAW MOD_DEFAULT
#endif
#ifndef MOD_KNOB
#  define MOD_KNOB MOD_DEFAULT
#endif
#ifndef MOD_LOVEBOX
#  define MOD_LOVEBOX MOD_DEFAULT
#endif
#ifndef MOD_ALBUM
#  define MOD_ALBUM MOD_DEFAULT
#endif
#ifndef MOD_MAPS
#  define MOD_MAPS MOD_DEFAULT
#endif
#ifndef MOD_WEATHER
#  define MOD_WEATHER MOD_DEFAULT
#endif
#ifndef MOD_MUSIC
#  define MOD_MUSIC MOD_DEFAULT
#endif
#ifndef MOD_STREAK
#  define MOD_STREAK MOD_DEFAULT
#endif
#ifndef MOD_POMO
#  define MOD_POMO MOD_DEFAULT
#endif
#ifndef MOD_CAPSULE
#  define MOD_CAPSULE MOD_DEFAULT
#endif
#ifndef MOD_CAMERA
#  define MOD_CAMERA MOD_DEFAULT
#endif
#ifndef MOD_RPS
#  define MOD_RPS MOD_DEFAULT
#endif
#ifndef MOD_REFLEX
#  define MOD_REFLEX MOD_DEFAULT
#endif
#ifndef MOD_TTT
#  define MOD_TTT MOD_DEFAULT
#endif
#ifndef MOD_ASSISTANT
#  define MOD_ASSISTANT MOD_DEFAULT
#endif
#ifndef MOD_VOICENOTE
#  define MOD_VOICENOTE MOD_DEFAULT
#endif
#ifndef MOD_XIAOZHI
#  define MOD_XIAOZHI MOD_DEFAULT
#endif
#ifndef MOD_OPENCLAW
#  define MOD_OPENCLAW MOD_DEFAULT
#endif
#ifndef MOD_MIRROR
#  define MOD_MIRROR MOD_DEFAULT
#endif
#ifndef MOD_NAV
#  define MOD_NAV MOD_DEFAULT
#endif
#ifndef MOD_LYRICS
#  define MOD_LYRICS MOD_DEFAULT
#endif
#ifndef MOD_READER
#  define MOD_READER MOD_DEFAULT
#endif
#ifndef MOD_ROTATE
#  define MOD_ROTATE MOD_DEFAULT
#endif

// --- dependency note -------------------------------------------------------
// Lyrics shows now-playing metadata owned by Music; mod_lyrics.h #includes
// mod_music.h directly so its symbols always compile even if MOD_MUSIC is 0
// (music just isn't a separate launcher entry). Flip MOD_MUSIC=1 to add it.
