/* =====================================================================
   monitor_potenza_shelly.ino  -  v1
   Monitor consumi che legge DIRETTAMENTE uno Shelly EM (Gen1) in rete locale,
   senza Home Assistant. Display OLED SH1106 1.3" 128x64 I2C + ESP32 DevKit 30 pin.
   Stesso hardware e stesso contenitore DIN del progetto monitor_potenza_din (v3, HA).

   FUNZIONI
     - Potenza istantanea di totale / ramo 1 / ramo 2, letta dallo Shelly ogni secondo
       (GET http://<shelly>/status, una sola richiesta per ciclo)
     - Tensione di rete e fattore di potenza dei due canali
     - Display a 3 pagine che ruotano automaticamente ogni 5 s:
         1) potenza totale con cifre grandi + barra 0-4 kW
         2) i due rami (con la tensione di rete nell'intestazione)
         3) energia di oggi e del mese + picco giornaliero
     - Buzzer passivo GPIO 27: sirena bitonale continua sopra soglia;
       in allarme il display si blocca sulla pagina 1 e lampeggia in negativo
     - Picco massimo giornaliero (valore e ora)
     - Storico kWh: 31 giorni + 12 mesi, salvato in flash (LittleFS),
       calcolato per differenza dai contatori cumulativi dello Shelly (Wh)
     - Pagina web su http://monitor-potenza-shelly.local  (o http://IP)
       con valori live, grafici a barre giornalieri/mensili e regolazione
       della soglia di allarme senza ricompilare
     - Aggiornamento firmware OTA
     - Data e ora prese dallo Shelly (campo unixtime di /status), con NTP
       come riserva se c'e' Internet. Ora legale gestita in automatico.

   SHELLY EM GEN1 (tipo SHEM) - cosa si legge da /status
     emeters[n].power     W istantanei del canale n
     emeters[n].voltage   V di rete
     emeters[n].pf        fattore di potenza
     emeters[n].is_valid  false se la pinza non e' collegata / lettura non valida
     emeters[n].total     Wh cumulativi del canale n
     unixtime             ora UTC dello Shelly (0 se lo Shelly non e' sincronizzato)
   Gli Shelly Gen2/Gen3 (Pro EM, EM Gen3) usano invece le API RPC: questo sketch
   e' scritto per il Gen1, quello con l'interfaccia web "vecchio stile".

   COLLEGAMENTI  (4 fili, saldati direttamente ai pad del display)
     Guardando il display DA DAVANTI, i 4 pad in alto da sinistra a destra sono:
         VSS(GND) - VDD - SCK - SDA          (passo 2.54)
     Dal retro l'ordine e' invertito: SDA - SCK - VDD - VSS.

     Display SH1106     ESP32
     ---------------------------------
     VSS  (GND) ->      GND
     VDD        ->      3V3     (il modulo ha il regolatore, va bene anche 5V)
     SCK  (SCL) ->      GPIO 22
     SDA        ->      GPIO 21

     Buzzer passivo:    + -> GPIO 27      - -> GND

     NOTA: i moduli SH1106 hanno quasi sempre indirizzo I2C 0x3C.
     Se il tuo ha il ponticello sul 0x3D cambia OLED_ADDR qui sotto.

   LIBRERIE (Gestore librerie di Arduino IDE)
     - "U8g2" di oliver (olikraus)  >= 2.34
     (Wire, WiFi, HTTPClient, WebServer, ESPmDNS, LittleFS e ArduinoOTA sono nel core ESP32)

   IMPOSTAZIONI ARDUINO IDE
     Scheda: ESP32 Dev Module - Flash 4MB
     Partition Scheme: "Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)"
     (serve sia per l'OTA sia per LittleFS)

   AGGIORNAMENTO OTA
     Primo caricamento via USB. Dai successivi:
     Strumenti -> Porta -> "monitor-potenza-shelly" (porta di rete).
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

// ============================ CONFIGURAZIONE ==========================
#define WIFI_SSID       "NOME_DELLA_TUA_RETE_WIFI"
#define WIFI_PASSWORD   "LA_TUA_PASSWORD_WIFI"

#define SHELLY_HOST     "192.168.1.50"      // IP dello Shelly EM (meglio se riservato nel router)
#define SHELLY_USER     ""                // solo se nello Shelly e' attivo "Restrict login"
#define SHELLY_PASS     ""

#define CANALE_1        0                 // indice di emeters[] del primo ramo
#define CANALE_2        1                 // indice di emeters[] del secondo ramo
#define USA_CANALE_2    1                 // 0 se hai una sola pinza
#define NOME_1          "Casa"            // nomi dei rami (display e pagina web)
#define NOME_2          "Cantina"

#define FUSO_ORARIO     "CET-1CEST,M3.5.0,M10.5.0/3"   // Italia, con ora legale
#define NTP_SERVER      "pool.ntp.org"                 // riserva se lo Shelly non ha l'ora

#define OTA_HOSTNAME    "monitor-potenza-shelly"       // anche nome mDNS del sito
#define OTA_PASSWORD    "LA_TUA_PASSWORD_OTA"
// ======================================================================

// ---------------------------- PIN -------------------------------------
#define PIN_SDA     21
#define PIN_SCL     22
#define OLED_ADDR   0x3C     // 0x3D su alcuni cloni
#define I2C_HZ      400000
#define PIN_BUZZER  27

// -------------------------- PARAMETRI ---------------------------------
#define ROTAZIONE_OLED  U8G2_R0   // U8G2_R2 se monti il display capovolto
#define SOGLIA_DEFAULT   3100.0  // soglia allarme iniziale (poi regolabile dal web)
#define ISTERESI          100.0  // il buzzer tace sotto (soglia - isteresi)
#define BARRA_FONDO      4000.0  // fondo scala della barra grafica
#define PERIODO_LETTURA   1000   // ms tra due letture dello Shelly
#define PERIODO_PAGINA    5000   // ms di permanenza su ogni pagina
#define PERIODO_SCHERMO    500   // ms tra due ridisegni del display
#define PERIODO_SALVA   1800000  // ms tra due salvataggi periodici (30 min)
#define TIMEOUT_HTTP      1500   // ms timeout richiesta HTTP allo Shelly
#define ERRORI_OFFLINE       3   // letture fallite di fila prima di andare OFFLINE
#define N_PAGINE             3

#define N_GIORNI  31             // giorni di storico
#define N_MESI    12             // mesi di storico
#define FILE_DATI "/consumi.bin"
#define MAGIC     0x4D505331UL   // "MPS1"
#define EPOCA_MIN 1700000000UL   // sotto questo valore l'ora non e' valida

// Buzzer: sirena bitonale
#define TONO_A      800
#define TONO_B     1250
#define DURATA_NOTA 280
#define CANALE_LEDC   0

// ----------------------- AREE DELLO SCHERMO ---------------------------
#define W_SCHERMO  128
#define H_SCHERMO   64
#define Y_TESTA      8     // baseline della riga di intestazione
#define Y_LINEA     11     // riga separatrice
#define BARRA_X      0
#define BARRA_Y     50
#define BARRA_W    128
#define BARRA_H     13

// --------------------------- FONT -------------------------------------
#define F_MINI    u8g2_font_5x8_tf          // riga di stato, note
#define F_PICCOLO u8g2_font_6x12_tf         // etichette
#define F_MEDIO   u8g2_font_logisoso16_tn   // valori medi (solo cifre)
#define F_GRANDE  u8g2_font_logisoso28_tn   // valore principale (solo cifre)

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(ROTAZIONE_OLED, U8X8_PIN_NONE);
WebServer server(80);

// ======================== DATI PERSISTENTI ============================
struct VoceGiorno { int16_t anno; uint8_t mese; uint8_t giorno; float casa; float cant; };
struct VoceMese   { int16_t anno; uint8_t mese;                 float casa; float cant; };

struct Dati {
  uint32_t   magic;
  float      soglia;                 // soglia allarme in W
  int16_t    annoCorr; uint8_t meseCorr; uint8_t giornoCorr;
  double     baseCasaG, baseCantG;   // contatori a inizio giornata (kWh)
  double     baseCasaM, baseCantM;   // contatori a inizio mese (kWh)
  float      picco;                  // picco W della giornata
  char       piccoOra[6];            // "HH:MM"
  uint8_t    nGiorni;
  uint8_t    nMesi;
  VoceGiorno giorni[N_GIORNI];
  VoceMese   mesi[N_MESI];
};
Dati dati;

// un canale dello Shelly EM (dichiarata qui in alto per i prototipi automatici di Arduino)
struct Canale {
  bool   ok;        // pinza presente e lettura valida
  float  w;         // W istantanei
  float  v;         // V di rete
  float  pf;        // fattore di potenza
  double kwh;       // contatore cumulativo in kWh
};

// ----------------------------- STATO ----------------------------------
float  wTotale = NAN, wCasa = NAN, wCantina = NAN;   // ramo 1 = "casa", ramo 2 = "cantina"
float  tensione = NAN, pf1 = NAN, pf2 = NAN;
double kCasa   = NAN, kCantina = NAN;                // contatori cumulativi in kWh
char   oraStr[6]   = "--:--";
char   dataStr[11] = "----------";
char   nomeDisp1[12], nomeDisp2[12];                 // nomi in maiuscolo per l'OLED

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
//  STORICO SU FLASH
// ======================================================================
void datiDefault() {
  memset(&dati, 0, sizeof(dati));
  dati.magic  = MAGIC;
  dati.soglia = SOGLIA_DEFAULT;
  strcpy(dati.piccoOra, "--:--");
}

void salvaDati() {
  File f = LittleFS.open(FILE_DATI, "w");
  if (!f) { Serial.println("[FS] scrittura fallita"); return; }
  f.write((uint8_t*)&dati, sizeof(dati));
  f.close();
  tUltimoSalva = millis();
  Serial.println("[FS] dati salvati");
}

void caricaDati() {
  if (!LittleFS.begin(true)) { Serial.println("[FS] mount fallito"); datiDefault(); return; }
  File f = LittleFS.open(FILE_DATI, "r");
  if (!f || f.size() != sizeof(dati)) {
    if (f) f.close();
    Serial.println("[FS] nessun archivio valido, ne creo uno nuovo");
    datiDefault();
    return;
  }
  f.read((uint8_t*)&dati, sizeof(dati));
  f.close();
  if (dati.magic != MAGIC) { Serial.println("[FS] archivio incompatibile"); datiDefault(); return; }
  if (dati.soglia < 500 || dati.soglia > 20000) dati.soglia = SOGLIA_DEFAULT;
  Serial.printf("[FS] archivio ok: %u giorni, %u mesi, soglia %.0f W\n",
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

// chiude giorno/mese quando cambia la data e riparte con nuove basi
void aggiornaCalendario(int16_t a, uint8_t m, uint8_t g) {
  if (a < 2020) return;
  bool salva = false;

  // primo avvio in assoluto
  if (dati.annoCorr == 0) {
    dati.annoCorr = a; dati.meseCorr = m; dati.giornoCorr = g;
    dati.baseCasaG = dati.baseCasaM = isnan(kCasa)    ? 0 : kCasa;
    dati.baseCantG = dati.baseCantM = isnan(kCantina) ? 0 : kCantina;
    dati.picco = 0; strcpy(dati.piccoOra, "--:--");
    salvaDati();
    return;
  }

  // cambio giorno
  if (a != dati.annoCorr || m != dati.meseCorr || g != dati.giornoCorr) {
    aggiungiGiorno(dati.annoCorr, dati.meseCorr, dati.giornoCorr,
                   kwhOggiCasa(), kwhOggiCant());
    dati.baseCasaG = isnan(kCasa)    ? dati.baseCasaG : kCasa;
    dati.baseCantG = isnan(kCantina) ? dati.baseCantG : kCantina;
    dati.picco = 0; strcpy(dati.piccoOra, "--:--");
    salva = true;

    // cambio mese
    if (a != dati.annoCorr || m != dati.meseCorr) {
      aggiungiMese(dati.annoCorr, dati.meseCorr, kwhMeseCasa(), kwhMeseCant());
      dati.baseCasaM = isnan(kCasa)    ? dati.baseCasaM : kCasa;
      dati.baseCantM = isnan(kCantina) ? dati.baseCantM : kCantina;
    }
    dati.annoCorr = a; dati.meseCorr = m; dati.giornoCorr = g;
  }

  // il contatore dello Shelly e' ripartito da zero (reset, sostituzione, aggiornamento)
  if (!isnan(kCasa) && kCasa + 0.001 < dati.baseCasaG) { dati.baseCasaG = kCasa; dati.baseCasaM = kCasa; salva = true; }
  if (!isnan(kCantina) && kCantina + 0.001 < dati.baseCantG) { dati.baseCantG = kCantina; dati.baseCantM = kCantina; salva = true; }

  if (salva) salvaDati();
}

// ======================================================================
//  OROLOGIO  (ora dallo Shelly, NTP di riserva)
// ======================================================================
bool oraValida() { return time(nullptr) > (time_t)EPOCA_MIN; }

// rimette l'orologio interno sull'ora dello Shelly se si discosta piu' di 30 s
void sincronizzaDaShelly(uint32_t unixtime) {
  if (unixtime < EPOCA_MIN) return;               // lo Shelly non ha ancora l'ora
  time_t adesso = time(nullptr);
  long scarto = (long)adesso - (long)unixtime;
  if (!oraValida() || scarto > 30 || scarto < -30) {
    struct timeval tv = { (time_t)unixtime, 0 };
    settimeofday(&tv, nullptr);
    Serial.printf("[ORA] presa dallo Shelly (scarto %ld s)\n", oraValida() ? scarto : 0L);
  }
}

// aggiorna le stringhe di data/ora e restituisce la data locale
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
//  UTILITA' GRAFICHE
// ======================================================================

// Formatta una potenza per il font "solo cifre": sotto 10 kW l'intero in W,
// sopra i kW con un decimale. L'unita' va stampata a parte con un font di testo.
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

// intestazione comune: titolo a sinistra, orologio a destra, pallini pagina
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

// barra orizzontale con tacche ogni 1 kW disegnate in XOR
void barraPotenza(float w) {
  u8g2.drawFrame(BARRA_X, BARRA_Y, BARRA_W, BARRA_H);
  float v = isnan(w) ? 0 : w;
  if (v < 0) v = 0;
  int riemp = (int)((BARRA_W - 2) * (v / BARRA_FONDO));
  if (riemp > BARRA_W - 2) riemp = BARRA_W - 2;
  if (riemp > 0) u8g2.drawBox(BARRA_X + 1, BARRA_Y + 1, riemp, BARRA_H - 2);
  u8g2.setDrawColor(2);                       // XOR: le tacche si vedono sempre
  for (int k = 1; k <= 3; k++) {
    int x = BARRA_X + (int)(BARRA_W * (k * 1000.0 / BARRA_FONDO));
    u8g2.drawVLine(x, BARRA_Y + 1, BARRA_H - 2);
  }
  u8g2.setDrawColor(1);
}

// ======================================================================
//  PAGINE DEL DISPLAY
// ======================================================================

// pagina 1: potenza totale in cifre grandi + barra
void paginaTotale() {
  intestazione(allarme ? "SOVRACCARICO" : "TOTALE", !allarme);
  char buf[12]; const char* um;
  formattaW(wTotale, buf, sizeof(buf), &um);

  u8g2.setFont(F_PICCOLO);
  int xUm = W_SCHERMO - u8g2.getStrWidth(um);
  u8g2.drawStr(xUm, 42, um);

  if (isnan(wTotale)) testoDx(buf, xUm - 4, 42, F_PICCOLO);
  else                testoDx(buf, xUm - 4, 42, F_GRANDE);

  barraPotenza(wTotale);
}

// pagina 2: i due rami
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
  if (isnan(tensione)) snprintf(tit, sizeof(tit), "RAMI");
  else                 snprintf(tit, sizeof(tit), "RAMI %dV", (int)lroundf(tensione));
  intestazione(tit, true);
  ramo(nomeDisp1, wCasa, 34);
#if USA_CANALE_2
  ramo(nomeDisp2, wCantina, 60);
#else
  ramo("RETE V", tensione, 60);
#endif
}

// pagina 3: energia di oggi, del mese e picco
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
  intestazione("ENERGIA", true);
  voceEnergia("OGGI", kwhOggiCasa() + kwhOggiCant(), 2, 32);
  voceEnergia("MESE", kwhMeseCasa() + kwhMeseCant(), 1, 52);
  char buf[32];
  snprintf(buf, sizeof(buf), "picco %d W alle %s",
           (int)lroundf(dati.picco), dati.piccoOra);
  u8g2.setFont(F_MINI);
  u8g2.drawStr(0, 63, buf);
}

// schermata quando lo Shelly non risponde
void paginaOffline() {
  intestazione("OFFLINE", false);
  testoCx("Shelly EM", W_SCHERMO / 2, 28, F_PICCOLO);
  testoCx("non raggiungibile", W_SCHERMO / 2, 42, F_PICCOLO);
  char buf[32];
  snprintf(buf, sizeof(buf), "%s  WiFi %s", SHELLY_HOST,
           WiFi.status() == WL_CONNECTED ? "ok" : "assente");
  testoCx(buf, W_SCHERMO / 2, 60, F_MINI);
}

void disegnaSchermo() {
  if (otaInCorso) return;                 // durante l'OTA comanda schermataOTA()
  u8g2.clearBuffer();

  if (!datiValidi)     paginaOffline();
  else if (allarme)    paginaTotale();    // in allarme resta fissa sulla pagina 1
  else switch (pagina) {
    case 0:  paginaTotale();  break;
    case 1:  paginaRami();    break;
    default: paginaEnergia(); break;
  }

  // in allarme lo schermo lampeggia in negativo
  if (allarme && lampeggio) {
    u8g2.setDrawColor(2);
    u8g2.drawBox(0, 0, W_SCHERMO, H_SCHERMO);
    u8g2.setDrawColor(1);
  }
  u8g2.sendBuffer();
}

// ======================================================================
//  LETTURA DALLO SHELLY EM
// ======================================================================
// cerca "chiave":numero tra le posizioni da..a della risposta JSON
bool campo(const String& r, int da, int a, const char* chiave, double& out) {
  String k = String("\"") + chiave + "\":";
  int p = r.indexOf(k, da);
  if (p < 0 || p >= a) return false;
  const char* s = r.c_str() + p + k.length();
  if (*s == 'n') return false;                    // null
  out = atof(s);
  return true;
}

// estrae l'oggetto n-esimo dell'array "emeters"
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
  if (pe < 0) { Serial.println("[SHELLY] risposta senza emeters: e' uno Shelly EM Gen1?"); return false; }

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
  // totale = somma dei canali validi (una pinza staccata non manda tutto OFFLINE)
  if (c1.ok || c2.ok) wTotale = (c1.ok ? c1.w : 0) + (c2.ok ? c2.w : 0);
  else                wTotale = NAN;
  if (!isnan(c1.kwh)) kCasa    = c1.kwh;
  if (!isnan(c2.kwh)) kCantina = c2.kwh;

  // ora dello Shelly
  double ut;
  if (campo(r, 0, r.length(), "unixtime", ut)) sincronizzaDaShelly((uint32_t)ut);

  int16_t a; uint8_t m, g;
  if (aggiornaOrologio(&a, &m, &g)) aggiornaCalendario(a, m, g);

  // picco della giornata
  if (!isnan(wTotale) && wTotale > dati.picco) {
    dati.picco = wTotale;
    strncpy(dati.piccoOra, oraStr, sizeof(dati.piccoOra) - 1);
    dati.piccoOra[sizeof(dati.piccoOra) - 1] = 0;
  }
  return !isnan(wTotale);
}

// ======================================================================
//  PAGINA WEB
// ======================================================================
static const char PAGINA[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Monitor consumi</title><style>
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
<header><span id="dot"></span><h1>Monitor consumi</h1><span id="ora" style="color:#8b949e;font-size:13px"></span></header>
<main>
<div id="all">SOVRACCARICO</div>

<div class="griglia">
  <div class="card big" style="grid-column:1/-1"><div class="et">Potenza totale</div><div class="vl"><span id="tot">--</span><span class="um">W</span></div></div>
  <div class="card"><div class="et n1">Ramo 1</div><div class="vl"><span id="casa">--</span><span class="um">W</span></div><div class="et" style="margin-top:4px">cos&phi; <span id="pf1">--</span></div></div>
  <div class="card" id="card2"><div class="et n2">Ramo 2</div><div class="vl"><span id="cant">--</span><span class="um">W</span></div><div class="et" style="margin-top:4px">cos&phi; <span id="pf2">--</span></div></div>
  <div class="card"><div class="et">Tensione di rete</div><div class="vl"><span id="vv" class="n">--</span><span class="um">V</span></div></div>
  <div class="card"><div class="et">Picco di oggi</div><div class="vl"><span id="pk" class="n">--</span><span class="um">W</span></div><div class="et" style="margin-top:4px">alle <span id="pko">--:--</span></div></div>
</div>

<h2>Energia</h2>
<div class="griglia">
  <div class="card"><div class="et">Oggi</div><div class="vl"><span id="og">--</span><span class="um">kWh</span></div><div class="et" style="margin-top:6px"><span class="n1">ramo 1</span> <span id="ogc">--</span> &middot; <span class="n2">ramo 2</span> <span id="ogk">--</span></div></div>
  <div class="card"><div class="et">Mese corrente</div><div class="vl"><span id="me">--</span><span class="um">kWh</span></div><div class="et" style="margin-top:6px"><span class="n1">ramo 1</span> <span id="mec">--</span> &middot; <span class="n2">ramo 2</span> <span id="mek">--</span></div></div>
</div>

<h2>Consumo giornaliero</h2>
<div class="gr"><div id="cg"></div>
<div class="leg"><span><i style="background:#388bfd"></i><span class="n1">Ramo 1</span></span><span><i style="background:#2ea043"></i><span class="n2">Ramo 2</span></span><span id="tg"></span></div></div>

<h2>Consumo mensile</h2>
<div class="gr"><div id="cm"></div>
<div class="leg"><span><i style="background:#388bfd"></i><span class="n1">Ramo 1</span></span><span><i style="background:#2ea043"></i><span class="n2">Ramo 2</span></span><span id="tm"></span></div></div>

<h2>Soglia di allarme</h2>
<div class="card"><div class="riga">
  <input type="number" id="sg" min="500" max="20000" step="50">
  <span class="n">W</span><button onclick="salva()">Salva</button>
  <span id="msg" class="n"></span>
</div></div>

<footer id="ft"></footer>
</main>
<script>
const $=i=>document.getElementById(i);
const cls=w=>w<=2000?'v':(w<=3000?'g':'r');
const nf=(v,d)=>v==null||isNaN(v)?'--':v.toFixed(d);
let N1='Ramo 1',N2='Ramo 2';

function barre(cont,voci,etich){
  if(!voci.length){cont.innerHTML='<p style="color:#6e7681;font-size:13px;margin:8px 4px">Nessun dato ancora registrato.</p>';return 0;}
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
    $('ft').innerHTML=`IP ${d.ip} &middot; WiFi ${d.rssi} dBm &middot; acceso da ${Math.floor(d.up/3600)}h ${Math.floor(d.up%3600/60)}m<br>Shelly EM ${d.sh} &middot; soglia allarme ${d.soglia} W &middot; storico ${d.ng} giorni / ${d.nm} mesi`;
  }catch(e){ $('dot').className='ko'; }
}

async function storico(){
  try{
    const d=await (await fetch('/api/storico')).json();
    const sg=barre($('cg'),d.giorni,d.giorni.map(v=>v.d));
    const sm=barre($('cm'),d.mesi,d.mesi.map(v=>v.m));
    $('tg').textContent=d.giorni.length?`totale periodo ${sg.toFixed(1)} kWh`:'';
    $('tm').textContent=d.mesi.length?`totale periodo ${sm.toFixed(0)} kWh`:'';
  }catch(e){}
}

async function salva(){
  const v=parseInt($('sg').value);
  if(!(v>=500&&v<=20000)){$('msg').textContent='valore non valido';return;}
  const r=await fetch('/api/soglia?w='+v,{method:'POST'});
  $('msg').textContent=r.ok?'salvata':'errore';
  setTimeout(()=>$('msg').textContent='',2500);
  live();
}

live().then(storico);
setInterval(live,2000);
setInterval(storico,60000);
</script></body></html>)rawliteral";

// ======================================================================
//  API DEL WEBSERVER
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
  // il giorno in corso, ancora aperto
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
  if (!server.hasArg("w")) { server.send(400, "text/plain", "manca w"); return; }
  float v = server.arg("w").toFloat();
  if (v < 500 || v > 20000) { server.send(400, "text/plain", "fuori intervallo"); return; }
  dati.soglia = v;
  salvaDati();
  Serial.printf("[WEB] nuova soglia %.0f W\n", v);
  server.send(200, "application/json", "{\"ok\":true}");
}

void avviaWebServer() {
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", PAGINA); });
  server.on("/api/live",    HTTP_GET, apiLive);
  server.on("/api/storico", HTTP_GET, apiStorico);
  server.on("/api/soglia",  HTTP_ANY, apiSoglia);
  server.onNotFound([]() { server.send(404, "text/plain", "non trovato"); });
  server.begin();
  Serial.println("[WEB] server avviato sulla porta 80");
}

// ======================================================================
//  OTA
// ======================================================================
void schermataOTA(const char* titolo, int perc) {
  u8g2.clearBuffer();
  u8g2.setFont(F_MINI);
  u8g2.drawStr(0, Y_TESTA, "AGGIORNAMENTO OTA");
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
    schermataOTA("in corso", perc);
  });

  ArduinoOTA.onEnd([]() { schermataOTA("Completato", 100); });

  ArduinoOTA.onError([](ota_error_t e) {
    const char* m = "Errore sconosciuto";
    if      (e == OTA_AUTH_ERROR)    m = "Password errata";
    else if (e == OTA_BEGIN_ERROR)   m = "Avvio fallito";
    else if (e == OTA_CONNECT_ERROR) m = "Connessione persa";
    else if (e == OTA_RECEIVE_ERROR) m = "Ricezione fallita";
    else if (e == OTA_END_ERROR)     m = "Chiusura fallita";
    schermataOTA(m, -1);
    delay(4000);
    ESP.restart();
  });

  ArduinoOTA.begin();
  otaAttivo = true;
  Serial.println("[OTA] pronto");
}

// ======================================================================
//  WIFI
// ======================================================================
void avviaWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(OTA_HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] connessione");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) { delay(300); Serial.print('.'); }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) Serial.println("[WiFi] IP " + WiFi.localIP().toString());
  else Serial.println("[WiFi] timeout, riprovo in background");
}

void avviaServizi() {
  if (MDNS.begin(OTA_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("[mDNS] http://" OTA_HOSTNAME ".local");
  }
  configTzTime(FUSO_ORARIO, NTP_SERVER);   // riserva: se c'e' Internet l'ora arriva anche da qui
  avviaOTA();
  avviaWebServer();
}

// schermata di avvio
void schermataAvvio(const char* riga2, const char* riga3) {
  u8g2.clearBuffer();
  testoCx("Monitor consumi", W_SCHERMO / 2, 18, F_PICCOLO);
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
  u8g2.setI2CAddress(OLED_ADDR * 2);      // U8g2 vuole l'indirizzo gia' shiftato
  u8g2.begin();
  u8g2.setBusClock(I2C_HZ);
  u8g2.setContrast(255);
  schermataAvvio("avvio...", NULL);

  caricaDati();
  avviaWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    avviaServizi();
    schermataAvvio("WiFi connesso", WiFi.localIP().toString().c_str());
    delay(1500);
  } else {
    schermataAvvio("WiFi assente", "riprovo in background");
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

  // --- riconnessione WiFi non bloccante ---
  static unsigned long tWiFi = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - tWiFi > 10000) {
    tWiFi = millis();
    Serial.println("[WiFi] riconnessione");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
  if (!otaAttivo && WiFi.status() == WL_CONNECTED) avviaServizi();

  // --- lettura periodica dello Shelly EM ---
  if (millis() - tUltimaLettura >= PERIODO_LETTURA) {
    tUltimaLettura = millis();
    bool ok = (WiFi.status() == WL_CONNECTED) ? leggiShelly() : false;
    if (ok) erroriDiFila = 0;
    else if (erroriDiFila < 255) erroriDiFila++;
    // una lettura persa non fa sparire i valori: si va OFFLINE solo dopo qualche errore di fila
    datiValidi = (erroriDiFila < ERRORI_OFFLINE);
    if (!datiValidi) { wTotale = NAN; wCasa = NAN; wCantina = NAN; tensione = NAN; pf1 = NAN; pf2 = NAN; }
    if (!ok) aggiornaOrologio(nullptr, nullptr, nullptr);   // l'orologio va avanti lo stesso

    if (!isnan(wTotale)) {
      if (!allarme && wTotale > dati.soglia)                allarme = true;
      else if (allarme && wTotale < dati.soglia - ISTERESI) allarme = false;
    } else allarme = false;
  }

  // --- rotazione automatica delle pagine ---
  if (allarme || !datiValidi) {
    tUltimaPagina = millis();                 // in allarme o offline niente rotazione
  } else if (millis() - tUltimaPagina >= PERIODO_PAGINA) {
    tUltimaPagina = millis();
    pagina = (pagina + 1) % N_PAGINE;
  }

  // --- lampeggio in allarme ---
  if (millis() - tUltimoLampeggio >= 500) {
    tUltimoLampeggio = millis();
    lampeggio = allarme ? !lampeggio : false;
  }

  // --- ridisegno del display ---
  if (millis() - tUltimoSchermo >= PERIODO_SCHERMO) {
    tUltimoSchermo = millis();
    disegnaSchermo();
  }

  // --- salvataggio periodico del picco ---
  if (millis() - tUltimoSalva >= PERIODO_SALVA) salvaDati();

  gestisciBuzzer();
}
