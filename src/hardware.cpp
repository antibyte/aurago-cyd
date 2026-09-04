#include "hardware.h"

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <string.h>
#include <strings.h>

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

static const int TOUCH_X_MIN = 290;
static const int TOUCH_X_MAX = 3670;
static const int TOUCH_Y_MIN = 230;
static const int TOUCH_Y_MAX = 3860;

static bool touch_down = false;
static int16_t down_x = 0;
static int16_t down_y = 0;
static int16_t last_x = 0;
static int16_t last_y = 0;
static uint32_t down_ms = 0;

void hardware_begin() {
  pinMode(CYD_LED_RED, OUTPUT);
  pinMode(CYD_LED_GREEN, OUTPUT);
  pinMode(CYD_LED_BLUE, OUTPUT);
  pinMode(CYD_BOOT_PIN, INPUT_PULLUP);
  pinMode(CYD_LDR_PIN, INPUT);
  hardware_set_led(LedColor::Blue);

  ledcSetup(0, 5000, 8);
  ledcAttachPin(CYD_BACKLIGHT_PIN, 0);
  hardware_set_brightness(200);

  touchSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSpi);
  ts.setRotation(1);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
}

void hardware_set_led(LedColor color) {
  digitalWrite(CYD_LED_RED, HIGH);
  digitalWrite(CYD_LED_GREEN, HIGH);
  digitalWrite(CYD_LED_BLUE, HIGH);
  switch (color) {
    case LedColor::Green:
      digitalWrite(CYD_LED_GREEN, LOW);
      break;
    case LedColor::Yellow:
      digitalWrite(CYD_LED_RED, LOW);
      digitalWrite(CYD_LED_GREEN, LOW);
      break;
    case LedColor::Red:
      digitalWrite(CYD_LED_RED, LOW);
      break;
    case LedColor::Blue:
      digitalWrite(CYD_LED_BLUE, LOW);
      break;
    case LedColor::Off:
    default:
      break;
  }
}

void hardware_set_brightness(uint8_t value) {
  if (value < 12) {
    value = 12;
  }
  ledcWrite(0, value);
}

uint8_t hardware_auto_brightness() {
  int raw = analogRead(CYD_LDR_PIN);
  // LDR: dark = high ADC, bright = low ADC on typical CYD boards
  int mapped = map(raw, 0, 4095, 255, 40);
  if (mapped < 40) {
    mapped = 40;
  }
  if (mapped > 255) {
    mapped = 255;
  }
  return static_cast<uint8_t>(mapped);
}

bool hardware_boot_held(uint32_t ms) {
  uint32_t start = millis();
  while (digitalRead(CYD_BOOT_PIN) == LOW) {
    if (millis() - start >= ms) {
      return true;
    }
    delay(20);
  }
  return false;
}

static void map_touch(const TS_Point &p, int16_t *x, int16_t *y) {
  int mx = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCREEN_W - 1);
  int my = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_H - 1);
  if (mx < 0) {
    mx = 0;
  }
  if (mx > SCREEN_W - 1) {
    mx = SCREEN_W - 1;
  }
  if (my < 0) {
    my = 0;
  }
  if (my > SCREEN_H - 1) {
    my = SCREEN_H - 1;
  }
  *x = static_cast<int16_t>(mx);
  *y = static_cast<int16_t>(my);
}

TouchEvent hardware_poll_touch() {
  TouchEvent ev{};
  bool pressed = ts.tirqTouched() && ts.touched();
  if (pressed) {
    TS_Point p = ts.getPoint();
    if (p.z < 200) {
      return ev;
    }
    int16_t x = 0;
    int16_t y = 0;
    map_touch(p, &x, &y);
    ev.active = true;
    ev.x = x;
    ev.y = y;
    last_x = x;
    last_y = y;
    if (!touch_down) {
      touch_down = true;
      down_x = x;
      down_y = y;
      down_ms = millis();
    }
    return ev;
  }

  if (touch_down) {
    touch_down = false;
    uint32_t dt = millis() - down_ms;
    int16_t up_x = last_x;
    int16_t up_y = last_y;
    int dx = up_x - down_x;
    int dy = up_y - down_y;
    ev.x = up_x;
    ev.y = up_y;
    if (dt < 500 && abs(dx) < 18 && abs(dy) < 18) {
      ev.tap = true;
    } else if (abs(dx) > 40 && abs(dx) > abs(dy)) {
      if (dx < 0) {
        ev.swipe_left = true;
      } else {
        ev.swipe_right = true;
      }
    }
  }
  return ev;
}

int8_t hardware_wifi_rssi() {
  if (WiFi.status() != WL_CONNECTED) {
    return 0;
  }
  return static_cast<int8_t>(WiFi.RSSI());
}
