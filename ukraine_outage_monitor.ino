// Монітор графіка відключень світла на ESP32-C3 Super Mini + ST7789 240x240.
//
// Годинник, день тижня, дата, стан світла зараз, наступна подія та 24-годинний графік
// для вибраного населеного пункту й черги. Графік перевіряється раз на 10 хвилин.
// Wi-Fi, часовий пояс, населений пункт і черга вводяться двома кнопками та запам'ятовуються.
//
// Докладніше: README.md. Налаштування пінів: config.h. Джерела даних: sources.h.

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <time.h>
#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

#include "config.h"
#include "types.h"

// ---------- Кольори (RGB565) ----------
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
const uint16_t COL_BG    = 0x0000;
const uint16_t COL_LINE  = rgb565(60, 60, 70);
const uint16_t COL_TIME  = rgb565(255, 90, 200);
const uint16_t COL_DAY   = rgb565(255, 255, 255);
const uint16_t COL_TEXT  = rgb565(200, 200, 210);
const uint16_t COL_DIM   = rgb565(130, 130, 140);
const uint16_t COL_FEELS = rgb565(255, 210, 60);
const uint16_t COL_RAIN  = rgb565(80, 200, 255);
const uint16_t COL_OK    = rgb565(60, 220, 90);
const uint16_t COL_WARN  = rgb565(255, 170, 0);
const uint16_t COL_BAD   = rgb565(255, 70, 70);
// клітинки графіка
const uint16_t COL_S_ON    = rgb565(0, 170, 60);
const uint16_t COL_S_OFF   = rgb565(215, 40, 40);
const uint16_t COL_S_MAYBE = rgb565(230, 170, 0);
const uint16_t COL_S_NONE  = rgb565(50, 50, 55);

Adafruit_ST7789 tft(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

#include "ukr.h"
#include "wizard.h"
#include "schedule.h"
#include "places.h"

// ---------- Стан ----------
Preferences prefs;
String gTz;
PlaceCfg gPlace;

SemaphoreHandle_t gMutex = nullptr;
Schedule gSched;                 // останній отриманий графік (під gMutex)
uint32_t gOkMs = 0;              // millis() останнього успішного запиту
bool gEverOk = false;
bool gFetchFailed = false;
char gErr[48] = "";
uint32_t gChangedMs = 0;         // коли графік востаннє змінився
uint32_t gVer = 0;               // лічильник оновлень (для перемальовування графіка)

volatile bool gPauseNet = false;  // меню відкрите: фонове завантаження не чіпає Wi-Fi
volatile bool gNetBusy = false;
volatile bool gForce = false;     // оновити зараз
uint32_t gLastManual = 0;

int gBrightness = 100;  // відсотків
bool ntpStarted = false;

// ---------- Підсвітка ----------
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
#define BL_PWM_V3 1  // нове API ledcAttach(pin, ...)
#endif
constexpr uint8_t BL_PWM_CHANNEL = 0;  // потрібен лише для ядра 2.x

void backlightBegin() {
  if (PIN_TFT_BL < 0) return;
#ifdef BL_PWM_V3
  ledcAttach(PIN_TFT_BL, 5000, 8);
#else
  ledcSetup(BL_PWM_CHANNEL, 5000, 8);
  ledcAttachPin(PIN_TFT_BL, BL_PWM_CHANNEL);
#endif
}

void backlightSet(int percent) {
  if (PIN_TFT_BL < 0) return;
  if (percent < 5) percent = 5;
  if (percent > 100) percent = 100;
  int duty = percent * 255 / 100;
  if (!TFT_BL_ACTIVE_HIGH) duty = 255 - duty;
#ifdef BL_PWM_V3
  ledcWrite(PIN_TFT_BL, duty);
#else
  ledcWrite(BL_PWM_CHANNEL, duty);
#endif
}

// ---------- Поля тексту ----------
Field fTime  = {8, 40, &FreeSansBold24pt7b};
Field fSec   = {142, 40, &FreeSans12ptCyr};
Field fDay   = {4, 66, &FreeSans12ptCyr};
Field fDate  = {120, 66, &FreeSans12ptCyr};
Field fQueue = {8, 118, &FreeSans9ptCyr};
Field fUpd   = {120, 118, &FreeSans9ptCyr};
Field fSt1   = {8, 148, &FreeSans12ptCyr};
Field fSt2   = {8, 167, &FreeSans9ptCyr};
Field fSt3   = {8, 184, &FreeSans9ptCyr};
Field* const ALL_FIELDS[] = {&fTime, &fSec, &fDay, &fDate, &fQueue, &fUpd, &fSt1, &fSt2, &fSt3};

// Перемальовує поле лише якщо змінились текст або колір; стирає тільки свою область.
void setField(Field& f, const char* s, uint16_t color) {
  if (f.drawn && f.lastColor == color && strcmp(f.last, s) == 0) return;
  tft.setFont(f.font);
  int16_t x1, y1;
  uint16_t w, h;
  boundsU(s, f.x, f.y, &x1, &y1, &w, &h);
  if (f.drawn) tft.fillRect(f.bx, f.by, f.bw, f.bh, COL_BG);
  f.bx = x1 - 1;
  f.by = y1 - 1;
  f.bw = w + 2;
  f.bh = h + 2;
  tft.setTextColor(color);
  tft.setCursor(f.x, f.y);
  printU(s);
  strlcpy(f.last, s, sizeof f.last);
  f.lastColor = color;
  f.drawn = true;
}

// Текст, вирівняний по правому краю (xRight)
void setFieldRight(Field& f, int xRight, const char* s, uint16_t color) {
  tft.setFont(f.font);
  int16_t x1, y1;
  uint16_t w, h;
  boundsU(s, 0, 0, &x1, &y1, &w, &h);
  f.x = xRight - (int)w - x1;
  setField(f, s, color);
}

// Розташування годинника й дати (як у погодній станції)
void layoutClock() {
  int16_t x1, y1;
  uint16_t wT, wS, h;
  tft.setFont(&FreeSansBold24pt7b);
  boundsU("00:00", 0, 0, &x1, &y1, &wT, &h);
  tft.setFont(&FreeSans12ptCyr);
  boundsU("00", 0, 0, &x1, &y1, &wS, &h);
  int total = wT + 6 + wS;
  fTime.x = (240 - total) / 2;
  fSec.x = fTime.x + wT + 6;
  fSec.y = fTime.y;
  boundsU("00.00.0000", 0, 0, &x1, &y1, &wT, &h);
  fDate.x = 236 - (int)wT - x1;
}

// ---------- Малювання ----------
const char* const DAYS_UA[7] = {"Неділя", "Понеділок", "Вівторок", "Середа", "Четвер", "П'ятниця", "Субота"};
const char* const DAYS_UA_SHORT[7] = {"Нд", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"};

void drawStaticUi() {
  tft.fillScreen(COL_BG);
  tft.drawFastHLine(8, 74, 224, COL_LINE);
  tft.drawFastHLine(8, 124, 224, COL_LINE);
}

int gChartT0 = 0, gChartEl = -1;
uint32_t gChartVer = 0xFFFFFFFF;
char gPlaceShown[48] = "";
Marq gPlaceMq;
uint16_t gLastDot = 0xFFFF;

void resetUi() {
  for (Field* f : ALL_FIELDS) {
    f->drawn = false;
    f->last[0] = 0;
  }
  gChartT0 = 0;
  gChartEl = -1;
  gChartVer = 0xFFFFFFFF;
  gPlaceShown[0] = 0;
  gLastDot = 0xFFFF;
  drawStaticUi();
}

void drawClock() {
  struct tm t;
  char buf[24];
  if (getLocalTime(&t, 0)) {
    snprintf(buf, sizeof buf, "%02d:%02d", t.tm_hour, t.tm_min);
    setField(fTime, buf, COL_TIME);
    snprintf(buf, sizeof buf, "%02d", t.tm_sec);
    setField(fSec, buf, COL_DIM);
    setField(fDay, DAYS_UA[t.tm_wday], COL_DAY);
    snprintf(buf, sizeof buf, "%02d.%02d.%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
    setField(fDate, buf, COL_TIME);
  } else {
    setField(fTime, "--:--", COL_DIM);
    setField(fSec, "", COL_DIM);
  }
}

// Назва населеного пункту: якщо не вміщується, біжить рядком
void renderPlace(int off, bool fits) {
  if (!gCv) {
    tft.fillRect(0, 78, 240, 26, COL_BG);
    char fit[64];
    tft.setFont(&FreeSans12ptCyr);
    fitText(gPlace.name, fit, sizeof fit, 224);
    tft.setTextColor(COL_DAY);
    tft.setCursor(8, 97);
    printU(fit);
    return;
  }
  gCv->fillScreen(COL_BG);
  gCv->setFont(&FreeSans12ptCyr);
  gCv->setTextColor(COL_DAY);
  int x = 4 - off;
  if (fits) x = 4 + (224 - gPlaceMq.textW) / 2;
  gCv->setCursor(x, 19);
  printUTo(*gCv, gPlace.name);
  gCv->fillRect(0, 0, 4, 26, COL_BG);
  gCv->fillRect(228, 0, 4, 26, COL_BG);
  cvBlit(4, 78);
}

void drawPlaceLine() {
  if (strcmp(gPlaceShown, gPlace.name) != 0) {
    strlcpy(gPlaceShown, gPlace.name, sizeof gPlaceShown);
    tft.setFont(&FreeSans12ptCyr);
    marqStart(gPlaceMq, textWidthU(gPlace.name), 224);
    renderPlace(0, !gPlaceMq.active);
  } else if (marqTick(gPlaceMq)) {
    renderPlace(gPlaceMq.off, false);
  }
}

uint16_t slotColor(uint8_t st) {
  switch (st) {
    case SL_ON: return COL_S_ON;
    case SL_OFF: return COL_S_OFF;
    case SL_MAYBE: return COL_S_MAYBE;
  }
  return COL_S_NONE;
}

uint16_t dimColor(uint16_t c) { return (c & 0xF7DE) >> 1; }

// ---------- Графік на 24 години ----------
// Два рядки по 12 годин: перший від поточної пів-години, другий далі. Минулі клітинки не показуються.
constexpr int CH_X0 = 8, CH_W = 224, CH_BAR_H = 12;
const int CH_Y[2] = {192, 217};

void drawChartFull(const Schedule& s, time_t now) {
  time_t t0 = now - (now % SLOT_SEC);
  for (int r = 0; r < 2; r++) {
    int y = CH_Y[r];
    tft.fillRect(0, y - 3, 240, CH_BAR_H + 15, COL_BG);
    for (int k = 0; k < 24; k++) {
      int i = r * 24 + k;
      time_t t = t0 + (time_t)i * SLOT_SEC;
      int x0 = CH_X0 + (k * CH_W) / 24, x1 = CH_X0 + ((k + 1) * CH_W) / 24;
      tft.fillRect(x0, y, x1 - x0, CH_BAR_H, slotColor(slotAt(s, t)));
      struct tm lt;
      localtime_r(&t, &lt);
      if (lt.tm_min == 0) {
        if (k > 0) tft.drawFastVLine(x0, y, CH_BAR_H, COL_BG);  // межа годин
        {  // підпис кожної години
          char lb[4];
          snprintf(lb, sizeof lb, "%02d", lt.tm_hour);
          tft.drawFastVLine(x0, y + CH_BAR_H, 3, COL_DIM);
          tft.setFont(nullptr);
          tft.setTextSize(1);
          tft.setTextColor(lt.tm_hour == 0 ? COL_FEELS : COL_DIM);
          int lx = x0 - 5;
          if (lx < 0) lx = 0;
          tft.setCursor(lx, y + CH_BAR_H + 4);
          tft.print(lb);
        }
      }
    }
  }
}

// Перша клітинка (поточна пів-година): минула частина притемнена, білий маркер "зараз"
void drawNowCell(const Schedule& s, time_t now) {
  time_t t0 = now - (now % SLOT_SEC);
  int y = CH_Y[0];
  int x0 = CH_X0, x1 = CH_X0 + CH_W / 24;
  int w = x1 - x0;
  uint16_t col = slotColor(slotAt(s, t0));
  int el = (int)((now - t0) * w / SLOT_SEC);
  if (el < 0) el = 0;
  if (el > w - 1) el = w - 1;
  tft.fillRect(x0 - 2, y - 3, w + 4, 3, COL_BG);
  tft.fillRect(x0 - 2, y + CH_BAR_H, w + 4, 3, COL_BG);
  if (el > 0) tft.fillRect(x0, y, el, CH_BAR_H, dimColor(col));
  tft.fillRect(x0 + el, y, w - el, CH_BAR_H, col);
  tft.fillRect(x0 + el, y - 3, 2, CH_BAR_H + 6, COL_DAY);
  gChartEl = el;
}

// ---------- Текстові підписи ----------
void fmtCountdown(long sec, char* out, size_t n) {
  long m = (sec + 59) / 60;
  if (m >= 1440) snprintf(out, n, "%ldд %ldг", m / 1440, (m % 1440) / 60);
  else if (m >= 60) snprintf(out, n, "%ldг %02ldхв", m / 60, m % 60);
  else snprintf(out, n, "%ldхв", m < 1 ? 1 : m);
}

// "о 14:00" / "завтра 04:00" / "Нд 04:00" (isEnd: 00:00 показуємо як 24:00 цієї ж доби)
void whenStr(time_t t, time_t now, bool atWord, bool isEnd, char* out, size_t n) {
  struct tm a, b;
  time_t td = t;
  bool mid = false;
  localtime_r(&t, &a);
  if (isEnd && a.tm_hour == 0 && a.tm_min == 0) { td = t - 60; mid = true; }
  localtime_r(&td, &a);
  localtime_r(&now, &b);
  struct tm m = b;  // різниця в календарних днях
  m.tm_hour = m.tm_min = m.tm_sec = 0;
  struct tm d2 = a;
  d2.tm_hour = d2.tm_min = d2.tm_sec = 0;
  long days = (long)((mktime(&d2) - mktime(&m) + 43200L) / 86400L);  // +12 год: стійко до переходу на літній/зимовий час
  struct tm tt;
  localtime_r(&t, &tt);
  int hh = mid ? 24 : tt.tm_hour;
  int mm = mid ? 0 : tt.tm_min;
  if (days == 0) snprintf(out, n, atWord ? "о %02d:%02d" : "%02d:%02d", hh, mm);
  else if (days == 1) snprintf(out, n, "завтра %02d:%02d", hh, mm);
  else snprintf(out, n, "%s %02d:%02d", DAYS_UA_SHORT[a.tm_wday], hh, mm);
}

// ---------- Статус і графік ----------
void drawInfoLine(bool failed, bool stale) {
  char buf[64];
  snprintf(buf, sizeof buf, "Черга %s", queueShort(gPlace.queue));
  setField(fQueue, buf, COL_FEELS);

  uint32_t ms = millis();
  if (failed && gErr[0]) {
    setFieldRight(fUpd, 232, "Помилка", COL_BAD);  // подробиці нижче, щоб текст не накладався на чергу
  } else if (gEverOk && gChangedMs && (ms - gChangedMs) < CHANGED_SHOW_MS) {
    setFieldRight(fUpd, 232, "Графік оновлено", COL_OK);
  } else if (gEverOk) {
    time_t now = time(nullptr);
    time_t at = now - (ms - gOkMs) / 1000;
    struct tm t;
    localtime_r(&at, &t);
    snprintf(buf, sizeof buf, "Оновл. %02d:%02d", t.tm_hour, t.tm_min);
    setFieldRight(fUpd, 232, buf, stale ? COL_WARN : COL_DIM);
  } else {
    setFieldRight(fUpd, 232, "", COL_DIM);
  }
}

void drawStatus() {
  Schedule s;
  bool failed, everOk;
  uint32_t okMs, ver;
  time_t now = time(nullptr);
  xSemaphoreTake(gMutex, portMAX_DELAY);
  if (timeIsValid(now)) pruneSchedule(gSched, now);  // прибираємо застарілі доби
  s = gSched;
  failed = gFetchFailed;
  everOk = gEverOk;
  okMs = gOkMs;
  ver = gVer;
  xSemaphoreGive(gMutex);

  bool stale = everOk && (millis() - okMs) > STALE_AFTER_MS;
  drawInfoLine(failed, stale);

  // Індикатор у куті: червоний немає Wi-Fi, жовтий дані застаріли або помилка, зелений усе гаразд
  uint16_t dot = (WiFi.status() != WL_CONNECTED) ? COL_BAD : ((everOk && !stale && !failed) ? COL_OK : COL_WARN);
  if (dot != gLastDot) {
    tft.fillCircle(228, 10, 4, dot);
    gLastDot = dot;
  }

  if (!timeIsValid(now)) {
    setField(fSt1, "Синхронізація часу...", COL_DIM);
    setField(fSt2, "", COL_TEXT);
    setField(fSt3, "", COL_DIM);
    return;
  }

  char l2[64] = "", l3[64] = "", w[32], cd[32];
  const char* l1 = "";
  uint16_t c1 = COL_DIM, c2 = COL_TEXT;

  if (!s.valid) {
    if (!everOk && !failed) {
      l1 = "Завантаження...";
    } else if (failed) {
      l1 = "Немає даних";
      strlcpy(l2, gErr, sizeof l2);
      c2 = COL_BAD;
    } else {
      l1 = "Немає даних";
      strlcpy(l2, "Графік не опубліковано", sizeof l2);
    }
  } else {
    uint8_t cur = slotAt(s, now);
    NextEvent ev = findNextChange(s, now, cur);
    if (cur == SL_NONE) {
      l1 = "Немає даних";
      strlcpy(l2, "Для цього часу графіка нема", sizeof l2);
    } else {
      if (cur == SL_ON) { l1 = "Світло є"; c1 = COL_OK; }
      else if (cur == SL_OFF) { l1 = "Світла немає"; c1 = COL_BAD; }
      else { l1 = "Можливе вимкнення"; c1 = COL_WARN; }

      if (ev.found) {
        uint8_t nx = slotAt(s, ev.t);
        whenStr(ev.t, now, true, false, w, sizeof w);
        if (cur == SL_ON) snprintf(l2, sizeof l2, nx == SL_MAYBE ? "Можливо %s" : "Вимкнення %s", w);
        else if (cur == SL_OFF) snprintf(l2, sizeof l2, "Увімкнення %s", w);
        else snprintf(l2, sizeof l2, "Далі зміна %s", w);
        fmtCountdown((long)(ev.t - now), cd, sizeof cd);
        snprintf(l3, sizeof l3, "через %s", cd);
      } else if (ev.unknown) {
        whenStr(ev.t, now, false, true, w, sizeof w);
        if (cur == SL_ON) snprintf(l2, sizeof l2, "Без відключень до %s", w);
        else if (cur == SL_OFF) snprintf(l2, sizeof l2, "Вимкнено до %s", w);
        else snprintf(l2, sizeof l2, "Можливо до %s", w);
        strlcpy(l3, "далі графіка ще немає", sizeof l3);
      } else {
        strlcpy(l2, cur == SL_ON ? "Відключень не заплановано" : "Без змін понад 48 год", sizeof l2);
      }
    }
  }
  setField(fSt1, l1, c1);
  setField(fSt2, l2, c2);
  setField(fSt3, l3, COL_DIM);

  // Графік: повністю при зміні даних чи пів-години, маркер "зараз" при зсуві
  if (s.valid || everOk) {
    int t0 = (int)(now - (now % SLOT_SEC));
    if (gChartT0 != t0 || gChartVer != ver) {
      drawChartFull(s, now);
      gChartT0 = t0;
      gChartVer = ver;
      drawNowCell(s, now);
    } else {
      int el = (int)((now - t0) * (CH_W / 24) / SLOT_SEC);
      if (el != gChartEl) drawNowCell(s, now);
    }
  }
}

// ---------- Wi-Fi: перепідключення у фоні ----------
enum KState : uint8_t { K_IDLE, K_SCANNING, K_CONNECTING };
KState kState = K_IDLE;
uint32_t kLostMs = 0, kStateMs = 0;
bool kBoot = true;
int kCand[MAX_SAVED];
int kCandN = 0, kCandPos = 0;

void wifiKeeper() {
  uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    kState = K_IDLE;
    kLostMs = 0;
    kBoot = false;
    return;
  }
  if (gPauseNet) return;  // зараз працює майстер Wi-Fi
  if (kState == K_IDLE) {
    if (!kBoot) {
      if (kLostMs == 0) { kLostMs = now; return; }
      if (now - kLostMs < 30000) return;  // даємо драйверу самому відновити зв'язок
    }
    WiFi.scanNetworks(true);
    kState = K_SCANNING;
    kStateMs = now;
  } else if (kState == K_SCANNING) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING && now - kStateMs < 15000) return;
    kCandN = 0;
    if (n > 0) {
      for (int i = 0; i < n && kCandN < MAX_SAVED; i++) {
        String ss = WiFi.SSID(i);
        int slot = wifiStoreFind(ss.c_str());
        if (slot >= 0) kCand[kCandN++] = slot;
      }
    }
    WiFi.scanDelete();
    kCandPos = 0;
    if (kCandN == 0) { kState = K_IDLE; kLostMs = now; kBoot = false; return; }
    SavedNet sn;
    if (wifiStoreLoad(kCand[0], sn)) WiFi.begin(sn.ssid, sn.pass[0] ? sn.pass : nullptr);
    kState = K_CONNECTING;
    kStateMs = now;
  } else if (kState == K_CONNECTING) {
    if (now - kStateMs < 15000) return;
    kCandPos++;
    if (kCandPos < kCandN) {
      SavedNet sn;
      if (wifiStoreLoad(kCand[kCandPos], sn)) WiFi.begin(sn.ssid, sn.pass[0] ? sn.pass : nullptr);
      kStateMs = now;
    } else {
      WiFi.disconnect();
      kState = K_IDLE;
      kLostMs = now;
      kBoot = false;
    }
  }
}

// ---------- Фонове завантаження графіка ----------
void resetSchedule() {
  xSemaphoreTake(gMutex, portMAX_DELAY);
  memset(&gSched, 0, sizeof gSched);
  gEverOk = false;
  gFetchFailed = false;
  gErr[0] = 0;
  gChangedMs = 0;
  gVer++;
  xSemaphoreGive(gMutex);
}

void scheduleTask(void*) {
  uint32_t nextMs = 0;
  bool first = true;
  for (;;) {
    if (gPauseNet || WiFi.status() != WL_CONNECTED || !gPlace.valid) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (!first && !gForce && (int32_t)(millis() - nextMs) < 0) {
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }
    first = false;
    gForce = false;
    gNetBusy = true;
    Schedule s;
    char err[48] = "";
    PlaceCfg pc = gPlace;
    FetchResult r = fetchSchedule(pc, s, err, sizeof err);
    gNetBusy = false;

    xSemaphoreTake(gMutex, portMAX_DELAY);
    if (r == FETCH_FAIL) {
      gFetchFailed = true;
      strlcpy(gErr, err[0] ? err : "Помилка завантаження", sizeof gErr);
    } else {
      gFetchFailed = false;
      gErr[0] = 0;
      gOkMs = millis();
      bool changed = (r == FETCH_OK) && gEverOk && (s.hash != gSched.hash);
      if (r == FETCH_OK) {
        gSched = s;
      } else {
        memset(&gSched, 0, sizeof gSched);  // джерело працює, але для черги графіка немає
      }
      if (changed) gChangedMs = millis();
      gEverOk = true;
      gVer++;
    }
    xSemaphoreGive(gMutex);
    Serial.printf("fetch: %d %s\n", (int)r, err);
    nextMs = millis() + (r == FETCH_FAIL ? RETRY_INTERVAL_MS : SCHEDULE_INTERVAL_MS);
  }
}

// ---------- Кнопки ----------
void drainButtons() {
  uint32_t t = millis();
  while (millis() - t < 300) {
    pollButton(btnA);
    pollButton(btnB);
    if (btnA.down || btnB.down) t = millis();
    delay(5);
  }
}

bool bothHeldLong() {
  return btnA.down && btnB.down && !btnA.longFired && !btnB.longFired &&
         (millis() - (btnA.pressMs > btnB.pressMs ? btnA.pressMs : btnB.pressMs)) >= LONG_PRESS_MS;
}

// Яскравість: A плюс, B мінус, пауза закриває; значення зберігається
void brightnessMode() {
  btnA.longFired = btnB.longFired = true;
  tft.fillRect(0, 205, 240, 35, COL_BG);
  uint32_t last = millis();
  int shown = -1;
  while (millis() - last < BL_ADJUST_TIMEOUT_MS) {
    BtnEvent ea = pollButton(btnA);
    BtnEvent eb = pollButton(btnB);
    if (ea == EV_SHORT) { gBrightness = min(100, gBrightness + 10); last = millis(); }
    if (eb == EV_SHORT) { gBrightness = max(5, gBrightness - 10); last = millis(); }
    if (gBrightness != shown) {
      shown = gBrightness;
      backlightSet(gBrightness);
      char b[32];
      snprintf(b, sizeof b, "Яскравість %d%%  A+ B-", gBrightness);
      tft.fillRect(0, 205, 240, 35, COL_BG);
      tft.setFont(&FreeSans9ptCyr);
      tft.setTextColor(COL_TEXT);
      centerText(b, 226);
    }
    delay(10);
  }
  prefs.putInt("bl", gBrightness);
  tft.fillRect(0, 205, 240, 35, COL_BG);
  gChartT0 = 0;  // перемалювати графік
  drainButtons();
}

// ---------- Налаштування ----------
void applyTz() {
  setenv("TZ", gTz.c_str(), 1);
  tzset();
}

void menuLabel(int i, char* out, size_t n) {
  static const char* const L[] = {"Населений пункт і черга", "Мережа Wi-Fi", "Часовий пояс", "Оновити графік зараз", "Назад"};
  strlcpy(out, L[i], n);
}

void settingsMenu() {
  drainButtons();
  gPauseNet = true;
  uint32_t t0 = millis();
  while (gNetBusy && millis() - t0 < 20000) delay(50);

  int s = listMenu("Налаштування", 5, menuLabel, 0);
  if (s == 0) {
    PlaceCfg pc;
    if (runPlaceWizard(prefs, pc, true)) {
      gPlace = pc;
      resetSchedule();
      gForce = true;
    }
  } else if (s == 1) {
    if (!wizardWifi(true)) {  // користувач передумав: Wi-Fi вже від'єднано пошуком, підключаємось знову
      kState = K_IDLE;
      kBoot = true;
    }
  } else if (s == 2) {
    wizardTz(prefs);
    gTz = prefs.getString("tz", DEFAULT_TZ_INFO);
    applyTz();
  } else if (s == 3) {
    gForce = true;
  }
  drainButtons();
  resetUi();
  gPauseNet = false;
}

// Очікування підключення до Wi-Fi (для першого налаштування, коли мережі вже відомі)
bool waitWifi(uint32_t timeoutMs) {
  showMessage("Підключення до Wi-Fi...", nullptr, COL_TEXT);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    wifiKeeper();
    delay(50);
  }
  return WiFi.status() == WL_CONNECTED;
}

// ---------- Запуск ----------
void setup() {
  Serial.begin(115200);
  backlightBegin();
  backlightSet(0);

  SPI.begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI, PIN_TFT_CS);
  tft.init(240, 240, TFT_SPI_MODE);
  tft.setRotation(TFT_ROTATION);
  tft.setTextWrap(false);
  tft.fillScreen(COL_BG);
  layoutClock();
  cvBegin();
  buttonsBegin();
  gMutex = xSemaphoreCreateMutex();

  prefs.begin("station", false);
  gBrightness = prefs.getInt("bl", 100);
  backlightSet(gBrightness);

  bool forceSetup = bothButtonsHeld();  // утримання обох кнопок при вмиканні: повне налаштування
  wifiStoreBegin();
  gTz = prefs.getString("tz", "");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (WIFI_LOW_TX_POWER) WiFi.setTxPower(WIFI_POWER_8_5dBm);

  if (forceSetup || gTz.length() == 0 || wifiStoreCount() == 0) {
    runSetupWizard(prefs);
    gTz = prefs.getString("tz", DEFAULT_TZ_INFO);
  }
  applyTz();

  bool havePlace = loadPlace(prefs, gPlace);
  if (forceSetup || !havePlace) {
    if (WiFi.status() != WL_CONNECTED) waitWifi(25000);  // потрібен для запиту списку черг
    runPlaceWizard(prefs, gPlace, false);
  }

  resetUi();
  memset(&gSched, 0, sizeof gSched);
  xTaskCreate(scheduleTask, "sched", 20480, nullptr, 1, nullptr);
}

void loop() {
  if (!ntpStarted && WiFi.status() == WL_CONNECTED) {
    configTzTime(gTz.c_str(), "pool.ntp.org", "time.google.com", "ua.pool.ntp.org");
    ntpStarted = true;
  }
  wifiKeeper();

  BtnEvent ea = pollButton(btnA);
  BtnEvent eb = pollButton(btnB);
  (void)eb;
  if (PIN_TFT_BL >= 0 && bothHeldLong()) {
    brightnessMode();
  } else if (ea == EV_LONG && !btnB.down) {
    settingsMenu();
  } else if (ea == EV_SHORT && !btnB.down && millis() - gLastManual > 15000) {
    gLastManual = millis();  // коротке натискання A: перевірити графік зараз
    gForce = true;
  }

  static uint32_t lastDraw = 0;
  uint32_t ms = millis();
  drawPlaceLine();
  if (ms - lastDraw >= 200) {
    lastDraw = ms;
    drawClock();
    drawStatus();
  }
  delay(10);
}
