# Vecta

Waveshare ESP32-S3 Touch AMOLED 1.32 icin **moduler platform**. Cihaz acilinca
bir **launcher** (ana menu) gosterir; sadece etkin moduller listelenir. Telefon
app'inden moduller acilir/kapanir (NVS'te saklanir, reflash gerekmez).

> **Yeni:** cihaz **modulsuz (base) firmware** ile baslayabilir; moduller telefon
> app'inin **Magaza** sekmesinden **kablosuz (OTA)** yuklenir. Bkz.
> [Slim / Base build + Modul Magazasi](#slim--base-build--modul-magazasi-ota-ile-modul-yukleme).

> Bu firmware eski `AmoledImageReceiver`'in yerine gecer. Artik onu kullanma.

## Moduller

| id | Modul | Donanim | Ne yapar |
|----|-------|---------|----------|
| `charm`  | Canta Susu | Ekran | Telefondan push'lanan resim/GIF/maskot animasyonu; yoksa isim etiketi. |
| `tama`   | Tamagotchi | Mikrofon + dokunmatik | Cevreye tepki veren sanal pet. Ses = heyecan, dokunma = okşama/besleme, sessizlik = uyku. Stat'lar NVS'te kalici. |
| `collar` | Akilli Tasma | Ekran (+QR ops.) | Pet kimlik karti (ad + sahip tel), kayip modu (yanip sonen + QR), "beni bul" parlak flash. GPS yok - ID etiketi + dikkat cekici. |
| `game`   | Oyun | Dokunmatik | Zar (d6/d20), Cark (spinner), Tur sayaci, Yazi-Tura. Ust kenara dokun = arac degistir. |
| `clock`  | Saat | Ekran (telefon senkron) | Analog + dijital saat. Dokun = stil degistir. Telefon /time ile senkron eder. |
| `badge`  | Durum Rozeti | Ekran | MUSAIT / MESGUL / TOPLANTIDA + isim, animasyonlu renkli halka. Dokun = degistir. |
| `notify` | Bildirimler | WiFi push | Akilli saat gibi: telefon arama/mesaj/uygulama bildirimlerini iletir. Gelen aramada cevapla/reddet ekrani; mesaj/bildirim kartlari; kaydirilabilir akis. Kaydir = gez, dokun = kapat. |
| `audio`  | Ses | Mikrofon | Dairesel ses gorsellestirici (bars / pulse / wave). Dokun = stil degistir. |
| `fidget` | Nefes & Fidget | Dokunmatik | Nefes rehberi (al/tut/ver) + dokununca dalga efektleri. |
| `finder` | Bulucu | WiFi RSSI + telefon GPS | AirTag tarzi: telefon mesafeyi gosterir, "caldir" -> cihaz parlar, son gorulen GPS kaydedilir. Dokun = beacon. |
| `buddy`  | Ikiz / Pusula | ESP-NOW (+pusula ops.) | Iki Vecta cihazi birbirini bulur. Manyetometre+GPS varsa **yon oku**, yoksa mesafe (sicak/soguk). Dokun = otekini cagir. |
| `knob`   | Doner Dugme | BLE HID | Ekrani **doner dugme** yap: parmagi cevir = ses ac/kis, dokun = oynat/duraklat. PC/telefona Bluetooth ile baglanir. (Kutuphane gerekir, asagi bak) |
| `lovebox`| Lovebox | WiFi | Telefondan mesaj/emoji gonder -> cihazda **kalp animasyonu** + okunmadi gostergesi. Dokun = oku. |
| `album`  | Foto Albumu | Ekran | Telefondan birden cok foto yukle -> cihaz sirayla dondurur (maks 12). Dokun = sonraki. |
| `reader` | Hizli Okuma (RSVP) | WiFi | Telefondan gelen PDF/metni **kelime kelime** gosterir (Spritz tarzi RSVP). Odak harfi (ORP) kirmizi ve sabit bir sutuna tutturulur; goz oynamaz. Hiz WPM ile ayarlanir; uzun kelime ve noktalamada otomatik bekleme. Dokun = oynat/duraklat, sag/sol kaydir = cumle atla, yukari/asagi kaydir = WPM +/-. Telefon PDF'den metni cikarir (pdf.js), cihaza parca parca akitir (`/reader`). |
| `maps`   | Harita | WiFi (+telefon GPS) | **2 mod:** (1) **Gercek ekran yansitma** (Android Auto gibi) - telefonun ekranini (Google Maps dahil) MediaProjection ile cihaza aynalar (`/map?mirror=1`, sadece Android + dev build, bkz. `AmoledSenderExpo/SCREEN_MIRROR_SETUP.md`). (2) **Uygulama ici harita** - ucretsiz OpenStreetMap'i cizip yayinlar (Expo Go'da calisir, anahtar yok). Her ikisi de cihaza JPEG kare akitir; cihaz tam ekran gosterir. |

> **Doner Dugme (knob) icin kutuphane:** Arduino Library Manager -> **"ESP32 BLE Keyboard"** (T-vK) kur.
> Kurmazsan modul "kutuphane gerekli" ekrani gosterir, gerisi calisir. Kurunca cihaz
> **"Vecta Knob"** adiyla Bluetooth'ta gorunur; PC/telefondan eslestir, medya tusu gibi calisir.

### Iki cihazli "Ikiz / Pusula" kurulumu

- Iki Vecta cihazina da firmware'i yukle. Ikisi de softAP kanali **1**'de (kod sabitliyor), ESP-NOW broadcast ile otomatik birbirini bulur.
- Sadece **mesafe** istiyorsan ek donanim yok - calisir (sicak/soguk).
- **Yon oku** icin: her cihaza bir **QMC5883L manyetometre** (dokunmatik I2C hattina, 0x0D) tak + her cihazin telefonu **Ikiz** modul ayarindan "Konumu gonder" ile GPS paylassin. Cihaz, esinin konumuna donen ok cizer.
- Manyetometre yoksa kod otomatik mesafe moduna duser (lehim gerekmez).

## QR ile Wi-Fi'a baglanma

Acilista cihaz bir **Wi-Fi QR kodu** gosterir. Telefon kamerasiyla okutunca telefon
**"bu aga baglan?"** der ve dogrudan baglanir (uygulama bile gerekmez). Bulucu modulu de
telefon bagli degilken ayni QR'i gosterir. Ekrana dokununca QR ekrani gecilir.

> **QRCode kutuphanesi gerekli:** Arduino Library Manager -> **"QRCode"** (Richard Moore) kur.
> Hem Wi-Fi baglanma QR'i hem Tasma modulunun `tel:` QR'i bunu kullanir. Kurmazsan QR yerine
> Wi-Fi adi + sifre **yazi** olarak gosterilir (her sey yine calisir).

> **TJpg_Decoder kutuphanesi gerekli (foto + harita):** `/photo` (Expo foto yukleme) ve `/map`
> (Harita modulu) JPEG cozer. Kurmazsan bu ikisi "kutuphane gerekli" cevabi verir, diger her sey calisir.
> - **PlatformIO** ile derliyorsan zaten `platformio.ini` -> `lib_deps` icinde (otomatik iner).
> - **Arduino IDE** ile derliyorsan Library Manager -> **"TJpg_Decoder"** (Bodmer) kur.
> Not: PlatformIO, Arduino IDE'ye kurdugun kutuphaneleri **gormez**; PlatformIO'da kutuphane mutlaka
> `lib_deps`'te olmali (yoksa kurulu olsa bile "kutuphane gerekli" der).

## Kontrol

- **Launcher:** parmagini **surukle = carousel doner** (atalet/momentum ile yavaslayip durur, ogeye yumusak oturur). Hizli savur = birkac oge gec. Dokun = ortadakini ac. BOOT kisa = sonraki, uzun = ac.
- **Modul icinde:** kisa dokunma = modul aksiyonu. **Uzun bas (>0.8sn) = ana menuye don.**
- **Bildirim akisi** ve liste tarzi ekranlar momentum ile kayar; liste ucunda hafif **esneme/bounce** olur.
- Dokunmatik calismazsa **BOOT butonu** her yerde yedek giris.
- Kaydirma motoru `platform.h` icindeki `Scroller` (atalet + yay + uc-bounce); baska modullere de eklenebilir.

## Kurulum (Arduino IDE)

- Kutuphane: **GFX Library for Arduino** (Arduino_GFX, moononournation)
- Board: **ESP32S3 Dev Module**
- PSRAM: **OPI PSRAM** · Flash: **8MB** · Partition: **8M with spiffs**
- `Vecta` klasorunu ac (tum `.h` dosyalari `Vecta.ino` ile ayni klasorde olmali), Upload.

## Slim / Base build + Modul Magazasi (OTA ile modul yukleme)

Hangi modullerin firmware'e gomulecegi **derleme zamaninda** secilir
(`mod_config.h`). Boylece cihaz **bos** (modulsuz) baslar, modulleri kullanici
telefon app'inin **Magaza** sekmesinden **kablosuz (OTA)** yukler. Sadece
yukledigin modul cihazda yer kaplar.

### 1) Modul secimi - `mod_config.h`

- Her ozellik modulu icin bir `MOD_<ID>` bayragi var, varsayilan `1` (acik).
- Istemedigini `0` yap (dosyada veya `-DMOD_<ID>=0` build flag'iyle; flag kazanir).
- `registerModule()` cagrisi kaldirilan modulun kodu, ESP32 Arduino cekirdeginin
  `-ffunction-sections -Wl,--gc-sections` ayari sayesinde link sirasinda `.bin`'den
  **atilir** -> gercek flash tasarrufu. (Include'lar durur; `net.h` sembolleri icin.)

### 2) BASE firmware (kutudan **modulsuz** cikar) - fabrika imaji

`CHARM_BASE=1` ile **tum** ozellik modulleri kapanir; geriye yalnizca **baglanti +
cihaz-ustu Ayarlar** kalir. Cihaz acilir, **SoftAP + web sunucusu + OTA**'yi
calistirir; telefon Wi-Fi'i ayarlar ve modulleri Magaza'dan yukler.

```bash
arduino-cli compile \
  --fqbn esp32:esp32:esp32s3 \
  --build-property "build.extra_flags=-DCHARM_BASE=1" \
  --output-dir ./out/base \
  Vecta/Vecta.ino
# -> ./out/base/Vecta.ino.bin = charm-base.bin (fabrika / ilk flash imaji)
```

Base'in uzerine birkac modul gommek istersen ek flag: `-DCHARM_BASE=1 -DMOD_CLOCK=1
-DMOD_TAMA=1`. (`settings` her zaman kalir; `-DMOD_SETTINGS=0` ile o da cikar.)
Arduino IDE kullaniyorsan: `mod_config.h`'de `CHARM_BASE` satirini `1` yap, derle,
`Sketch -> Export Compiled Binary`.

### 3) Kutudan cikis akisi (kullanici)

1. Fabrikada **`charm-base.bin`** flash'lanir (USB ile bir kez).
2. Cihaz acilir -> kendi **Vecta-XXXX** SoftAP'sini yayinlar, launcher'da yalniz **Ayarlar**.
3. Telefon, cihazin SoftAP'sine baglanir; uygulama Wi-Fi'i ve cihaz adini ayarlar.
4. Uygulamada **Magaza** sekmesi -> istedigin modulun yanindaki **Yukle** -> firmware
   kablosuz (OTA) yazilir, cihaz yeniden baslar. **Moduller** sekmesi yalnizca yuklu
   olanlari gosterir.

### 4) Magaza bundle'lari + manifest (dagitim)

- Bir slim `.bin` = bir **bundle** (cekirdek + birkac modul). Sunmak istedigin her
  bundle icin bir `.bin` derle (`-DMOD_x=0/1` ile), bir yere host'la (en kolayi
  **GitHub Release**).
- `AmoledSenderExpo/store/modules.json` manifest'ini doldur: her `id` -> o modulu
  iceren bundle'in `.bin` URL'si (birden cok id ayni bundle'i paylasabilir).
- Manifest'i host'la, sonra `AmoledSenderExpo/lib/device.ts` icinde
  `MODULE_STORE_URL`'i o adrese ayarla.
- Host yokken **"Dosyadan .bin yukle"** ile elle `.bin` secip OTA yapilabilir.
- Tam adim adim: **`AmoledSenderExpo/store/README.md`**.

## DOGRULANMASI GEREKEN PINLER

Cihaza ozel; Waveshare wiki/semasindan teyit et:

- **Dokunmatik (CST820) I2C** -> `platform.h`: `TOUCH_SDA`, `TOUCH_SCL`, `TOUCH_RST`, `TOUCH_INT`.
  Yanlissa cihaz yine calisir ama dokunma yerine sadece **BOOT** butonu gecer.
- **Mikrofon + hoparlor (ES8311)** -> `audio.h`: pinler S3_AMOLED_1_32 icin dogrulandi
  (i2c 47/48, i2s mclk38/bclk39/ws41/din40/dout42, PA 46). ES8311 I2C'de bulunursa
  gercek mic+hoparlor; bulunmazsa otomatik simule sese duser.

## HTTP API (telefon app kullanir)

```
GET  /                 durum + modul listesi (JSON)
GET  /modules          [{id,name,enabled}]
GET  /module?id=&en=0|1   modul ac/kapa
GET  /open?id=         modul ac (id=home -> ana menu)
GET  /mode?tool=dice|spinner|timer|coin&sides=&count=&slots=&secs=
GET  /charm?name=      canta susu isim etiketi
GET  /time?epoch=      saat senkronu (tz uygulanmis unix saniye)
GET  /badge?state=free|busy|meet&text=    durum rozeti
GET  /collar?name=&phone=&owner=&lost=0|1&find=1   tasma
GET  /notify?type=call|msg|mail|alarm|info&app=&title=&body=&color=  telefon bildirimi (akilli saat gibi)
GET  /notify?count=&text=&color=          (eski) basit bildirim
GET  /notify?clear=1                       bildirimleri temizle
GET  /notify?poll=1                        {callAction,calling,unread} (cihazdaki cevapla/reddet'i oku)
GET  /status           {rssi,id}  (Bulucu canli mesafe)
GET  /gps?lat=&lon=    telefon GPS'i (Ikiz pusula yon oku icin)
GET  /find             Bulucu "caldir" -> dikkat flash
GET  /love?text=&emoji=   Lovebox mesaji
POST /album            Foto albumune 466x466 RGB565-BE kare ekle
GET  /album?clear=1    albumu temizle
POST /map?lat=&lon=&zoom=   Google static-map JPEG yukle -> Harita modulu
GET  /map?poll=1       {refresh,zoom,lat,lon,has}  (telefon canli harita icin yoklar)
POST /animation        RGB565 animasyon (header 'ANIM'|frameCount|frameMs|w|h)
GET  /stats            {imu,steps,stepsLife,activity,history[7],streak,pomo,tama}  (haftalik adim grafigi)
GET  /power?sleep=&tilt=0|1   ekran uyku zaman asimi (sn, 0=kapali) + kaldirinca-uyan
GET  /face             {face,count,names[]}  watch-face galerisi
GET  /face?set=N       saat yuzu sec (clock modulunu acar)
GET  /onboard          {onboarded}
GET  /onboard?reset=1  ilk kurulum sihirbazini tekrar calistir (cihaz yeniden baslar)
GET  /ota              tarayicidan firmware guncelleme sayfasi (.bin yukle)
POST /update           OTA: derlenmis .bin'i flash'a yazip yeniden baslatir
```

## Pil tasarrufu (ekran uykusu + kaldirinca-uyan)

Hareketsizlikte (varsayilan 25 sn) AMOLED kararir (parlaklik 0 = panel sonuk, ~sifir
guc). **Dokunma**, **BOOT**, gelen bildirim ya da **bilegi kaldirma** (IMU) uyandirir.
Sure ve "kaldirinca uyan" anahtarini **Ayarlar** modulunden veya `/power` ile degistir.
Uyku sadece launcher + sakin modullerde (saat/charm/rozet/tasma/tama) ve slayt
kapaliyken devreye girer; navigasyon/harita/muzik/oyun acikken ekran sonmez.

## Firmware guncelleme (OTA, kablosuz)

Cihaza bagliyken telefon tarayicisindan `http://192.168.4.1/ota` (veya `vecta.local/ota`)
ac, Arduino IDE'nin urettigi `.bin`'i sec, **Guncelle**. Cihaz yazarken ekranda ilerleme
gosterir ve bitince otomatik yeniden baslar. Uygulamadan `otaUpdate(ip, binUri)` ile de yapilir.

## Ilk kurulum sihirbazi

Yeni cihaz ilk acilista bir **profil** sectirir (Hepsi / Sade / Fitness / Sosyal) ve
sadece o modulleri acar; gerisi NVS'te kapali kalir. Daha sonra `/onboard?reset=1` ile
tekrar calistirilir. Cihaz adi + dil telefon uygulamasindan ayarlanir.

## Saat yuzleri (watch-face galerisi)

Saat modulunde **dokun = sonraki yuz**. Yuzler: Analog, Dijital, Sade, Buyuk,
**Fit** (saat + pil halkasi + gunluk adim). Telefondan `/face?set=N` ile de secilir.

## Mikrofon + hoparlor (ES8311)

Kartta ES8311 codec + dahili mikrofon + hoparlor var. `audio.h` calisma aninda
otomatik secer: ES8311 I2C'de (0x18) ACK verirse **gercek** giris/cikis, vermezse
**simule** ses (gorseller yine calisir).

- **Mikrofon**: `audioLevel()` ortam sesini RMS olarak verir; Ses Gorsellestirici ve
  Tamagotchi gercek sese tepki verir.
- **Hoparlor**: `audioPlayTone(freq, ms)` / `soundPlay(...)` tonu DAC + PA (GPIO46)
  uzerinden calar. Zar atisinda tumbling sesi gelir (**ses temasi sessiz olmamali**).
- ES8311 register dizisi standart 16-bit / 256*fs bring-up; mic/hoparlor zayif/bozuksa
  `audio.h` icindeki `ES8311_MIC_GAIN` ve 0x17 (ADC) / 0x32 (DAC) register'larini ayarla.

Yani: hemen denemek istiyorsan dokunma; gercek mikrofon icin pinleri teyit et + `1` yap.

SSID artik cihaza ozel: **Vecta-XXXX** (XXXX = MAC son baytlari).

Wi-Fi: **Vecta** / sifre **12345678** / IP **192.168.4.1**
