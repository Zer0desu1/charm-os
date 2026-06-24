// ============================================================================
// mod_mirror.h - Module: Yansit (Vecta ekranini TELEFONA yansitma)
//   Ters yonde ekran aynalama: telefonun (veya herhangi bir tarayicinin)
//   http://vecta.local/mirror sayfasi, cihazin framebuffer'ini GET /screen ile
//   dongulu ceker ve canvasa cizer (~5-10 fps). Uygulama gerekmez.
//
//   /screen her ekranda calisir (yansitma icin bu modulde durmak SART degil);
//   bu modul kurulum ekranidir: adresi gosterir, izleyici baglaninca soyler.
// ============================================================================
#pragma once

#include "platform.h"
#include <WiFi.h>

// Izleyici gostergesi: net.h hScreenRaw her /screen isteginde platform.h'deki
// g_mirrorPollAt'i damgalar (ayni damga yansitma boyunca sesleri de susturur).
static void mirror_enter() {}

static void mirror_tick() {
  uint32_t now = millis();
  bool viewing = soundMuted();              // = son 2.5sn icinde /screen istegi var

  gfx->fillScreen(C_BG);

  // telefon ikonu + disari ok (saat -> telefon)
  uint16_t col = viewing ? C_OK : C_ACCENT;
  gfx->drawRoundRect(CXi - 34, 86, 68, 110, 12, col);
  gfx->drawRoundRect(CXi - 33, 87, 66, 108, 11, col);
  gfx->fillRect(CXi - 10, 180, 20, 4, col);                  // home cizgisi
  if (viewing) {
    // izleyici varken telefon "ekraninda" canli nokta yanip soner
    if ((now / 400) & 1) gfx->fillCircle(CXi, 132, 10, C_OK);
  } else {
    gfx->fillTriangle(CXi, 116, CXi - 12, 136, CXi + 12, 136, col);  // ok ucu
    gfx->fillRect(CXi - 4, 136, 8, 22, col);
  }

  textCenter("TELEFONA YANSIT", CXi, 48, 3, C_GOLD);

  textCenter("telefonun tarayicisindan ac:", CXi, 232, 2, C_DIM);
  textCenter("vecta.local/mirror", CXi, 262, 3, C_TEXT);

  IPAddress mip = WiFi.localIP();
  if (mip == IPAddress(0, 0, 0, 0)) mip = WiFi.softAPIP();   // AP-only -> 192.168.4.1
  String ipLine = String("veya  ") + mip.toString() + "/mirror";
  textCenter(ipLine.c_str(), CXi, 296, 2, C_DIM);

  textCenter(viewing ? "izleyici BAGLI - canli yayinda" : "izleyici bekleniyor...",
             CXi, 350, 2, viewing ? C_OK : C_WARN);
  textCenter("ekran her modulde yayinlanir", CXi, 396, 2, C_DIM);
  textCenter("uzun bas: cikis", CXi, 428, 2, C_DIM);

  present();
  delay(80);
}
