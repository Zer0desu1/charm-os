// ============================================================================
// home_proto.h - Vecta <-> akilli ev dugumu ESP-NOW protokolu (paylasilan).
//
//   Saat (mod_home.h) ile ev dugumu (tools/home_node) AYNI bu yapiyi kullanir.
//   Tek paket, sabit boyut, ESP-NOW yayinindan (broadcast) gider:
//     * ANNOUNCE : dugum -> saat. Dugum kendini ve guncel durumunu duyurur
//                  (her ~2 sn + her degisimde). Saat bundan cihaz listesini kurar.
//     * CMD      : saat -> dugum. Hedef id eslesirse dugum uygular, sonra ANNOUNCE
//                  ile geri bildirir (kapali dongu = en saglam onay).
//
//   Sihirli bayt 0x48 ('H'); espnow.h dispatcher'i bunu modul magic'i olarak
//   ayirir, Vecta-eslesmesini (HELLO 0xE0) kirletmez.
// ============================================================================
#pragma once
#include <stdint.h>

#define HOME_MAGIC     0x48
#define HOME_ANNOUNCE  1
#define HOME_CMD       2

// cihaz tipleri (ikon + dimmable davranisi icin)
enum { HT_SWITCH = 0, HT_LIGHT = 1, HT_FAN = 2, HT_SENSOR = 3, HT_COUNT };

struct HomePkt {
  uint8_t magic;      // HOME_MAGIC
  uint8_t kind;       // HOME_ANNOUNCE | HOME_CMD
  char    id[6];      // cihaz kimligi (<=6 bayt, ANSI; null-pad)
  uint8_t type;       // HT_*
  uint8_t state;      // 0=kapali, 1=acik
  uint8_t level;      // 0..100 (dim/fan; switch'te yok sayilir)
  char    name[16];   // kullanici-dostu ad (ANNOUNCE'ta dolu)
} __attribute__((packed));   // 26 bayt
