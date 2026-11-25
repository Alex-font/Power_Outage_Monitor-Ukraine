#pragma once
// Налаштування пристрою. Ключів і паролів тут немає: Wi-Fi, часовий пояс, населений пункт
// і черга вводяться кнопками на екрані та зберігаються в пам'яті плати.

#include <Arduino.h>
#include <SPI.h>

// ===== Wi-Fi =====
// Якщо плата C3 Super Mini погано підключається до Wi-Fi, поставте true
// (знижує потужність передавача, це відомий фікс для деяких плат).
constexpr bool WIFI_LOW_TX_POWER = false;

// ===== Часовий пояс =====
// Лише підсвічується у списку за замовчуванням (Київ).
constexpr const char* DEFAULT_TZ_INFO = "EET-2EEST,M3.5.0/3,M10.5.0/4";

// ===== Інтервали =====
constexpr uint32_t SCHEDULE_INTERVAL_MS = 10UL * 60UL * 1000UL;  // перевірка змін графіка раз на 10 хв
constexpr uint32_t RETRY_INTERVAL_MS    = 30UL * 1000UL;         // повтор після помилки
constexpr uint32_t STALE_AFTER_MS       = 30UL * 60UL * 1000UL;  // дані "застаріли" (жовтий індикатор)
constexpr uint32_t CHANGED_SHOW_MS      = 3UL * 60UL * 1000UL;   // скільки показувати "Графік оновлено"

// ===== Екран ST7789 240x240 (піни ESP32-C3 Super Mini) =====
// Уникайте GPIO2, 8, 9 (strapping) та 20, 21 (UART).
constexpr int8_t PIN_TFT_SCK  = 4;   // SCL
constexpr int8_t PIN_TFT_MOSI = 6;   // SDA
constexpr int8_t PIN_TFT_DC   = 5;   // DC
constexpr int8_t PIN_TFT_RST  = 3;   // RES
constexpr int8_t PIN_TFT_CS   = 7;   // CS; якщо на модулі немає CS, поставте -1
constexpr int8_t PIN_TFT_BL   = 10;  // BLK; -1, якщо підсвітка підключена до 3V3 (тоді регулювання яскравості недоступне)
constexpr bool TFT_BL_ACTIVE_HIGH = true;  // false, якщо підсвітка вмикається низьким рівнем (рідко)

// Модулі БЕЗ піна CS часто працюють лише в SPI_MODE2 або SPI_MODE3.
// Якщо екран білий/порожній, спробуйте їх. Для модулів з CS залишайте SPI_MODE0.
constexpr uint8_t TFT_SPI_MODE = SPI_MODE0;
constexpr uint8_t TFT_ROTATION = 1;  // 0..3; 1 = поворот на 90° за годинниковою стрілкою (якщо вийшло в інший бік, поставте 3)

// ===== Кнопки (одним виводом на GND, використовується внутрішня підтяжка) =====
// Не використовуйте GPIO9 (кнопка BOOT) і GPIO2, 8 (strapping).
constexpr int8_t PIN_BTN_A = 0;   // A: далі / вниз / наступний символ
constexpr int8_t PIN_BTN_B = 1;   // B: назад / вгору
constexpr uint32_t LONG_PRESS_MS   = 3000;  // довге утримання
constexpr uint32_t BTN_DEBOUNCE_MS = 30;    // усунення брязкоту
constexpr uint32_t CHAR_COMMIT_MS  = 1500;  // пауза, після якої обраний символ пароля фіксується
constexpr uint32_t BL_ADJUST_TIMEOUT_MS = 5000;  // вихід з регулювання яскравості після паузи

// ===== Довгі назви (рядок, що біжить) =====
constexpr uint32_t MARQUEE_HOLD_MS = 900;   // пауза на початку й наприкінці
constexpr uint32_t MARQUEE_STEP_MS = 45;    // крок прокрутки (2 пікселі)
