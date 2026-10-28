/* =====================================================================
   shelly_power_monitor.ino  -  v1
   Power monitor that reads a Shelly EM (Gen1) DIRECTLY on the local network,
   without Home Assistant. SH1106 1.3" 128x64 I2C OLED display + 30-pin ESP32 DevKit.
   Same hardware and same DIN enclosure as the din_power_monitor project (v3, HA).

   FEATURES
     - Instantaneous power of total / branch 1 / branch 2, read from the Shelly every second
       (GET http://<shelly>/status, a single request per cycle)
     - Mains voltage and power factor of the two channels
     - 3-page display rotating automatically every 5 s:
         1) total power in large digits + 0-4 kW bar
         2) the two branches (with the mains voltage in the header)
         3) today's and this month's energy + daily peak
     - Passive buzzer on GPIO 27: continuous two-tone siren above the threshold;
       during an alarm the display stays on page 1 and flashes in negative
     - Daily maximum peak (value and time)
     - kWh history: 31 days + 12 months, stored in flash (LittleFS),
       calculated as the difference of the Shelly cumulative counters (Wh)
     - Web page at http://shelly-power-monitor.local  (or http://IP)
       with live values, daily/monthly bar charts and adjustment of the
       alarm threshold without recompiling
     - OTA firmware update
     - Date and time taken from the Shelly (unixtime field of /status), with NTP
       as a fallback when there is Internet access. Daylight saving time handled automatically.

   SHELLY EM GEN1 (type SHEM) - what is read from /status
     emeters[n].power     instantaneous W of channel n
     emeters[n].voltage   mains V
     emeters[n].pf        power factor
     emeters[n].is_valid  false if the clamp is not connected / reading not valid
     emeters[n].total     cumulative Wh of channel n
     unixtime             Shelly UTC time (0 if the Shelly is not synchronised)
   Shelly Gen2/Gen3 devices (Pro EM, EM Gen3) use the RPC API instead: this sketch
   is written for Gen1, the one with the "old style" web interface.

   WIRING  (4 wires, soldered directly to the display pads)
     Looking at the display FROM THE FRONT, the 4 pads at the top from left to right are:
         VSS(GND) - VDD - SCK - SDA          (2.54 pitch)
     From the back the order is reversed: SDA - SCK - VDD - VSS.

     Display SH1106     ESP32
     ---------------------------------
     VSS  (GND) ->      GND
     VDD        ->      3V3     (the module has a regulator, 5V also works)
     SCK  (SCL) ->      GPIO 22
     SDA        ->      GPIO 21

     Passive buzzer:    + -> GPIO 27      - -> GND

     NOTE: SH1106 modules almost always use I2C address 0x3C.
     If yours has the jumper set to 0x3D, change OLED_ADDR below.

   LIBRARIES (Arduino IDE Library Manager)
     - "U8g2" by oliver (olikraus)  >= 2.34
     (Wire, WiFi, HTTPClient, WebServer, ESPmDNS, LittleFS and ArduinoOTA are part of the ESP32 core)

   ARDUINO IDE SETTINGS
     Board: ESP32 Dev Module - Flash 4MB
     Partition Scheme: "Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)"
     (needed both for OTA and for LittleFS)

   OTA UPDATE
     First upload over USB. From then on:
     Tools -> Port -> "shelly-power-monitor" (network port).
   ===================================================================== */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <FS.h>
#include <LittleFS.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <time.h>
#include <sys/time.h>

// ============================ CONFIGURATION ==========================
#define WIFI_SSID       "YOUR_WIFI_NETWORK_NAME"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

#define SHELLY_HOST     "192.168.1.50"      // Shelly EM IP (better if reserved in the router)
#define SHELLY_USER     ""                // only if "Restrict login" is enabled on the Shelly
#define SHELLY_PASS     ""

#define CANALE_1        0                 // emeters[] index of the first branch
#define CANALE_2        1                 // emeters[] index of the second branch
#define USA_CANALE_2    1                 // 0 if you have only one clamp
#define NOME_1          "House"           // branch names (display and web page)
#define NOME_2          "Cellar"

#define FUSO_ORARIO     "CET-1CEST,M3.5.0,M10.5.0/3"   // Italy, with daylight saving time (change for your country)
#define NTP_SERVER      "pool.ntp.org"                 // fallback if the Shelly has no time

#define OTA_HOSTNAME    "shelly-power-monitor"         // also the mDNS name of the web page
#define OTA_PASSWORD    "YOUR_OTA_PASSWORD"
// ======================================================================

// ---------------------------- PIN -------------------------------------
#define PIN_SDA     21
#define PIN_SCL     22
#define OLED_ADDR   0x3C     // 0x3D on some clones
#define I2C_HZ      400000
#define PIN_BUZZER  27

// -------------------------- PARAMETERS --------------------------------
#define ROTAZIONE_OLED  U8G2_R0   // U8G2_R2 if the display is mounted upside down
#define SOGLIA_DEFAULT   3100.0  // initial alarm threshold (then adjustable from the web page)
#define ISTERESI          100.0  // the buzzer stops below (threshold - hysteresis)
#define BARRA_FONDO      4000.0  // full scale of the bar graph
#define PERIODO_LETTURA   1000   // ms between two Shelly readings
#define PERIODO_PAGINA    5000   // ms spent on each page
#define PERIODO_SCHERMO    500   // ms between two display redraws
#define PERIODO_SALVA   1800000  // ms between two periodic saves (30 min)
#define TIMEOUT_HTTP      1500   // ms timeout of the HTTP request to the Shelly
#define ERRORI_OFFLINE       3   // consecutive failed readings before going OFFLINE
#define N_PAGINE             3

#define N_GIORNI  31             // days of history
#define N_MESI    12             // months of history
#define FILE_DATI "/consumi.bin"
#define MAGIC     0x4D505331UL   // "MPS1"
#define EPOCA_MIN 1700000000UL   // below this value the time is not valid

// Buzzer: two-tone siren
#define TONO_A      800
#define TONO_B     1250
#define DURATA_NOTA 280
#define CANALE_LEDC   0

// ----------------------- SCREEN AREAS ---------------------------------
#define W_SCHERMO  128
#define H_SCHERMO   64
#define Y_TESTA      8     // baseline of the header row
#define Y_LINEA     11     // separator line
#define BARRA_X      0
#define BARRA_Y     50
#define BARRA_W    128
#define BARRA_H     13

// --------------------------- FONT -------------------------------------
#define F_MINI    u8g2_font_5x8_tf          // status row, notes
#define F_PICCOLO u8g2_font_6x12_tf         // labels
#define F_MEDIO   u8g2_font_logisoso16_tn   // medium values (digits only)
#define F_GRANDE  u8g2_font_logisoso28_tn   // main value (digits only)

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(ROTAZIONE_OLED, U8X8_PIN_NONE);
WebServer server(80);

// ======================== PERSISTENT DATA =============================
struct VoceGiorno { int16_t anno; uint8_t mese; uint8_t giorno; float casa; float cant; };
struct VoceMese   { int16_t anno; uint8_t mese;                 float casa; float cant; };

struct Dati {
  uint32_t   magic;
  float      soglia;                 // alarm threshold in W
  int16_t    annoCorr; uint8_t meseCorr; uint8_t giornoCorr;
  double     baseCasaG, baseCantG;   // counters at the start of the day (kWh)
  double     baseCasaM, baseCantM;   // counters at the start of the month (kWh)
  float      picco;                  // peak W of the day
  char       piccoOra[6];            // "HH:MM"
  uint8_t    nGiorni;
  uint8_t    nMesi;
  VoceGiorno giorni[N_GIORNI];
  VoceMese   mesi[N_MESI];
};
Dati dati;

// a Shelly EM channel (declared up here because of Arduino's automatic prototypes)
struct Canale {
  bool   ok;        // clamp present and reading valid
  float  w;         // instantaneous W
  float  v;         // mains V
  float  pf;        // power factor
  double kwh;       // cumulative counter in kWh
};

// ----------------------------- STATE ----------------------------------
float  wTotale = NAN, wCasa = NAN, wCantina = NAN;   // branch 1 = "house", branch 2 = "cellar"
float  tensione = NAN, pf1 = NAN, pf2 = NAN;
double kCasa   = NAN, kCantina = NAN;                // cumulative counters in kWh
char   oraStr[6]   = "--:--";
char   dataStr[11] = "----------";
char   nomeDisp1[12], nomeDisp2[12];                 // upper-case names for the OLED

bool datiValidi = false;
uint8_t erroriDiFila = ERRORI_OFFLINE;
bool allarme = false, lampeggio = false;
bool otaAttivo = false, otaInCorso = false;
int  otaUltimaPerc = -1;
uint8_t pagina = 0;

unsigned long tUltimaLettura = 0, tUltimaNota = 0, tUltimoLampeggio = 0;
unsigned long tUltimoSalva = 0, tUltimaPagina = 0, tUltimoSchermo = 0;
bool notaAlta = false;

// ======================================================================
//  BUZZER
// ======================================================================
void buzzerInit() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_BUZZER, 1000, 10);
#else
  ledcSetup(CANALE_LEDC, 1000, 10);
  ledcAttachPin(PIN_BUZZER, CANALE_LEDC);
#endif
}

void buzzerTono(uint32_t hz) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  if (hz == 0) ledcWrite(PIN_BUZZER, 0);
  else         ledcWriteTone(PIN_BUZZER, hz);
#else
  if (hz == 0) ledcWrite(CANALE_LEDC, 0);
  else         ledcWriteTone(CANALE_LEDC, hz);
#endif
}

void gestisciBuzzer() {
  if (!allarme) { buzzerTono(0); return; }
  if (millis() - tUltimaNota >= DURATA_NOTA) {
    tUltimaNota = millis();
    notaAlta = !notaAlta;
    buzzerTono(notaAlta ? TONO_B : TONO_A);
  }
}

// ======================================================================
//  HISTORY IN FLASH
// ======================================================================
void datiDefault() {
  memset(&dati, 0, sizeof(dati));
  dati.magic  = MAGIC;
  dati.soglia = SOGLIA_DEFAULT;
  strcpy(dati.piccoOra, "--:--");
}

void salvaDati() {
  File f = LittleFS.open(FILE_DATI, "w");
  if (!f) { Serial.println("[FS] write failed"); return; }
  f.write((uint8_t*)&dati, sizeof(dati));
  f.close();
  tUltimoSalva = millis();
  Serial.println("[FS] data saved");
}

void caricaDati() {
  if (!LittleFS.begin(true)) { Serial.println("[FS] mount failed"); datiDefault(); return; }
  File f = LittleFS.open(FILE_DATI, "r");
  if (!f || f.size() != sizeof(dati)) {
    if (f) f.close();
    Serial.println("[FS] no valid archive, creating a new one");
    datiDefault();
    return;
  }
  f.read((uint8_t*)&dati, sizeof(dati));
  f.close();
  if (dati.magic != MAGIC) { Serial.println("[FS] incompatible archive"); datiDefault(); return; }
  if (dati.soglia < 500 || dati.soglia > 20000) dati.soglia = SOGLIA_DEFAULT;
  Serial.printf("[FS] archive ok: %u days, %u months, threshold %.0f W\n",
                dati.nGiorni, dati.nMesi, dati.soglia);
}

void aggiungiGiorno(int16_t a, uint8_t m, uint8_t g, float casa, float cant) {
  if (dati.nGiorni >= N_GIORNI) {
    memmove(&dati.giorni[0], &dati.giorni[1], sizeof(VoceGiorno) * (N_GIORNI - 1));
    dati.nGiorni = N_GIORNI - 1;
  }
  dati.giorni[dati.nGiorni++] = { a, m, g, casa, cant };
}

void aggiungiMese(int16_t a, uint8_t m, float casa, float cant) {
  if (dati.nMesi >= N_MESI) {
    memmove(&dati.mesi[0], &dati.mesi[1], sizeof(VoceMese) * (N_MESI - 1));
    dati.nMesi = N_MESI - 1;
  }
  dati.mesi[dati.nMesi++] = { a, m, casa, cant };
}

float kwhOggiCasa() { return (isnan(kCasa))    ? 0 : (float)(kCasa    - dati.baseCasaG); }
float kwhOggiCant() { return (isnan(kCantina)) ? 0 : (float)(kCantina - dati.baseCantG); }
float kwhMeseCasa() { return (isnan(kCasa))    ? 0 : (float)(kCasa    - dati.baseCasaM); }
float kwhMeseCant() { return (isnan(kCantina)) ? 0 : (float)(kCantina - dati.baseCantM); }

// closes the day/month when the date changes and restarts from new baselines
void aggiornaCalendario(int16_t a, uint8_t m, uint8_t g) {
  if (a < 2020) return;
  bool salva = false;

  // very first start
  if (dati.annoCorr == 0) {
    dati.annoCorr = a; dati.meseCorr = m; dati.giornoCorr = g;
    dati.baseCasaG = dati.baseCasaM = isnan(kCasa)    ? 0 : kCasa;
    dati.baseCantG = dati.baseCantM = isnan(kCantina) ? 0 : kCantina;
    dati.picco = 0; strcpy(dati.piccoOra, "--:--");
    salvaDati();
    return;
  }

  // day change
  if (a != dati.annoCorr || m != dati.meseCorr || g != dati.giornoCorr) {
    aggiungiGiorno(dati.annoCorr, dati.meseCorr, dati.giornoCorr,
                   kwhOggiCasa(), kwhOggiCant());
    dati.baseCasaG = isnan(kCasa)    ? dati.baseCasaG : kCasa;
    dati.baseCantG = isnan(kCantina) ? dati.baseCantG : kCantina;
    dati.picco = 0; strcpy(dati.piccoOra, "--:--");
    salva = true;

    // month change
    if (a != dati.annoCorr || m != dati.meseCorr) {
      aggiungiMese(dati.annoCorr, dati.meseCorr, kwhMeseCasa(), kwhMeseCant());
      dati.baseCasaM = isnan(kCasa)    ? dati.baseCasaM : kCasa;
      dati.baseCantM = isnan(kCantina) ? dati.baseCantM : kCantina;
    }
    dati.annoCorr = a; dati.meseCorr = m; dati.giornoCorr = g;
  }

  // the Shelly counter has restarted from zero (reset, replacement, update)
  if (!isnan(kCasa) && kCasa + 0.001 < dati.baseCasaG) { dati.baseCasaG = kCasa; dati.baseCasaM = kCasa; salva = true; }
  if (!isnan(kCantina) && kCantina + 0.001 < dati.baseCantG) { dati.baseCantG = kCantina; dati.baseCantM = kCantina; salva = true; }

  if (salva) salvaDati();
}

// ======================================================================
//  CLOCK  (time from the Shelly, NTP as fallback)
// ======================================================================
bool oraValida() { return time(nullptr) > (time_t)EPOCA_MIN; }

// resets the internal clock to the Shelly time if it differs by more than 30 s
void sincronizzaDaShelly(uint32_t unixtime) {
  if (unixtime < EPOCA_MIN) return;               // the Shelly does not have the time yet
  time_t adesso = time(nullptr);
  long scarto = (long)adesso - (long)unixtime;
  if (!oraValida() || scarto > 30 || scarto < -30) {
    struct timeval tv = { (time_t)unixtime, 0 };
    settimeofday(&tv, nullptr);
    Serial.printf("[TIME] taken from the Shelly (offset %ld s)\n", oraValida() ? scarto : 0L);
  }
}

// updates the date/time strings and returns the local date
bool aggiornaOrologio(int16_t* a, uint8_t* m, uint8_t* g) {
  if (!oraValida()) { strcpy(oraStr, "--:--"); return false; }
  time_t t = time(nullptr);
  struct tm tm;
  localtime_r(&t, &tm);
  strftime(oraStr,  sizeof(oraStr),  "%H:%M",    &tm);
  strftime(dataStr, sizeof(dataStr), "%Y-%m-%d", &tm);
  if (a) *a = tm.tm_year + 1900;
  if (m) *m = tm.tm_mon + 1;
  if (g) *g = tm.tm_mday;
  return true;
}

// ======================================================================
//  GRAPHICS UTILITIES
// ======================================================================

// Formats a power value for the "digits only" font: below 10 kW whole watts,
// above that kW with one decimal. The unit is printed separately with a text font.
void formattaW(float w, char* out, size_t n, const char** unita) {
  if (isnan(w)) { snprintf(out, n, "----"); *unita = "W"; return; }
  if (fabs(w) >= 10000) { snprintf(out, n, "%.1f", w / 1000.0); *unita = "kW"; }
  else                  { snprintf(out, n, "%d", (int)lroundf(w)); *unita = "W"; }
}

void testoDx(const char* s, int xDestra, int baseline, const uint8_t* f) {
  u8g2.setFont(f);
  u8g2.drawStr(xDestra - u8g2.getStrWidth(s), baseline, s);
}

void testoCx(const char* s, int xCentro, int baseline, const uint8_t* f) {
  u8g2.setFont(f);
  u8g2.drawStr(xCentro - u8g2.getStrWidth(s) / 2, baseline, s);
}

void maiuscolo(const char* da, char* a, size_t n) {
  size_t i = 0;
  for (; da[i] && i < n - 1; i++) a[i] = toupper((unsigned char)da[i]);
  a[i] = 0;
}

// common header: title on the left, clock on the right, page dots
void intestazione(const char* titolo, bool pallini) {
  u8g2.setFont(F_MINI);
  u8g2.drawStr(0, Y_TESTA, titolo);
  testoDx(oraStr, W_SCHERMO, Y_TESTA, F_MINI);
  if (pallini) {
    for (uint8_t i = 0; i < N_PAGINE; i++) {
      int x = 58 + i * 7;
      if (i == pagina) u8g2.drawBox(x, 2, 5, 5);
      else             u8g2.drawFrame(x, 2, 5, 5);
    }
  }
  u8g2.drawHLine(0, Y_LINEA, W_SCHERMO);
}

// horizontal bar with ticks every 1 kW drawn in XOR
void barraPotenza(float w) {
  u8g2.drawFrame(BARRA_X, BARRA_Y, BARRA_W, BARRA_H);
  float v = isnan(w) ? 0 : w;
  if (v < 0) v = 0;
  int riemp = (int)((BARRA_W - 2) * (v / BARRA_FONDO));
  if (riemp > BARRA_W - 2) riemp = BARRA_W - 2;
  if (riemp > 0) u8g2.drawBox(BARRA_X + 1, BARRA_Y + 1, riemp, BARRA_H - 2);
  u8g2.setDrawColor(2);                       // XOR: the ticks are always visible
  for (int k = 1; k <= 3; k++) {
    int x = BARRA_X + (int)(BARRA_W * (k * 1000.0 / BARRA_FONDO));
    u8g2.drawVLine(x, BARRA_Y + 1, BARRA_H - 2);
  }
  u8g2.setDrawColor(1);
}

// ======================================================================
//  DISPLAY PAGES
// ======================================================================

// page 1: total power in large digits + bar
void paginaTotale() {
  intestazione(allarme ? "OVERLOAD" : "TOTAL", !allarme);
  char buf[12]; const char* um;
  formattaW(wTotale, buf, sizeof(buf), &um);

  u8g2.setFont(F_PICCOLO);
  int xUm = W_SCHERMO - u8g2.getStrWidth(um);
  u8g2.drawStr(xUm, 42, um);

  if (isnan(wTotale)) testoDx(buf, xUm - 4, 42, F_PICCOLO);
  else                testoDx(buf, xUm - 4, 42, F_GRANDE);

  barraPotenza(wTotale);
}

// page 2: the two branches
void ramo(const char* et, float w, int baseline) {
  char buf[12]; const char* um;
  formattaW(w, buf, sizeof(buf), &um);
  u8g2.setFont(F_PICCOLO);
  u8g2.drawStr(0, baseline, et);
  int xUm = W_SCHERMO - u8g2.getStrWidth(um);
  u8g2.drawStr(xUm, baseline, um);
  if (isnan(w)) testoDx(buf, xUm - 3, baseline, F_PICCOLO);
  else          testoDx(buf, xUm - 3, baseline, F_MEDIO);
}

void paginaRami() {
  char tit[16];
  if (isnan(tensione)) snprintf(tit, sizeof(tit), "BRANCHES");
  else                 snprintf(tit, sizeof(tit), "BRANCH %dV", (int)lroundf(tensione));
  intestazione(tit, true);
  ramo(nomeDisp1, wCasa, 34);
#if USA_CANALE_2
  ramo(nomeDisp2, wCantina, 60);
#else
  ramo("MAINS V", tensione, 60);
#endif
}

// page 3: today's energy, this month's energy and peak
void voceEnergia(const char* et, float kwh, int decimali, int baseline) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%.*f", decimali, kwh);
  u8g2.setFont(F_PICCOLO);
  u8g2.drawStr(0, baseline, et);
  int xUm = W_SCHERMO - u8g2.getStrWidth("kWh");
  u8g2.drawStr(xUm, baseline, "kWh");
  testoDx(buf, xUm - 3, baseline, F_MEDIO);
}

void paginaEnergia() {
  intestazione("ENERGY", true);
  voceEnergia("TODAY", kwhOggiCasa() + kwhOggiCant(), 2, 32);
  voceEnergia("MONTH", kwhMeseCasa() + kwhMeseCant(), 1, 52);
  char buf[32];
  snprintf(buf, sizeof(buf), "peak %d W at %s",
           (int)lroundf(dati.picco), dati.piccoOra);
  u8g2.setFont(F_MINI);
  u8g2.drawStr(0, 63, buf);
}

// screen shown when the Shelly does not answer
void paginaOffline() {
  intestazione("OFFLINE", false);
  testoCx("Shelly EM", W_SCHERMO / 2, 28, F_PICCOLO);
  testoCx("not reachable", W_SCHERMO / 2, 42, F_PICCOLO);
  char buf[32];
  snprintf(buf, sizeof(buf), "%s  WiFi %s", SHELLY_HOST,
           WiFi.status() == WL_CONNECTED ? "ok" : "missing");
  testoCx(buf, W_SCHERMO / 2, 60, F_MINI);
}

void disegnaSchermo() {
  if (otaInCorso) return;                 // during OTA schermataOTA() is in charge
  u8g2.clearBuffer();

  if (!datiValidi)     paginaOffline();
  else if (allarme)    paginaTotale();    // during an alarm it stays on page 1
  else switch (pagina) {
    case 0:  paginaTotale();  break;
    case 1:  paginaRami();    break;
    default: paginaEnergia(); break;
  }

  // during an alarm the screen flashes in negative
  if (allarme && lampeggio) {
    u8g2.setDrawColor(2);
    u8g2.drawBox(0, 0, W_SCHERMO, H_SCHERMO);
    u8g2.setDrawColor(1);
  }
  u8g2.sendBuffer();
}

// ======================================================================
//  READING FROM THE SHELLY EM
// ======================================================================
// looks for "key":number between positions da..a of the JSON response
bool campo(const String& r, int da, int a, const char* chiave, double& out) {
  String k = String("\"") + chiave + "\":";
  int p = r.indexOf(k, da);
  if (p < 0 || p >= a) return false;
  const char* s = r.c_str() + p + k.length();
  if (*s == 'n') return false;                    // null
  out = atof(s);
  return true;
}

// extracts the n-th object of the "emeters" array
bool leggiCanale(const String& r, int inizioArray, int indice, Canale& c) {
  c.ok = false; c.w = NAN; c.v = NAN; c.pf = NAN; c.kwh = NAN;
  int ini = inizioArray;
  for (int i = 0; i <= indice; i++) {
    ini = r.indexOf('{', ini + 1);
    if (ini < 0) return false;
  }
  int fine = r.indexOf('}', ini);
  if (fine < 0) return false;

  int pv = r.indexOf("\"is_valid\":true", ini);
  bool valido = (pv >= 0 && pv < fine);

  double x;
  if (campo(r, ini, fine, "power",   x)) c.w   = x;
  if (campo(r, ini, fine, "voltage", x)) c.v   = x;
  if (campo(r, ini, fine, "pf",      x)) c.pf  = x;
  if (campo(r, ini, fine, "total",   x)) c.kwh = x / 1000.0;   // Wh -> kWh

  c.ok = valido && !isnan(c.w);
  if (!c.ok) { c.w = NAN; c.pf = NAN; }
  return true;
}

bool leggiShelly() {
  WiFiClient client;
  HTTPClient http;

  String url = String("http://") + SHELLY_HOST + "/status";
  if (!http.begin(client, url)) return false;
  http.setConnectTimeout(TIMEOUT_HTTP);
  http.setTimeout(TIMEOUT_HTTP);
  if (strlen(SHELLY_USER) > 0) http.setAuthorization(SHELLY_USER, SHELLY_PASS);

  int code = http.GET();
  if (code != 200) { Serial.printf("[SHELLY] HTTP %d\n", code); http.end(); return false; }
  String r = http.getString();
  http.end();

  int pe = r.indexOf("\"emeters\"");
  if (pe < 0) { Serial.println("[SHELLY] response without emeters: is it a Shelly EM Gen1?"); return false; }

  Canale c1, c2;
  if (!leggiCanale(r, pe, CANALE_1, c1)) return false;
#if USA_CANALE_2
  if (!leggiCanale(r, pe, CANALE_2, c2)) return false;
#else
  c2.ok = true; c2.w = 0; c2.v = NAN; c2.pf = NAN; c2.kwh = 0;
#endif

  wCasa    = c1.w;
  wCantina = c2.w;
  pf1      = c1.pf;
  pf2      = c2.pf;
  tensione = !isnan(c1.v) ? c1.v : c2.v;
  // total = sum of the valid channels (a disconnected clamp does not send everything OFFLINE)
  if (c1.ok || c2.ok) wTotale = (c1.ok ? c1.w : 0) + (c2.ok ? c2.w : 0);
  else                wTotale = NAN;
  if (!isnan(c1.kwh)) kCasa    = c1.kwh;
  if (!isnan(c2.kwh)) kCantina = c2.kwh;

  // Shelly time
  double ut;
  if (campo(r, 0, r.length(), "unixtime", ut)) sincronizzaDaShelly((uint32_t)ut);

  int16_t a; uint8_t m, g;
  if (aggiornaOrologio(&a, &m, &g)) aggiornaCalendario(a, m, g);

  // peak of the day
  if (!isnan(wTotale) && wTotale > dati.picco) {
    dati.picco = wTotale;
    strncpy(dati.piccoOra, oraStr, sizeof(dati.piccoOra) - 1);
    dati.piccoOra[sizeof(dati.piccoOra) - 1] = 0;
  }
  return !isnan(wTotale);
}

// ======================================================================
//  WEB PAGE
// ======================================================================
static const char PAGINA[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Power monitor</title><style>
*{box-sizing:border-box}
body{margin:0;background:#0d1117;color:#e6edf3;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{background:#161b22;padding:14px 18px;border-bottom:1px solid #30363d;display:flex;align-items:center;gap:10px}
header h1{font-size:17px;margin:0;font-weight:600;flex:1}
#dot{width:10px;height:10px;border-radius:50%;background:#3fb950}
#dot.ko{background:#f85149}
main{max-width:900px;margin:0 auto;padding:16px}
.griglia{display:grid;gap:12px;grid-template-columns:repeat(auto-fit,minmax(150px,1fr))}
.card{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:14px}
.card .et{font-size:11px;letter-spacing:.9px;color:#8b949e;text-transform:uppercase}
.card .vl{font-size:30px;font-weight:700;margin-top:6px;line-height:1.1}
.card .um{font-size:14px;color:#8b949e;font-weight:500;margin-left:4px}
.big .vl{font-size:52px}
.v{color:#3fb950}.g{color:#d29922}.r{color:#f85149}.n{color:#8b949e}
h2{font-size:14px;color:#8b949e;text-transform:uppercase;letter-spacing:.9px;margin:26px 0 10px}
.gr{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:12px}
svg{width:100%;height:auto;display:block}
.leg{display:flex;gap:18px;font-size:12px;color:#8b949e;margin-top:8px;flex-wrap:wrap}
.leg i{display:inline-block;width:11px;height:11px;border-radius:2px;margin-right:6px;vertical-align:-1px}
.riga{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
input[type=number]{background:#0d1117;border:1px solid #30363d;color:#e6edf3;border-radius:7px;padding:9px 11px;width:120px;font-size:15px}
button{background:#238636;border:0;color:#fff;border-radius:7px;padding:10px 18px;font-size:14px;font-weight:600;cursor:pointer}
button:active{background:#1a6f2b}
#all{background:#f85149;color:#fff;padding:12px;border-radius:10px;text-align:center;font-weight:700;letter-spacing:1px;margin-bottom:14px;display:none}
footer{color:#6e7681;font-size:12px;text-align:center;padding:22px 12px 32px;line-height:1.7}
</style></head><body>
<header><span id="dot"></span><h1>Power monitor</h1><span id="ora" style="color:#8b949e;font-size:13px"></span></header>
<main>
<div id="all">OVERLOAD</div>

<div class="griglia">
  <div class="card big" style="grid-column:1/-1"><div class="et">Total power</div><div class="vl"><span id="tot">--</span><span class="um">W</span></div></div>
  <div class="card"><div class="et n1">Branch 1</div><div class="vl"><span id="casa">--</span><span class="um">W</span></div><div class="et" style="margin-top:4px">cos&phi; <span id="pf1">--</span></div></div>
  <div class="card" id="card2"><div class="et n2">Branch 2</div><div class="vl"><span id="cant">--</span><span class="um">W</span></div><div class="et" style="margin-top:4px">cos&phi; <span id="pf2">--</span></div></div>
  <div class="card"><div class="et">Mains voltage</div><div class="vl"><span id="vv" class="n">--</span><span class="um">V</span></div></div>
  <div class="card"><div class="et">Today's peak</div><div class="vl"><span id="pk" class="n">--</span><span class="um">W</span></div><div class="et" style="margin-top:4px">at <span id="pko">--:--</span></div></div>
</div>

<h2>Energy</h2>
<div class="griglia">
  <div class="card"><div class="et">Today</div><div class="vl"><span id="og">--</span><span class="um">kWh</span></div><div class="et" style="margin-top:6px"><span class="n1">branch 1</span> <span id="ogc">--</span> &middot; <span class="n2">branch 2</span> <span id="ogk">--</span></div></div>
  <div class="card"><div class="et">This month</div><div class="vl"><span id="me">--</span><span class="um">kWh</span></div><div class="et" style="margin-top:6px"><span class="n1">branch 1</span> <span id="mec">--</span> &middot; <span class="n2">branch 2</span> <span id="mek">--</span></div></div>
</div>

<h2>Daily consumption</h2>
<div class="gr"><div id="cg"></div>
<div class="leg"><span><i style="background:#388bfd"></i><span class="n1">Branch 1</span></span><span><i style="background:#2ea043"></i><span class="n2">Branch 2</span></span><span id="tg"></span></div></div>

<h2>Monthly consumption</h2>
<div class="gr"><div id="cm"></div>
<div class="leg"><span><i style="background:#388bfd"></i><span class="n1">Branch 1</span></span><span><i style="background:#2ea043"></i><span class="n2">Branch 2</span></span><span id="tm"></span></div></div>

<h2>Alarm threshold</h2>
<div class="card"><div class="riga">
  <input type="number" id="sg" min="500" max="20000" step="50">
  <span class="n">W</span><button onclick="salva()">Save</button>
  <span id="msg" class="n"></span>
</div></div>

<footer id="ft"></footer>
</main>
<script>
const $=i=>document.getElementById(i);
const cls=w=>w<=2000?'v':(w<=3000?'g':'r');
const nf=(v,d)=>v==null||isNaN(v)?'--':v.toFixed(d);
let N1='Branch 1',N2='Branch 2';

function barre(cont,voci,etich){
  if(!voci.length){cont.innerHTML='<p style="color:#6e7681;font-size:13px;margin:8px 4px">No data recorded yet.</p>';return 0;}
  const W=620,H=220,ml=44,mb=26,mt=8,mr=6;
  const gw=W-ml-mr, gh=H-mt-mb;
  let max=0,somma=0;
  voci.forEach(v=>{const t=v.c+v.k; if(t>max)max=t; somma+=t;});
  max=max<=0?1:max*1.12;
  const bw=gw/voci.length, bar=Math.max(3,bw*0.66);
  let s=`<svg viewBox="0 0 ${W} ${H}" preserveAspectRatio="xMidYMid meet">`;
  for(let i=0;i<=3;i++){
    const y=mt+gh-gh*i/3, val=max*i/3;
    s+=`<line x1="${ml}" y1="${y}" x2="${W-mr}" y2="${y}" stroke="#30363d" stroke-width="1"/>`;
    s+=`<text x="${ml-6}" y="${y+4}" fill="#6e7681" font-size="11" text-anchor="end">${val>=100?val.toFixed(0):val.toFixed(1)}</text>`;
  }
  voci.forEach((v,i)=>{
    const x=ml+i*bw+(bw-bar)/2;
    const hc=gh*v.c/max, hk=gh*v.k/max;
    s+=`<g><title>${etich[i]}: ${(v.c+v.k).toFixed(2)} kWh (${N1} ${v.c.toFixed(2)} / ${N2} ${v.k.toFixed(2)})</title>`;
    s+=`<rect x="${x}" y="${mt+gh-hc-hk}" width="${bar}" height="${hk}" fill="#2ea043"/>`;
    s+=`<rect x="${x}" y="${mt+gh-hc}" width="${bar}" height="${hc}" fill="#388bfd"/></g>`;
    const passo=Math.ceil(voci.length/12);
    if(i%passo===0||i===voci.length-1)
      s+=`<text x="${x+bar/2}" y="${H-8}" fill="#6e7681" font-size="10" text-anchor="middle">${etich[i]}</text>`;
  });
  s+=`<line x1="${ml}" y1="${mt+gh}" x2="${W-mr}" y2="${mt+gh}" stroke="#484f58"/></svg>`;
  cont.innerHTML=s;
  return somma;
}

async function live(){
  try{
    const d=await (await fetch('/api/live')).json();
    N1=d.n1; N2=d.n2;
    document.querySelectorAll('.n1').forEach(e=>e.textContent=d.n1);
    document.querySelectorAll('.n2').forEach(e=>e.textContent=d.n2);
    $('card2').style.display=d.c2?'':'none';
    $('dot').className=d.ok?'':'ko';
    $('ora').textContent=d.ora||'';
    $('tot').textContent=d.ok?Math.round(d.tot):'--';
    $('tot').className=d.ok?cls(d.tot):'n';
    $('casa').textContent=d.ok?Math.round(d.casa):'--';
    $('casa').className=d.ok?cls(d.casa):'n';
    $('cant').textContent=d.ok?Math.round(d.cant):'--';
    $('cant').className=d.ok?cls(d.cant):'n';
    $('pf1').textContent=d.ok?nf(d.pf1,2):'--';
    $('pf2').textContent=d.ok?nf(d.pf2,2):'--';
    $('vv').textContent=d.ok&&d.v>0?nf(d.v,1):'--';
    $('pk').textContent=Math.round(d.picco); $('pk').className=cls(d.picco);
    $('pko').textContent=d.piccoOra;
    $('og').textContent=nf(d.ogC+d.ogK,2); $('ogc').textContent=nf(d.ogC,2); $('ogk').textContent=nf(d.ogK,2);
    $('me').textContent=nf(d.meC+d.meK,1); $('mec').textContent=nf(d.meC,1); $('mek').textContent=nf(d.meK,1);
    $('all').style.display=d.allarme?'block':'none';
    if(document.activeElement!==$('sg')) $('sg').value=d.soglia;
    $('ft').innerHTML=`IP ${d.ip} &middot; WiFi ${d.rssi} dBm &middot; up for ${Math.floor(d.up/3600)}h ${Math.floor(d.up%3600/60)}m<br>Shelly EM ${d.sh} &middot; alarm threshold ${d.soglia} W &middot; history ${d.ng} days / ${d.nm} months`;
  }catch(e){ $('dot').className='ko'; }
}

async function storico(){
  try{
    const d=await (await fetch('/api/storico')).json();
    const sg=barre($('cg'),d.giorni,d.giorni.map(v=>v.d));
    const sm=barre($('cm'),d.mesi,d.mesi.map(v=>v.m));
    $('tg').textContent=d.giorni.length?`period total ${sg.toFixed(1)} kWh`:'';
    $('tm').textContent=d.mesi.length?`period total ${sm.toFixed(0)} kWh`:'';
  }catch(e){}
}

async function salva(){
  const v=parseInt($('sg').value);
  if(!(v>=500&&v<=20000)){$('msg').textContent='invalid value';return;}
  const r=await fetch('/api/soglia?w='+v,{method:'POST'});
  $('msg').textContent=r.ok?'saved':'error';
  setTimeout(()=>$('msg').textContent='',2500);
  live();
}

live().then(storico);
setInterval(live,2000);
setInterval(storico,60000);
</script></body></html>)rawliteral";

// ======================================================================
//  WEB SERVER API
// ======================================================================
static float z(float v) { return isnan(v) ? 0 : v; }

void apiLive() {
  char b[768];
  snprintf(b, sizeof(b),
    "{\"ok\":%s,\"tot\":%.1f,\"casa\":%.1f,\"cant\":%.1f,"
    "\"v\":%.1f,\"pf1\":%.2f,\"pf2\":%.2f,"
    "\"picco\":%.0f,\"piccoOra\":\"%s\","
    "\"ogC\":%.3f,\"ogK\":%.3f,\"meC\":%.2f,\"meK\":%.2f,"
    "\"allarme\":%s,\"soglia\":%.0f,\"rssi\":%d,\"ip\":\"%s\","
    "\"up\":%lu,\"ora\":\"%s %s\",\"ng\":%u,\"nm\":%u,"
    "\"n1\":\"%s\",\"n2\":\"%s\",\"c2\":%s,\"sh\":\"%s\"}",
    datiValidi ? "true" : "false",
    z(wTotale), z(wCasa), z(wCantina),
    z(tensione), z(pf1), z(pf2),
    dati.picco, dati.piccoOra,
    kwhOggiCasa(), kwhOggiCant(), kwhMeseCasa(), kwhMeseCant(),
    allarme ? "true" : "false", dati.soglia,
    WiFi.RSSI(), WiFi.localIP().toString().c_str(),
    millis() / 1000UL, oraValida() ? dataStr : "", oraStr, dati.nGiorni, dati.nMesi,
    NOME_1, NOME_2, USA_CANALE_2 ? "true" : "false", SHELLY_HOST);
  server.send(200, "application/json", b);
}

void apiStorico() {
  String j = "{\"giorni\":[";
  for (uint8_t i = 0; i < dati.nGiorni; i++) {
    char v[96];
    snprintf(v, sizeof(v), "%s{\"d\":\"%02u/%02u\",\"c\":%.3f,\"k\":%.3f}",
             i ? "," : "", dati.giorni[i].giorno, dati.giorni[i].mese,
             dati.giorni[i].casa, dati.giorni[i].cant);
    j += v;
  }
  // the current day, still open
  if (dati.annoCorr != 0) {
    char v[96];
    snprintf(v, sizeof(v), "%s{\"d\":\"%02u/%02u\",\"c\":%.3f,\"k\":%.3f}",
             dati.nGiorni ? "," : "", dati.giornoCorr, dati.meseCorr,
             kwhOggiCasa(), kwhOggiCant());
    j += v;
  }
  j += "],\"mesi\":[";
  for (uint8_t i = 0; i < dati.nMesi; i++) {
    char v[96];
    snprintf(v, sizeof(v), "%s{\"m\":\"%02u/%04d\",\"c\":%.2f,\"k\":%.2f}",
             i ? "," : "", dati.mesi[i].mese, dati.mesi[i].anno,
             dati.mesi[i].casa, dati.mesi[i].cant);
    j += v;
  }
  if (dati.annoCorr != 0) {
    char v[96];
    snprintf(v, sizeof(v), "%s{\"m\":\"%02u/%04d\",\"c\":%.2f,\"k\":%.2f}",
             dati.nMesi ? "," : "", dati.meseCorr, dati.annoCorr,
             kwhMeseCasa(), kwhMeseCant());
    j += v;
  }
  j += "]}";
  server.send(200, "application/json", j);
}

void apiSoglia() {
  if (!server.hasArg("w")) { server.send(400, "text/plain", "missing w"); return; }
  float v = server.arg("w").toFloat();
  if (v < 500 || v > 20000) { server.send(400, "text/plain", "out of range"); return; }
  dati.soglia = v;
  salvaDati();
  Serial.printf("[WEB] new threshold %.0f W\n", v);
  server.send(200, "application/json", "{\"ok\":true}");
}

void avviaWebServer() {
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", PAGINA); });
  server.on("/api/live",    HTTP_GET, apiLive);
  server.on("/api/storico", HTTP_GET, apiStorico);
  server.on("/api/soglia",  HTTP_ANY, apiSoglia);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();
  Serial.println("[WEB] server started on port 80");
}

// ======================================================================
//  OTA
// ======================================================================
void schermataOTA(const char* titolo, int perc) {
  u8g2.clearBuffer();
  u8g2.setFont(F_MINI);
  u8g2.drawStr(0, Y_TESTA, "OTA UPDATE");
  u8g2.drawHLine(0, Y_LINEA, W_SCHERMO);
  testoCx(titolo, W_SCHERMO / 2, 26, F_PICCOLO);
  if (perc >= 0) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", perc);
    testoCx(buf, W_SCHERMO / 2 - 8, 46, F_MEDIO);
    u8g2.setFont(F_PICCOLO);
    u8g2.drawStr(W_SCHERMO / 2 + 14, 46, "%");
    u8g2.drawFrame(0, 52, W_SCHERMO, 11);
    int riemp = (W_SCHERMO - 2) * perc / 100;
    if (riemp > 0) u8g2.drawBox(1, 53, riemp, 9);
  }
  u8g2.sendBuffer();
}

void avviaOTA() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    buzzerTono(0);
    otaInCorso = true;
    otaUltimaPerc = -1;
    schermataOTA(ArduinoOTA.getCommand() == U_FLASH ? "Firmware" : "Filesystem", 0);
  });

  ArduinoOTA.onProgress([](unsigned int fatto, unsigned int totale) {
    int perc = (totale > 0) ? (int)((fatto * 100UL) / totale) : 0;
    if (perc == otaUltimaPerc) return;
    otaUltimaPerc = perc;
    schermataOTA("in progress", perc);
  });

  ArduinoOTA.onEnd([]() { schermataOTA("Complete", 100); });

  ArduinoOTA.onError([](ota_error_t e) {
    const char* m = "Unknown error";
    if      (e == OTA_AUTH_ERROR)    m = "Wrong password";
    else if (e == OTA_BEGIN_ERROR)   m = "Begin failed";
    else if (e == OTA_CONNECT_ERROR) m = "Connection lost";
    else if (e == OTA_RECEIVE_ERROR) m = "Receive failed";
    else if (e == OTA_END_ERROR)     m = "End failed";
    schermataOTA(m, -1);
    delay(4000);
    ESP.restart();
  });

  ArduinoOTA.begin();
  otaAttivo = true;
  Serial.println("[OTA] ready");
}

// ======================================================================
//  WIFI
// ======================================================================
void avviaWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(OTA_HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] connecting");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) { delay(300); Serial.print('.'); }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) Serial.println("[WiFi] IP " + WiFi.localIP().toString());
  else Serial.println("[WiFi] timeout, retrying in the background");
}

void avviaServizi() {
  if (MDNS.begin(OTA_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("[mDNS] http://" OTA_HOSTNAME ".local");
  }
  configTzTime(FUSO_ORARIO, NTP_SERVER);   // fallback: if there is Internet access the time also comes from here
  avviaOTA();
  avviaWebServer();
}

// startup screen
void schermataAvvio(const char* riga2, const char* riga3) {
  u8g2.clearBuffer();
  testoCx("Power monitor", W_SCHERMO / 2, 18, F_PICCOLO);
  u8g2.drawHLine(8, 24, W_SCHERMO - 16);
  if (riga2) testoCx(riga2, W_SCHERMO / 2, 42, F_PICCOLO);
  if (riga3) testoCx(riga3, W_SCHERMO / 2, 58, F_MINI);
  u8g2.sendBuffer();
}

// ======================================================================
//  SETUP / LOOP
// ======================================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  setenv("TZ", FUSO_ORARIO, 1);
  tzset();
  maiuscolo(NOME_1, nomeDisp1, sizeof(nomeDisp1));
  maiuscolo(NOME_2, nomeDisp2, sizeof(nomeDisp2));

  buzzerInit();
  buzzerTono(0);

  Wire.begin(PIN_SDA, PIN_SCL, I2C_HZ);
  u8g2.setI2CAddress(OLED_ADDR * 2);      // U8g2 wants the address already shifted
  u8g2.begin();
  u8g2.setBusClock(I2C_HZ);
  u8g2.setContrast(255);
  schermataAvvio("starting...", NULL);

  caricaDati();
  avviaWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    avviaServizi();
    schermataAvvio("WiFi connected", WiFi.localIP().toString().c_str());
    delay(1500);
  } else {
    schermataAvvio("No WiFi", "retrying in background");
    delay(1500);
  }

  tUltimoSalva  = millis();
  tUltimaPagina = millis();
  disegnaSchermo();
}

void loop() {
  if (otaAttivo) {
    ArduinoOTA.handle();
    server.handleClient();
  }

  // --- non-blocking WiFi reconnection ---
  static unsigned long tWiFi = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - tWiFi > 10000) {
    tWiFi = millis();
    Serial.println("[WiFi] reconnecting");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
  if (!otaAttivo && WiFi.status() == WL_CONNECTED) avviaServizi();

  // --- periodic reading of the Shelly EM ---
  if (millis() - tUltimaLettura >= PERIODO_LETTURA) {
    tUltimaLettura = millis();
    bool ok = (WiFi.status() == WL_CONNECTED) ? leggiShelly() : false;
    if (ok) erroriDiFila = 0;
    else if (erroriDiFila < 255) erroriDiFila++;
    // one missed reading does not clear the values: go OFFLINE only after a few consecutive errors
    datiValidi = (erroriDiFila < ERRORI_OFFLINE);
    if (!datiValidi) { wTotale = NAN; wCasa = NAN; wCantina = NAN; tensione = NAN; pf1 = NAN; pf2 = NAN; }
    if (!ok) aggiornaOrologio(nullptr, nullptr, nullptr);   // the clock keeps running anyway

    if (!isnan(wTotale)) {
      if (!allarme && wTotale > dati.soglia)                allarme = true;
      else if (allarme && wTotale < dati.soglia - ISTERESI) allarme = false;
    } else allarme = false;
  }

  // --- automatic page rotation ---
  if (allarme || !datiValidi) {
    tUltimaPagina = millis();                 // no rotation during an alarm or offline
  } else if (millis() - tUltimaPagina >= PERIODO_PAGINA) {
    tUltimaPagina = millis();
    pagina = (pagina + 1) % N_PAGINE;
  }

  // --- flashing during an alarm ---
  if (millis() - tUltimoLampeggio >= 500) {
    tUltimoLampeggio = millis();
    lampeggio = allarme ? !lampeggio : false;
  }

  // --- display redraw ---
  if (millis() - tUltimoSchermo >= PERIODO_SCHERMO) {
    tUltimoSchermo = millis();
    disegnaSchermo();
  }

  // --- periodic saving of the peak ---
  if (millis() - tUltimoSalva >= PERIODO_SALVA) salvaDati();

  gestisciBuzzer();
}
