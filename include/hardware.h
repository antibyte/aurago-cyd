#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

#define SCREEN_W 320
#define SCREEN_H 240

#define CYD_LED_RED 4
#define CYD_LED_GREEN 16
#define CYD_LED_BLUE 17
#define CYD_LDR_PIN 34
#define CYD_BOOT_PIN 0
#define CYD_BACKLIGHT_PIN 21
#define CYD_SPEAKER_PIN 26

#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

enum class LedColor { Off, Green, Yellow, Red, Blue };

struct TouchEvent {
  bool active;
  bool tap;
  bool swipe_left;
  bool swipe_right;
  int16_t x;
  int16_t y;
};

extern TFT_eSPI tft;
extern XPT2046_Touchscreen ts;

void hardware_begin();
void hardware_set_led(LedColor color);
void hardware_set_brightness(uint8_t value);
uint8_t hardware_auto_brightness();
bool hardware_boot_held(uint32_t ms);
TouchEvent hardware_poll_touch();
int8_t hardware_wifi_rssi();
