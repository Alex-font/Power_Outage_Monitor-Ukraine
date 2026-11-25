#pragma once
// Меню, кнопки та майстри налаштування (Wi-Fi, пароль, часовий пояс) двома кнопками.
// Підключається з основного скетча ПІСЛЯ оголошення tft, кольорів COL_* і шрифтів.
//
// Кнопка A: далі / вниз / наступний символ.   Кнопка B: назад / вгору.
// Довге утримання (LONG_PRESS_MS) виконує додаткову дію (див. README).

#include <Preferences.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include "timezones.h"
#include "wifistore.h"

constexpr uint16_t COL_SEL = rgb565(0, 70, 140);

// ---------- Кнопки ----------
enum BtnEvent : uint8_t { EV_NONE, EV_SHORT, EV_LONG };

struct Button {
  uint8_t pin;
  bool raw;        // останній "сирий" стан піна (true = натиснуто)
  bool down;       // стабільний стан після усунення брязкоту
  bool longFired;  // довге утримання вже спрацювало, відпускання ігноруємо
  uint32_t changedMs;
  uint32_t pressMs;
};

Button btnA = {PIN_BTN_A, false, false, false, 0, 0};
Button btnB = {PIN_BTN_B, false, false, false, 0, 0};

void buttonsBegin() {
  pinMode(PIN_BTN_A, INPUT_PULLUP);
  pinMode(PIN_BTN_B, INPUT_PULLUP);
}

// Коротке натискання повертається при відпусканні, довге спрацьовує під час утримання.
static BtnEvent pollButton(Button& b) {
  uint32_t now = millis();
  bool raw = (digitalRead(b.pin) == LOW);
  if (raw != b.raw) {
    b.raw = raw;
    b.changedMs = now;
  }
  if (raw != b.down && (now - b.changedMs) >= BTN_DEBOUNCE_MS) {
    b.down = raw;
    if (raw) {
      b.pressMs = now;
      b.longFired = false;
    } else if (!b.longFired) {
      return EV_SHORT;
    }
    return EV_NONE;
  }
  if (b.down && !b.longFired && (now - b.pressMs) >= LONG_PRESS_MS) {
    b.longFired = true;
    return EV_LONG;
  }
  return EV_NONE;
}

// Обидві кнопки утримуються під час вмикання (перевіряємо ~300 мс)
bool bothButtonsHeld() {
  for (int i = 0; i < 6; i++) {
    if (digitalRead(PIN_BTN_A) != LOW || digitalRead(PIN_BTN_B) != LOW) return false;
    delay(50);
  }
  return true;
}

// ---------- Допоміжне малювання ----------
int gHoldW = -1;    // поточна ширина смуги утримання
int gCommitW = -1;  // поточна ширина смуги автофіксації символу

static void centerText(const char* s, int y) {  // шрифт встановлюється перед викликом
  int16_t x1, y1;
  uint16_t w, h;
  boundsU(s, 0, y, &x1, &y1, &w, &h);
  tft.setCursor((240 - (int)w) / 2, y);
  printU(s);
}

static void rightText(const char* s, int xRight, int y) {  // шрифт встановлюється перед викликом
  int16_t x1, y1;
  uint16_t w, h;
  boundsU(s, 0, y, &x1, &y1, &w, &h);
  tft.setCursor(xRight - (int)w, y);
  printU(s);
}

static void showMessage(const char* l1, const char* l2, uint16_t color) {
  tft.fillScreen(COL_BG);
  tft.setFont(&FreeSans12ptCyr);
  tft.setTextColor(color);
  centerText(l1, 110);
  if (l2) {
    tft.setFont(&FreeSans9ptCyr);
    tft.setTextColor(COL_TEXT);
    centerText(l2, 140);
  }
}

static float holdFraction(const Button& b) {
  if (!b.down || b.longFired) return 0;
  float f = (float)(millis() - b.pressMs) / (float)LONG_PRESS_MS;
  return f > 1 ? 1 : f;
}

// Смуга прогресу утримання (з'являється після ~10% часу, щоб не мигати при коротких натисканнях)
static void drawHoldBar(int16_t y) {
  float fa = holdFraction(btnA), fb = holdFraction(btnB);
  float f = fa > fb ? fa : fb;
  int w = (f < 0.1f) ? 0 : (int)(f * 224);
  w = (w / 4) * 4;
  if (w == gHoldW) return;
  gHoldW = w;
  tft.fillRect(8, y, 224, 6, COL_BG);
  if (w > 0) tft.fillRect(8, y, w, 6, COL_WARN);
}

static void waitButtonsReleased() {
  if (digitalRead(PIN_BTN_A) == LOW || digitalRead(PIN_BTN_B) == LOW) {
    showMessage(T("Release buttons", "Відпустіть кнопки"), nullptr, COL_WARN);
  }
  uint32_t t = millis();
  while (millis() - t < 300) {
    if (digitalRead(PIN_BTN_A) == LOW || digitalRead(PIN_BTN_B) == LOW) t = millis();
    delay(10);
  }
}

// ---------- Полотно для плавного рядка, що біжить ----------
// Текст малюється в оперативній пам'яті (232x26) і виводиться на екран одним блоком, тому не мерехтить.
// Саме тут обрізається все, що не вмістилось у ширину.
GFXcanvas16* gCv = nullptr;  // створюється в setup()

static void cvBegin() {
  gCv = new GFXcanvas16(232, 26);
  if (gCv && gCv->getBuffer()) {
    gCv->setTextWrap(false);
  } else {
    delete gCv;
    gCv = nullptr;  // без полотна довгі назви просто обрізаються
  }
}

static void cvBlit(int16_t x, int16_t y) {
  if (!gCv) return;
  tft.startWrite();
  tft.setAddrWindow(x, y, gCv->width(), gCv->height());
  tft.writePixels(gCv->getBuffer(), (uint32_t)gCv->width() * gCv->height());
  tft.endWrite();
}

struct Marq {
  bool active;    // текст ширший за доступне місце
  int textW, availW, off, maxOff;
  uint8_t ph;     // 0 пауза на початку, 1 прокрутка, 2 пауза наприкінці
  uint32_t t;
};

static void marqStart(Marq& m, int textW, int availW) {
  m.textW = textW;
  m.availW = availW;
  m.off = 0;
  m.ph = 0;
  m.t = millis();
  m.active = (textW > availW);
  m.maxOff = textW - availW + 6;
}

// Повертає true, коли зсув змінився і рядок треба перемалювати
static bool marqTick(Marq& m) {
  if (!m.active) return false;
  uint32_t now = millis();
  if (m.ph == 0) {
    if (now - m.t >= MARQUEE_HOLD_MS) { m.ph = 1; m.t = now; }
    return false;
  }
  if (m.ph == 1) {
    if (now - m.t < MARQUEE_STEP_MS) return false;
    m.t = now;
    m.off += 2;
    if (m.off >= m.maxOff) { m.off = m.maxOff; m.ph = 2; }
    return true;
  }
  if (now - m.t >= MARQUEE_HOLD_MS) {  // кінець паузи: починаємо спочатку
    m.off = 0;
    m.ph = 0;
    m.t = now;
    return true;
  }
  return false;
}

// ---------- Універсальний список ----------
// Повністю екран малюється один раз, далі перемальовуються лише змінені рядки.
// Вибраний рядок виводиться через полотно: якщо текст не вміщується, він біжить.
constexpr int LIST_ROWS = 7;
constexpr int LIST_ROW_H = 26;
constexpr int LIST_TOP = 34;
constexpr int LIST_TEXT_W = 218;  // ширина області тексту в рядку
typedef void (*LabelFn)(int idx, char* out, size_t n);

static int listFirst(int sel, int count) {
  int first = sel - LIST_ROWS / 2;
  if (first > count - LIST_ROWS) first = count - LIST_ROWS;
  if (first < 0) first = 0;
  return first;
}

static void listLabel(int idx, bool selected, LabelFn fn, LabelFn detail, char* out, size_t n) {
  if (selected && detail) detail(idx, out, n);
  else fn(idx, out, n);
}

static void drawSelRow(const char* label, int y, int off) {
  tft.setFont(&FreeSans9ptCyr);
  if (!gCv) {  // запасний варіант без полотна
    char buf[128];
    tft.fillRect(0, y, 240, LIST_ROW_H, COL_BG);
    tft.fillRoundRect(6, y, 228, LIST_ROW_H - 2, 4, COL_SEL);
    fitText(label, buf, sizeof buf, LIST_TEXT_W - 6);
    tft.setTextColor(COL_DAY);
    tft.setCursor(12, y + 18);
    printU(buf);
    return;
  }
  gCv->fillScreen(COL_BG);
  gCv->fillRect(2, 0, 228, LIST_ROW_H - 2, COL_SEL);
  gCv->setFont(&FreeSans9ptCyr);
  gCv->setTextColor(COL_DAY);
  gCv->setCursor(8 - off, 18);
  printUTo(*gCv, label);
  gCv->fillRect(2, 0, 6, LIST_ROW_H - 2, COL_SEL);    // ліве поле: ховаємо текст, що виїхав
  gCv->fillRect(226, 0, 4, LIST_ROW_H - 2, COL_SEL);  // праве поле
  gCv->fillRect(0, 0, 2, LIST_ROW_H, COL_BG);
  gCv->fillRect(230, 0, 2, LIST_ROW_H, COL_BG);
  cvBlit(4, y);
}

static void drawListRow(int idx, int first, int sel, int count, LabelFn fn, LabelFn detail, const Marq& mq) {
  int r = idx - first;
  if (idx < 0 || idx >= count || r < 0 || r >= LIST_ROWS) return;
  int y = LIST_TOP + r * LIST_ROW_H;
  bool s = (idx == sel);
  char buf[128];
  listLabel(idx, s, fn, detail, buf, sizeof buf);
  if (s) {
    drawSelRow(buf, y, mq.active ? mq.off : 0);
    return;
  }
  char fit[128];
  tft.setFont(&FreeSans9ptCyr);
  fitText(buf, fit, sizeof fit, LIST_TEXT_W - 6);
  tft.fillRect(0, y, 240, LIST_ROW_H, COL_BG);
  tft.setTextColor(COL_TEXT);
  tft.setCursor(12, y + 18);
  printU(fit);
}

static void drawListCounter(int sel, int count) {
  char buf[16];
  tft.fillRect(180, 4, 60, 22, COL_BG);
  snprintf(buf, sizeof buf, "%d/%d", sel + 1, count);
  tft.setFont(&FreeSans9ptCyr);
  tft.setTextColor(COL_DIM);
  rightText(buf, 232, 22);
}

static void drawListFull(const char* title, int count, LabelFn fn, LabelFn detail, int sel, const Marq& mq) {
  gHoldW = -1;
  tft.fillScreen(COL_BG);
  tft.setFont(&FreeSans12ptCyr);
  tft.setTextColor(COL_DAY);
  tft.setCursor(8, 22);
  char t[64];
  fitText(title, t, sizeof t, 168);
  printU(t);
  tft.drawFastHLine(8, 28, 224, COL_LINE);

  int first = listFirst(sel, count);
  for (int r = 0; r < LIST_ROWS; r++) drawListRow(first + r, first, sel, count, fn, detail, mq);
  drawListCounter(sel, count);

  char buf[40];
  snprintf(buf, sizeof buf, T("A:down B:up  hold %ds=OK", "A:вниз B:вгору  %dс=вибір"), (int)(LONG_PRESS_MS / 1000));
  tft.setFont(&FreeSans9ptCyr);
  tft.setTextColor(COL_DIM);
  tft.setCursor(8, 236);
  printU(buf);
}

static void startMarqFor(Marq& mq, int sel, LabelFn fn, LabelFn detail) {
  char buf[128];
  listLabel(sel, true, fn, detail, buf, sizeof buf);
  tft.setFont(&FreeSans9ptCyr);
  marqStart(mq, textWidthU(buf), LIST_TEXT_W);
}

// A: вниз, B: вгору, утримання будь-якої кнопки: вибір. Повертає індекс.
// detail (необов'язково): довший текст для вибраного рядка; якщо він не вміщується, то біжить.
static int listMenu(const char* title, int count, LabelFn fn, int sel, LabelFn detail = nullptr) {
  if (sel < 0 || sel >= count) sel = 0;
  Marq mq = {};
  startMarqFor(mq, sel, fn, detail);
  drawListFull(title, count, fn, detail, sel, mq);
  int first = listFirst(sel, count);
  for (;;) {
    BtnEvent ea = pollButton(btnA);
    BtnEvent eb = pollButton(btnB);
    if (ea == EV_LONG || eb == EV_LONG) return sel;

    int ns = sel;
    if (ea == EV_SHORT) ns = (sel + 1) % count;
    if (eb == EV_SHORT) ns = (sel + count - 1) % count;
    if (ns != sel) {
      startMarqFor(mq, ns, fn, detail);
      int nf = listFirst(ns, count);
      if (nf == first) {  // вікно не зсунулось: міняємо лише два рядки
        drawListRow(sel, first, ns, count, fn, detail, mq);
        drawListRow(ns, first, ns, count, fn, detail, mq);
      } else {
        for (int r = 0; r < LIST_ROWS; r++) drawListRow(nf + r, nf, ns, count, fn, detail, mq);
      }
      sel = ns;
      first = nf;
      drawListCounter(sel, count);
    } else if (marqTick(mq)) {
      drawListRow(sel, first, sel, count, fn, detail, mq);
    }
    drawHoldBar(220);
    delay(5);
  }
}

// ---------- Мережі Wi-Fi ----------
constexpr int MAX_NETS = 20;
struct NetItem {
  char ssid[33];   // справжня назва (для підключення)
  char label[34];  // назва для екрана (UTF-8)
  int rssi;
  bool secured;
  bool saved;      // пароль уже збережено
};
NetItem nets[MAX_NETS];
int netCount = 0;

static void scanNetworks() {
  showMessage(T("Scanning...", "Пошук мереж..."), "Wi-Fi", COL_TEXT);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  netCount = 0;
  for (int i = 0; i < n && netCount < MAX_NETS; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0 || s.length() > 32) continue;  // приховані мережі пропускаємо
    int dup = -1;
    for (int j = 0; j < netCount; j++) {
      if (s == nets[j].ssid) { dup = j; break; }
    }
    if (dup >= 0) {
      if (WiFi.RSSI(i) > nets[dup].rssi) nets[dup].rssi = WiFi.RSSI(i);
      continue;
    }
    NetItem& it = nets[netCount++];
    strlcpy(it.ssid, s.c_str(), sizeof it.ssid);
    strlcpy(it.label, it.ssid, sizeof it.label);  // UTF-8: українські назви теж виводяться
    it.rssi = WiFi.RSSI(i);
    it.secured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    it.saved = (wifiStoreFind(it.ssid) >= 0);
  }
  WiFi.scanDelete();
  for (int i = 1; i < netCount; i++) {  // за силою сигналу
    NetItem t = nets[i];
    int j = i - 1;
    while (j >= 0 && nets[j].rssi < t.rssi) { nets[j + 1] = nets[j]; j--; }
    nets[j + 1] = t;
  }
}

// "+" збережена мережа, "*" мережа з паролем. Після мереж: "Шукати знову" і (за потреби) "Назад".
static void netLabel(int i, char* out, size_t n) {
  if (i == netCount) {
    strlcpy(out, T("[ Rescan ]", "[ Шукати знову ]"), n);
  } else if (i > netCount) {
    strlcpy(out, T("[ Back ]", "[ Назад ]"), n);
  } else {
    char nm[48];
    u8trunc(nets[i].label, nm, sizeof nm, 40);  // довге ім'я біжить у вибраному рядку
    snprintf(out, n, "%c%c %s", nets[i].saved ? '+' : ' ', nets[i].secured ? '*' : ' ', nm);
  }
}

static const char* tzName(int i) { return UI_UKRAINIAN ? TIMEZONES[i].nameUa : TIMEZONES[i].name; }

static void tzLabel(int i, char* out, size_t n) { strlcpy(out, tzName(i), n); }

static void failLabel(int i, char* out, size_t n) {
  strlcpy(out, i == 0 ? T("Edit password", "Виправити пароль") : T("Choose other network", "Обрати іншу мережу"), n);
}

static int findTz(const String& posix) {
  for (int i = 0; i < TZ_COUNT; i++) {
    if (posix == TIMEZONES[i].posix) return i;
  }
  return 0;
}

static bool tryConnect(const char* ssid, const char* pass, const char* label) {
  char lb[64];
  u8trunc(label, lb, sizeof lb, 24);
  showMessage(T("Connecting...", "Підключення..."), lb, COL_TEXT);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid, (pass && pass[0]) ? pass : nullptr);
  uint32_t t0 = millis();
  while (millis() - t0 < 20000) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) return true;
    if (st == WL_CONNECT_FAILED) break;  // зазвичай хибний пароль
    delay(200);
  }
  WiFi.disconnect();
  return false;
}

// ---------- Введення пароля ----------
// A коротко: наступний символ. A 3 с: велика/мала літера. B коротко: крок назад.
// B 3 с: підтвердити. Символ фіксується сам після паузи CHAR_COMMIT_MS.
constexpr int PASS_MAX = 63;
constexpr int PASS_VISIBLE = 15;
static const char CHARSET[] = "abcdefghijklmnopqrstuvwxyz0123456789.-_@#!$%&*()+=?/:;,~^ ";
constexpr int CHARSET_N = sizeof(CHARSET) - 1;

static char candChar(int cand, bool upper) {
  char c = CHARSET[cand];
  if (upper && c >= 'a' && c <= 'z') c = c - 'a' + 'A';
  return c;
}

static void commitCand(char* pass, int& plen, int& cand, bool& upper) {
  if (cand >= 0 && plen < PASS_MAX) {
    pass[plen++] = candChar(cand, upper);
    pass[plen] = 0;
  }
  cand = -1;
  upper = false;
}

// Статична частина екрана малюється один раз
static void drawPassStatic(const char* netName) {
  gHoldW = -1;
  gCommitW = -1;
  tft.fillScreen(COL_BG);
  tft.setFont(&FreeSans12ptCyr);
  tft.setTextColor(COL_DAY);
  tft.setCursor(8, 22);
  printU(T("Wi-Fi password", "Пароль Wi-Fi"));
  tft.drawFastHLine(8, 28, 224, COL_LINE);

  char buf[48];
  tft.setFont(&FreeSans9ptCyr);
  tft.setTextColor(COL_DIM);
  u8trunc(netName, buf, sizeof buf, 14);
  tft.setCursor(8, 48);
  printU(buf);

  tft.drawRoundRect(6, 58, 228, 40, 6, COL_LINE);

  tft.setFont(&FreeSans9ptCyr);
  tft.setTextColor(COL_TEXT);
  snprintf(buf, sizeof buf, T("A: next  hold %ds: Aa", "A: символ  %dс: Аа"), (int)(LONG_PRESS_MS / 1000));
  tft.setCursor(8, 214);
  printU(buf);
  snprintf(buf, sizeof buf, T("B: back  hold %ds: connect", "B: назад  %dс: готово"), (int)(LONG_PRESS_MS / 1000));
  tft.setCursor(8, 232);
  printU(buf);
}

// Динамічна частина: лічильник, поле тексту і попередній перегляд (без повного очищення екрана)
static void drawPassDynamic(const char* pass, int plen, int cand, bool upper) {
  char buf[16];

  tft.fillRect(170, 32, 64, 20, COL_BG);
  snprintf(buf, sizeof buf, T("%d ch", "%d сим."), plen);
  tft.setFont(&FreeSans9ptCyr);
  tft.setTextColor(COL_DIM);
  rightText(buf, 232, 48);

  // Поле тексту (моноширинний шрифт, видно останні PASS_VISIBLE символів)
  tft.fillRect(8, 60, 224, 36, COL_BG);
  tft.setFont(&FreeMonoBold12pt7b);
  int total = plen + (cand >= 0 ? 1 : 0);
  int start = total > PASS_VISIBLE ? total - PASS_VISIBLE : 0;
  for (int i = start; i < total; i++) {
    int x = 12 + (i - start) * 14;
    bool isCand = (i == plen);
    char c = isCand ? candChar(cand, upper) : pass[i];
    if (isCand) tft.fillRect(x - 1, 66, 15, 26, COL_FEELS);
    tft.setTextColor(isCand ? COL_BG : COL_DAY);
    tft.setCursor(x, 86);
    tft.write(c);
  }

  // Великий попередній перегляд обраного символу
  tft.fillRect(0, 102, 240, 76, COL_BG);
  if (cand >= 0) {
    char c = candChar(cand, upper);
    tft.drawRoundRect(88, 108, 64, 66, 8, COL_FEELS);
    if (c == ' ') {
      tft.setFont(&FreeSans9ptCyr);
      tft.setTextColor(COL_FEELS);
      centerText(T("SPACE", "ПРОБІЛ"), 146);
    } else {
      char s[2] = {c, 0};
      tft.setFont(&FreeSansBold24pt7b);
      tft.setTextColor(COL_FEELS);
      centerText(s, 153);
    }
  } else {
    tft.setFont(&FreeSans9ptCyr);
    tft.setTextColor(COL_DIM);
    centerText(T("press A to pick a char", "натисніть A для символу"), 145);
  }
}

static void drawCommitBar(int16_t y, int w) {
  if (w == gCommitW) return;
  gCommitW = w;
  tft.fillRect(8, y, 224, 4, COL_BG);
  if (w > 0) tft.fillRect(8, y, w, 4, COL_RAIN);
}

static void enterPassword(const char* netName, char* pass, int& plen) {
  int cand = -1;
  bool upper = false;
  uint32_t lastAct = millis();
  bool prevDown = false;
  drawPassStatic(netName);
  drawPassDynamic(pass, plen, cand, upper);

  for (;;) {
    uint32_t now = millis();
    BtnEvent ea = pollButton(btnA);
    BtnEvent eb = pollButton(btnB);
    bool anyDown = btnA.down || btnB.down;
    if (prevDown && !anyDown) lastAct = now;  // пауза автофіксації рахується від відпускання
    prevDown = anyDown;
    bool redraw = false;

    if (ea == EV_SHORT) {
      cand = (cand + 1) % CHARSET_N;
      upper = false;
      lastAct = now;
      redraw = true;
    } else if (ea == EV_LONG) {
      if (cand >= 0 && CHARSET[cand] >= 'a' && CHARSET[cand] <= 'z') {
        upper = !upper;
        redraw = true;
      }
      lastAct = now;
    }

    if (eb == EV_SHORT) {
      if (cand >= 0) {            // спочатку скасовуємо обраний, ще не зафіксований символ
        cand = -1;
        upper = false;
      } else if (plen > 0) {      // потім стираємо останній зафіксований
        pass[--plen] = 0;
      }
      lastAct = now;
      redraw = true;
    } else if (eb == EV_LONG) {
      commitCand(pass, plen, cand, upper);
      if (plen < 8) {  // мінімум для WPA/WPA2
        showMessage(T("Min 8 characters", "Мінімум 8 символів"), nullptr, COL_BAD);
        delay(1500);
        drawPassStatic(netName);  // після повного повідомлення екран треба відновити
        lastAct = millis();
        redraw = true;
      } else {
        return;
      }
    }

    if (cand >= 0 && !anyDown && (now - lastAct) >= CHAR_COMMIT_MS) {
      commitCand(pass, plen, cand, upper);
      redraw = true;
    }

    if (redraw) drawPassDynamic(pass, plen, cand, upper);

    int cw = 0;
    if (cand >= 0) {
      if (anyDown) cw = 224;
      else {
        uint32_t el = now - lastAct;
        cw = el >= CHAR_COMMIT_MS ? 0 : (int)(224UL * (CHAR_COMMIT_MS - el) / CHAR_COMMIT_MS);
        cw = (cw / 4) * 4;
      }
    }
    drawCommitBar(180, cw);
    drawHoldBar(190);
    delay(5);
  }
}

// ---------- Майстер Wi-Fi ----------
// Вибір мережі, пароль, підключення. Зберігає мережу у список відомих і лишає Wi-Fi підключеним.
// allowCancel: додає пункт "Назад" (для виклику з меню); тоді повертає false, якщо користувач передумав.
bool wizardWifi(bool allowCancel) {
  waitButtonsReleased();
  WiFi.mode(WIFI_STA);

  char pass[PASS_MAX + 1] = "";
  int plen = 0;
  char ssidSel[33] = "";
  bool securedSel = false;
  bool connected = false;

  while (!connected) {
    // 1. Вибір мережі
    int sel = -1;
    for (;;) {
      scanNetworks();
      int extra = allowCancel ? 2 : 1;
      int s = listMenu(T("Wi-Fi network", "Мережа Wi-Fi"), netCount + extra, netLabel, 0);
      if (s < netCount) { sel = s; break; }
      if (s > netCount) return false;  // "Назад"
    }
    strlcpy(ssidSel, nets[sel].ssid, sizeof ssidSel);
    securedSel = nets[sel].secured;
    bool savedSel = nets[sel].saved;
    char label[34];
    strlcpy(label, nets[sel].label, sizeof label);
    plen = 0;
    pass[0] = 0;

    // 2a. Відома мережа: пробуємо збережений пароль, вводити його знову не потрібно
    if (savedSel) {
      int slot = wifiStoreFind(ssidSel);
      SavedNet sn;
      if (slot >= 0 && wifiStoreLoad(slot, sn)) {
        strlcpy(pass, sn.pass, sizeof pass);
        if (tryConnect(ssidSel, pass, label)) {
          connected = true;
          break;
        }
        showMessage(T("Saved password failed", "Пароль не підійшов"), T("Enter it again", "Введіть пароль знову"), COL_BAD);
        delay(1800);
        plen = 0;
        pass[0] = 0;
      }
    }

    // 2b. Нова мережа (або пароль змінився): введення пароля і підключення
    for (;;) {
      if (securedSel) enterPassword(label, pass, plen);
      if (tryConnect(ssidSel, securedSel ? pass : "", label)) {
        connected = true;
        break;
      }
      showMessage(T("Connection failed", "Не підключено"),
                  securedSel ? T("Check the password", "Перевірте пароль") : T("Try again", "Спробуйте ще раз"), COL_BAD);
      delay(2000);
      if (!securedSel) break;
      if (listMenu(T("Failed. Next?", "Що далі?"), 2, failLabel, 0) == 1) break;  // обрати іншу мережу
    }
  }

  wifiStoreSave(ssidSel, securedSel ? pass : "");
  return true;
}

// ---------- Майстер часового поясу ----------
void wizardTz(Preferences& prefs) {
  waitButtonsReleased();
  int start = findTz(prefs.isKey("tz") ? prefs.getString("tz", DEFAULT_TZ_INFO) : String(DEFAULT_TZ_INFO));
  int tz = listMenu(T("Time zone", "Часовий пояс"), TZ_COUNT, tzLabel, start);
  prefs.putString("tz", TIMEZONES[tz].posix);
  showMessage(T("Saved", "Збережено"), tzName(tz), COL_OK);
  delay(1200);
}

// Перше налаштування: мережа і часовий пояс (населений пункт вибирається окремо, див. places.h)
void runSetupWizard(Preferences& prefs) {
  wizardWifi(false);
  wizardTz(prefs);
}
