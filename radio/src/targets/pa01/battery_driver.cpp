/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "battery_driver.h"

#include "boards/generic_stm32/rgb_leds.h"
#include "led_scene.h"
#undef UNUSED
#include "bsp_io.h"
#include "edgetx.h"
#include "colors.h"
#include "etx_lv_theme.h"
#include "LvglWrapper.h"

#include "charge_text.h"

#include <math.h>

#define  __BATTERY_DRIVER_C__

// Landscape pack. PA01's panel is 320x240, so the battery lies on its side:
// body on the left, positive terminal on the right, fill growing rightwards.
#define BATTERY_W 160
#define BATTERY_H 86
#define BATTERY_TOP 54
// 6 px rather than the usual 4: the metal ramp needs some width across the
// frame before the shading reads as "brushed metal" instead of a grey line.
#define BATTERY_BORDER 6
#define BATTERY_PAD 7
#define BATTERY_RADIUS 16
#define BATTERY_FILL_RADIUS 10
#define BATTERY_CONNECTOR_W 11
#define BATTERY_CONNECTOR_H 40
// The body is nudged left by half the terminal width so that the body+terminal
// group is what ends up centred on the panel, not the body alone.
#define BATTERY_LEFT ((LCD_W - (BATTERY_W + BATTERY_CONNECTOR_W)) / 2)
#define BATTERY_W_INNER (BATTERY_W - 2 * (BATTERY_BORDER + BATTERY_PAD))
#define BATTERY_H_INNER (BATTERY_H - 2 * (BATTERY_BORDER + BATTERY_PAD))
#define BATTERY_LEFT_INNER (BATTERY_LEFT + BATTERY_BORDER + BATTERY_PAD)
#define BATTERY_TOP_INNER (BATTERY_TOP + BATTERY_BORDER + BATTERY_PAD)

#define UCHARGER_SAMPLING_CNT              10
#define UCHARGER_CHARGING_SAMPLING_CNT     10

typedef struct
{
  bool hasCharger : 1;
  bool isChargeEnd : 1;
  bool isChargerDetectionReady : 1;
  bool isChargingDetectionReady : 1;
  bool isHighCurrent : 1;
  uint8_t chargerSamplingCount;
  uint8_t chargingSamplingCount;
  uint8_t chargeEndSamplingCount;
} STRUCT_BATTERY_CHARGER;

STRUCT_BATTERY_CHARGER uCharger; // USB charger

void chargerDetection(STRUCT_BATTERY_CHARGER* charger, uint8_t chargerPinActive, uint8_t samplingCountThreshold)
{
  if ((charger->hasCharger && chargerPinActive) || (!charger->hasCharger && !chargerPinActive))
  {
    charger->chargerSamplingCount = 0;
  }
  else
  {
    charger->chargerSamplingCount++;
    if (charger->chargerSamplingCount >= samplingCountThreshold)
    {
      charger->chargerSamplingCount = 0;
      charger->hasCharger = !charger->hasCharger;
      charger->isChargerDetectionReady = true;
    }
  }
}

void resetChargeEndDetection(STRUCT_BATTERY_CHARGER* charger)
{
  charger->isChargeEnd = false;
  charger->isChargingDetectionReady = false;
  charger->chargingSamplingCount = 0;
  charger->isHighCurrent = false;
}

void chargeEndDetection(STRUCT_BATTERY_CHARGER* charger, uint8_t chargeEndPinActive, uint8_t samplingCountThreshold)
{
  if (charger->isChargeEnd)
  {
    if (chargeEndPinActive)
    {
      charger->chargingSamplingCount = 0;
      if (charger->isChargingDetectionReady)
      {
        charger->chargeEndSamplingCount = 0;
      }
      else
      {
        charger->chargeEndSamplingCount++;
        if (charger->chargeEndSamplingCount >= samplingCountThreshold)
        {
          charger->chargeEndSamplingCount = 0;
          charger->isChargingDetectionReady = true;
        }
      }
    }
    else
    {
      charger->chargeEndSamplingCount = 0;
      charger->chargingSamplingCount++;
      if (charger->chargingSamplingCount >= samplingCountThreshold)
      {
        charger->chargingSamplingCount = 0;
        charger->isChargeEnd = false;
        charger->isChargingDetectionReady = true;
      }
    }
  }
  else
  {
    if (!chargeEndPinActive)
    {
      charger->chargeEndSamplingCount = 0;
      if (charger->isChargingDetectionReady)
      {
        charger->chargingSamplingCount = 0;
      }
      else
      {
        charger->chargingSamplingCount++;
        if (charger->chargingSamplingCount >= samplingCountThreshold)
        {
          charger->chargingSamplingCount = 0;
          charger->isChargingDetectionReady = true;
        }
      }
    }
    else
    {
      charger->chargingSamplingCount = 0;
      charger->chargeEndSamplingCount++;
      if (charger->chargeEndSamplingCount >= samplingCountThreshold)
      {
        charger->chargeEndSamplingCount = 0;
        charger->isChargeEnd = true;
        charger->isChargingDetectionReady = true;
      }
    }
  }
}

uint16_t get_battery_charge_state()
{
  uint16_t state = CHARGE_UNKNOWN;

  chargerDetection(&uCharger, IS_UCHARGER_ACTIVE(), UCHARGER_SAMPLING_CNT);
  if (uCharger.isChargerDetectionReady)
  {
    if (uCharger.hasCharger)  // USB charger can be detected properly no matter it is enabled or not
    {
      ENABLE_UCHARGER();
      chargeEndDetection(&uCharger, IS_UCHARGER_CHARGE_END_ACTIVE(), UCHARGER_CHARGING_SAMPLING_CNT);
      if (uCharger.isChargingDetectionReady)
      {
        if (uCharger.isChargeEnd)
        {
          state = CHARGE_FINISHED;
        }
        else
        {
          state = CHARGE_STARTED;
        }
      }
    }
    else
    {
      resetChargeEndDetection(&uCharger);

      // Disable USB charger if it is not present, so that wireless charger can be detected properly
  //    DISABLE_UCHARGER();
    }
  }

  return state;
}

bool isChargerActive()
{  
  if (uCharger.isChargerDetectionReady) {
    // Detect the removal of the charger.
    get_battery_charge_state();
  } else {
    while (!uCharger.isChargerDetectionReady)
    {
      get_battery_charge_state();
      delay_ms(10);
    }
  }
  return uCharger.hasCharger;
}

void battery_charge_init()
{
  // USB charger status pins
  gpio_init(UCHARGER_GPIO, GPIO_IN, GPIO_PIN_SPEED_LOW);
  gpio_init(UCHARGER_CHARGE_END_GPIO, GPIO_IN_PD, GPIO_PIN_SPEED_LOW);

  // USB charger state init
  ENABLE_UCHARGER();
  uCharger.hasCharger = !IS_UCHARGER_ACTIVE();  // Init for sampling count works
  uCharger.isChargerDetectionReady = false;
  resetChargeEndDetection(&uCharger);
}

void ledChargingInfo(uint16_t chargeState) {
#if defined(LED_STRIP_GPIO)
  static int ledIdx = LED_CHARGING_END;
  if (ledIdx == LED_CHARGING_END)
    ledIdx = LED_CHARGING_START;
  else
    ledIdx += LED_CHARGING_START > LED_CHARGING_END ? -1 : 1;
  for (int i = 0; i < LED_STRIP_LENGTH; i++) {
    uint8_t green = 0;
    if (CHARGE_FINISHED == chargeState) {
      if (LED_CHARGING_START > LED_CHARGING_END && i <= LED_CHARGING_START && i >= LED_CHARGING_END)
      {
        green = 20;
      }
      if (LED_CHARGING_START <= LED_CHARGING_END && i >= LED_CHARGING_START && i <= LED_CHARGING_END)
      {
        green = 20;
      }
    } else if (ledIdx == i) {
      green = 20;
    }
    rgbSetLedColor(i, 0, green, 0);
  }
  rgbLedColorApply();
#endif
}

// The charging screen is drawn straight into the panel frame buffer and pushed
// over SPI. LVGL is deliberately not involved: at this point the radio is
// still "off" (no EEPROM read, g_eeGeneral all zeroes), and bringing the GUI
// stack up this early means lv_init() + theme + display driver all have to
// survive a half-initialised system. Painting pixels does not.
extern pixel_t LCD_FIRST_FRAME_BUFFER[];
extern "C" void lcdPushFullFrame(const uint16_t* fb);

// RGB565 renditions of the PA01 theme colours
#define CH_COLOR_BG    0x10A3  // PRIMARY2 (18,21,27)
#define CH_COLOR_FG    0xEF7E  // PRIMARY1 (232,236,242)
#define CH_COLOR_DONE  0x07E0  // bright green

// Backlight duty (0..100) for the charge screen. High enough to read across a
// room, low enough that leaving the radio plugged in overnight is not annoying.
#define CHARGE_BACKLIGHT 55

// How long the charge screen stays fully lit, then how long before it goes
// dark for good. A tap on the power key wakes it again; holding the key still
// boots the radio. The RGB strip is driven independently of all this, so a
// dark panel never means the radio has stopped charging.
#define CHARGE_DIM_AFTER_MS   30000UL    // 30 s
#define CHARGE_OFF_AFTER_MS   300000UL   // 5 min
#define CHARGE_BACKLIGHT_DIM  10
#define CHARGE_BACKLIGHT_DARK 1          // 1, not 0: 0 also sends panel "display off"

// Screen idle stages, in the order they are entered.
#define CHARGE_SCR_BRIGHT 0
#define CHARGE_SCR_DIM    1
#define CHARGE_SCR_DARK   2

// Top edge of the caption, centred in the space left under the battery body.
#define CH_TEXT_TOP (BATTERY_TOP + BATTERY_H + 16)

// Bring-up diagnostics: every step of the panel init latches a distinct colour
// on the internal RGB strip. If the screen stays dark, the colour the strip
// freezes on tells us exactly which call hung. Off in normal builds.
//   red    -> entered chargeLcdInit
//   yellow -> background painted into the frame buffer
//   green  -> frame buffer handed to the LCD driver
//   cyan   -> lcdInit() returned
//   blue   -> backlightInit() returned
//   white  -> backlightEnable() returned, everything done
#define PA01_CHARGE_DIAG 0

static void chargeDiagLed(uint8_t r, uint8_t g, uint8_t b)
{
#if PA01_CHARGE_DIAG && !defined(SIMU)
  for (int i = 0; i < 14; i++) rgbSetLedColor(i, r, g, b);
  rgbLedColorApply();
#endif
}

static bool chargeLcdReady = false;

static inline void chargeFillRect(pixel_t* fb, int x, int y, int w, int h,
                                  uint16_t color)
{
  if (w <= 0 || h <= 0) return;
  if (x >= LCD_W || y >= LCD_H) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > LCD_W) w = LCD_W - x;
  if (y + h > LCD_H) h = LCD_H - y;
  if (w <= 0 || h <= 0) return;

  for (int row = y; row < y + h; row++) {
    pixel_t* p = fb + (size_t)row * LCD_W + x;
    for (int col = 0; col < w; col++) *p++ = color;
  }
}

// Blend one RGB565 colour towards another by an 8-bit coverage value.
static inline uint16_t chargeBlend565(uint16_t bg, uint16_t fg, uint8_t a)
{
  if (a == 0) return bg;
  if (a == 255) return fg;

  int br = (bg >> 11) & 0x1F, bgc = (bg >> 5) & 0x3F, bb = bg & 0x1F;
  int fr = (fg >> 11) & 0x1F, fgc = (fg >> 5) & 0x3F, fb2 = fg & 0x1F;

  int r = br + (((fr - br) * a) >> 8);
  int g = bgc + (((fgc - bgc) * a) >> 8);
  int b = bb + (((fb2 - bb) * a) >> 8);

  return (uint16_t)((r << 11) | (g << 5) | b);
}

// Blit one of the baked caption masks, antialiased against the flat background.
// Everything here sits on CH_COLOR_BG, so the background colour is a constant
// rather than a per-pixel read back out of SDRAM.
static void chargeBlitText(pixel_t* fb, int x, int y, int w, int h,
                           const uint8_t* mask)
{
  for (int row = 0; row < h; row++) {
    int py = y + row;
    if (py < 0 || py >= LCD_H) continue;

    pixel_t* p = fb + (size_t)py * LCD_W + x;
    const uint8_t* m = mask + row * w;
    for (int col = 0; col < w; col++, p++, m++) {
      if (*m) *p = chargeBlend565(CH_COLOR_BG, CH_COLOR_FG, *m);
    }
  }
}

// Anti-aliased rounded rectangle, filled.
//
// chargeFillRect only does axis-aligned boxes, which would leave the battery
// with square corners - the design has 16 px radii. Coverage comes from the
// rounded-rect distance field: dx/dy are the distances past the straight middle
// section, so all four corner arcs fall out of a single sqrtf.
static void chargeRoundRect(pixel_t* fb, float x, float y, float w, float h,
                            float r, uint16_t color)
{
  if (w <= 0 || h <= 0) return;
  if (r > w * 0.5f) r = w * 0.5f;
  if (r > h * 0.5f) r = h * 0.5f;
  if (r < 0) r = 0;

  const float cx = x + w * 0.5f;
  const float cy = y + h * 0.5f;
  const float hx = w * 0.5f - r;
  const float hy = h * 0.5f - r;

  int x0 = (int)x - 1, x1 = (int)(x + w) + 1;
  int y0 = (int)y - 1, y1 = (int)(y + h) + 1;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > LCD_W) x1 = LCD_W;
  if (y1 > LCD_H) y1 = LCD_H;

  for (int row = y0; row < y1; row++) {
    float dy = fabsf((float)row + 0.5f - cy) - hy;
    if (dy < 0) dy = 0;

    pixel_t* p = fb + (size_t)row * LCD_W;
    for (int col = x0; col < x1; col++) {
      float dx = fabsf((float)col + 0.5f - cx) - hx;
      if (dx < 0) dx = 0;

      float cov = 0.5f - (sqrtf(dx * dx + dy * dy) - r);
      if (cov <= 0) continue;

      if (cov >= 1.0f) {
        p[col] = color;
      } else {
        p[col] = chargeBlend565(p[col], color, (uint8_t)(cov * 255.0f));
      }
    }
  }
}

// Metal silver ramp, sampled by position within the pack. Monotonic from a
// bright top edge down to a dark base - deliberately no re-brightening band in
// the middle, so nothing reads as a specular reflection.
static const struct { float p; uint16_t c; } chargeRampStops[] = {
    { 0.00f, 0xFFDF },  // (248,250,254) near-white top edge
    { 0.30f, 0xD0BB },  // (206,212,222)
    { 0.65f, 0x94F5 },  // (150,158,170)
    { 1.00f, 0x6B90 },  // (106,114,128) dark base
};

static uint16_t chargeMetalColor(float t)
{
  const int n = sizeof(chargeRampStops) / sizeof(chargeRampStops[0]);

  if (t <= chargeRampStops[0].p) return chargeRampStops[0].c;
  if (t >= chargeRampStops[n - 1].p) return chargeRampStops[n - 1].c;

  for (int i = 1; i < n; i++) {
    if (t <= chargeRampStops[i].p) {
      float f = (t - chargeRampStops[i - 1].p)
                / (chargeRampStops[i].p - chargeRampStops[i - 1].p);
      return chargeBlend565(chargeRampStops[i - 1].c, chargeRampStops[i].c,
                            (uint8_t)(f * 255.0f));
    }
  }
  return chargeRampStops[n - 1].c;
}

// Rainbow ramp for the fill bar, sampled left to right. The gradient spans the
// whole inner cavity, not the current bar width, so the growing bar reveals
// more and more of the spectrum as the charge animation advances.
static const struct { float p; uint16_t c; } chargeRainbowStops[] = {
    { 0.00f, 0xFA8A },  // (255, 80, 80) red
    { 0.25f, 0xFD47 },  // (255,170, 60) orange
    { 0.50f, 0x5F0F },  // ( 90,225,120) green
    { 0.75f, 0x465F },  // ( 70,200,255) cyan-blue
    { 1.00f, 0x8B7F },  // (140,110,255) violet
};

static uint16_t chargeRainbowColor(float t)
{
  const int n = sizeof(chargeRainbowStops) / sizeof(chargeRainbowStops[0]);

  if (t <= chargeRainbowStops[0].p) return chargeRainbowStops[0].c;
  if (t >= chargeRainbowStops[n - 1].p) return chargeRainbowStops[n - 1].c;

  for (int i = 1; i < n; i++) {
    if (t <= chargeRainbowStops[i].p) {
      float f = (t - chargeRainbowStops[i - 1].p)
                / (chargeRainbowStops[i].p - chargeRainbowStops[i - 1].p);
      return chargeBlend565(chargeRainbowStops[i - 1].c, chargeRainbowStops[i].c,
                            (uint8_t)(f * 255.0f));
    }
  }
  return chargeRainbowStops[n - 1].c;
}

// Rounded rect whose colour varies along X between gx0 and gx1 - the horizontal
// counterpart of chargeRoundRectMetal().
static void chargeRoundRectRainbow(pixel_t* fb, float x, float y, float w, float h,
                                   float r, float gx0, float gx1)
{
  if (w <= 0 || h <= 0) return;
  if (r > w * 0.5f) r = w * 0.5f;
  if (r > h * 0.5f) r = h * 0.5f;
  if (r < 0) r = 0;
  if (gx1 <= gx0) gx1 = gx0 + 1;

  const float cx = x + w * 0.5f;
  const float cy = y + h * 0.5f;
  const float hx = w * 0.5f - r;
  const float hy = h * 0.5f - r;

  int x0 = (int)x - 1, x1 = (int)(x + w) + 1;
  int y0 = (int)y - 1, y1 = (int)(y + h) + 1;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > LCD_W) x1 = LCD_W;
  if (y1 > LCD_H) y1 = LCD_H;

  for (int row = y0; row < y1; row++) {
    float dy = fabsf((float)row + 0.5f - cy) - hy;
    if (dy < 0) dy = 0;

    pixel_t* p = fb + (size_t)row * LCD_W;
    for (int col = x0; col < x1; col++) {
      float dx = fabsf((float)col + 0.5f - cx) - hx;
      if (dx < 0) dx = 0;

      float cov = 0.5f - (sqrtf(dx * dx + dy * dy) - r);
      if (cov <= 0) continue;

      float t = ((float)col + 0.5f - gx0) / (gx1 - gx0);
      uint16_t color = chargeRainbowColor(t);

      if (cov >= 1.0f) {
        p[col] = color;
      } else {
        p[col] = chargeBlend565(p[col], color, (uint8_t)(cov * 255.0f));
      }
    }
  }
}

// chargeRoundRect with the fill colour taken from the metal ramp. gy0/gy1 are
// the reference span the ramp maps onto - pass the body's bounds so every
// part of the pack shares one lighting curve.
static void chargeRoundRectMetal(pixel_t* fb, float x, float y, float w, float h,
                                 float r, float gy0, float gy1)
{
  if (w <= 0 || h <= 0) return;
  if (r > w * 0.5f) r = w * 0.5f;
  if (r > h * 0.5f) r = h * 0.5f;
  if (r < 0) r = 0;
  if (gy1 <= gy0) gy1 = gy0 + 1;

  const float cx = x + w * 0.5f;
  const float cy = y + h * 0.5f;
  const float hx = w * 0.5f - r;
  const float hy = h * 0.5f - r;

  int x0 = (int)x - 1, x1 = (int)(x + w) + 1;
  int y0 = (int)y - 1, y1 = (int)(y + h) + 1;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > LCD_W) x1 = LCD_W;
  if (y1 > LCD_H) y1 = LCD_H;

  for (int row = y0; row < y1; row++) {
    float dy = fabsf((float)row + 0.5f - cy) - hy;
    if (dy < 0) dy = 0;

    float t = ((float)row + 0.5f - gy0) / (gy1 - gy0);
    uint16_t color = chargeMetalColor(t);

    pixel_t* p = fb + (size_t)row * LCD_W;
    for (int col = x0; col < x1; col++) {
      float dx = fabsf((float)col + 0.5f - cx) - hx;
      if (dx < 0) dx = 0;

      float cov = 0.5f - (sqrtf(dx * dx + dy * dy) - r);
      if (cov <= 0) continue;

      if (cov >= 1.0f) {
        p[col] = color;
      } else {
        p[col] = chargeBlend565(p[col], color, (uint8_t)(cov * 255.0f));
      }
    }
  }
}

static void chargeLcdInit()
{
  pixel_t* fb = LCD_FIRST_FRAME_BUFFER;

  chargeDiagLed(255, 0, 0);

  // Paint before lcdInit(): the driver pushes the frame buffer to the panel as
  // part of its init sequence, so the first thing the user sees is the charge
  // background instead of whatever was left in SDRAM. (SDRAM itself is brought
  // up by the bootloader in boardBLPreJump(), which always runs before this
  // code - the firmware image does not contain the SDRAM driver at all.)
  chargeFillRect(fb, 0, 0, LCD_W, LCD_H, CH_COLOR_BG);

  chargeDiagLed(255, 255, 0);

  lcdSetInitalFrameBuffer(fb);

  chargeDiagLed(0, 255, 0);

  lcdInit();

  chargeDiagLed(0, 255, 255);

  backlightInit();

  chargeDiagLed(0, 0, 255);

  backlightEnable(CHARGE_BACKLIGHT);

  chargeDiagLed(255, 255, 255);

  // The RGB strip is left alone here: the charge loop keeps calling
  // updateBatteryState(RGB_STATE_CHARGE), exactly as it did before the panel
  // gauge existed, so the breathing charge indication is unchanged.

  chargeLcdReady = true;
}

void drawChargingInfo(uint16_t chargeState)
{
  // Charging walks 1..4 and wraps; "full" pins it at 4; an unknown state shows
  // an empty pack. Starting at 0 keeps the first frame from looking full.
  static int progress = 0;

  switch (chargeState) {
    case CHARGE_STARTED:
      progress += 1;
      if (progress > 4) progress = 1;
      break;
    case CHARGE_FINISHED:
      progress = 4;
      break;
    default:
      progress = 0;
      break;
  }

  if (!chargeLcdReady) chargeLcdInit();

  pixel_t* fb = LCD_FIRST_FRAME_BUFFER;
  chargeFillRect(fb, 0, 0, LCD_W, LCD_H, CH_COLOR_BG);

  // Battery body: metal frame, filled in step by step from the left. Drawn as
  // a gradient rounded rect with a second rounded rect punched out of the
  // middle; the punch is what turns the ramp into a frame.
  chargeRoundRectMetal(fb, BATTERY_LEFT, BATTERY_TOP, BATTERY_W, BATTERY_H,
                       BATTERY_RADIUS, BATTERY_TOP, BATTERY_TOP + BATTERY_H);
  chargeRoundRect(fb, BATTERY_LEFT + BATTERY_BORDER, BATTERY_TOP + BATTERY_BORDER,
                  BATTERY_W - 2 * BATTERY_BORDER, BATTERY_H - 2 * BATTERY_BORDER,
                  BATTERY_RADIUS - BATTERY_BORDER, CH_COLOR_BG);

  // Positive terminal, butted against the body's right edge. Mapped onto the
  // body's ramp span, so it shades in step with the frame beside it.
  chargeRoundRectMetal(fb, BATTERY_LEFT + BATTERY_W,
                       BATTERY_TOP + (BATTERY_H - BATTERY_CONNECTOR_H) / 2,
                       BATTERY_CONNECTOR_W, BATTERY_CONNECTOR_H,
                       BATTERY_CONNECTOR_W / 2,
                       BATTERY_TOP, BATTERY_TOP + BATTERY_H);

  // Fill bar: constant height, width follows the 4-step animation. While
  // charging it carries a rainbow gradient that spans the whole cavity, so the
  // growing bar reveals the spectrum step by step; "full" stays solid green,
  // which is a state signal rather than a decoration.
  if (progress > 0) {
    int w = (BATTERY_W_INNER * progress) / 4;
    if (chargeState == CHARGE_FINISHED) {
      chargeRoundRect(fb, BATTERY_LEFT_INNER, BATTERY_TOP_INNER, w,
                      BATTERY_H_INNER, BATTERY_FILL_RADIUS, CH_COLOR_DONE);
    } else {
      chargeRoundRectRainbow(fb, BATTERY_LEFT_INNER, BATTERY_TOP_INNER, w,
                             BATTERY_H_INNER, BATTERY_FILL_RADIUS,
                             BATTERY_LEFT_INNER,
                             BATTERY_LEFT_INNER + BATTERY_W_INNER);
    }
  }

  // Caption under the pack: the charge state in words, since a quarter-fill bar
  // alone does not say whether the radio is still drawing current.
  switch (chargeState) {
    case CHARGE_FINISHED:
      chargeBlitText(fb, (LCD_W - CHARGE_TEXT_FULL_W) / 2, CH_TEXT_TOP,
                     CHARGE_TEXT_FULL_W, CHARGE_TEXT_FULL_H, chargeTextFull);
      break;
    case CHARGE_STARTED:
      chargeBlitText(fb, (LCD_W - CHARGE_TEXT_CHARGING_W) / 2, CH_TEXT_TOP,
                     CHARGE_TEXT_CHARGING_W, CHARGE_TEXT_CHARGING_H,
                     chargeTextCharging);
      break;
    default:
      break;
  }

  lcdPushFullFrame(fb);
  // Brightness is the caller's job: the charge loop dims the panel on a timer,
  // and setting a fixed duty cycle here would undo that on every frame.
}

// Same flat black, no gauge: used when the screen times out.
static void drawBlankScreen()
{
  chargeFillRect(LCD_FIRST_FRAME_BUFFER, 0, 0, LCD_W, LCD_H, 0x0000);
  lcdPushFullFrame(LCD_FIRST_FRAME_BUFFER);
}

void battery_charge_end()
{
  if (!chargeLcdReady) return;

  // Hand over on a dark screen instead of switching the panel off. lcdOff()
  // sends the panel's "display off" command, and that state would survive into
  // the boot sequence, leaving the radio with a lit backlight and a dark panel.
  // Nothing else is torn down here: the normal boot path calls lcdInit() again
  // through lcdInitDisplayDriver(), which re-initialises the panel from scratch.
  pixel_t* fb = LCD_FIRST_FRAME_BUFFER;
  chargeFillRect(fb, 0, 0, LCD_W, LCD_H, CH_COLOR_BG);
  lcdPushFullFrame(fb);

  chargeLcdReady = false;
}

// Called from the charge loop every 10 ms. Two independent jobs live here:
// keeping the panel showing the charge gauge, and deciding how bright it should
// be. The RGB strip is NOT touched - board.cpp keeps calling
// updateBatteryState(RGB_STATE_CHARGE) next to this, so the charge indication on
// the strip behaves exactly as it did before the panel gauge existed.
//
// Idle policy: full brightness for CHARGE_DIM_AFTER_MS, dim until
// CHARGE_OFF_AFTER_MS, then a black frame and no further SPI traffic. Plugging
// in / finishing a charge, or tapping the power key, restarts the clock.
void handle_battery_charge(uint32_t pressTime)
{
#if !defined(SIMU)
  static uint32_t updateTime = 0;
  static uint32_t activeTime = 0;   // last time the panel was asked to wake
  static uint32_t lastPress = 0;    // edge detection on the power key
  static uint16_t lastState = CHARGE_UNKNOWN;
  static uint8_t screenLevel = CHARGE_SCR_BRIGHT;

  uint32_t now = timersGetMsTick();
  if (activeTime == 0) activeTime = now;

  uint16_t chargeState = get_battery_charge_state();
  if (chargeState != CHARGE_UNKNOWN) {
    // Plugging in, or the pack reaching full, is worth waking up for. The
    // "lastState + chargeState > 1" test skips the none -> unknown transition.
    if (lastState != chargeState && lastState + chargeState > 1) {
      activeTime = now;
    }
    lastState = chargeState;
  }

  // A tap wakes the panel. Holding the key long enough is handled by board.cpp,
  // which leaves this loop and boots the radio at POWER_ON_DELAY.
  if (pressTime != 0 && pressTime != lastPress) {
    lastPress = pressTime;
    activeTime = now;
  }

  const uint32_t idle = now - activeTime;
  const uint8_t want = idle >= CHARGE_OFF_AFTER_MS ? CHARGE_SCR_DARK
                     : idle >= CHARGE_DIM_AFTER_MS ? CHARGE_SCR_DIM
                                                   : CHARGE_SCR_BRIGHT;

  // One animation step every 500 ms. While dark nothing is pushed at all.
  if (updateTime == 0 || (now - updateTime) >= 500) {
    updateTime = now;

    if (want == CHARGE_SCR_DARK) {
      if (screenLevel != CHARGE_SCR_DARK) {
        drawBlankScreen();
        backlightEnable(CHARGE_BACKLIGHT_DARK);
      }
    } else {
      drawChargingInfo(chargeState);
      backlightEnable(want == CHARGE_SCR_BRIGHT ? CHARGE_BACKLIGHT
                                                : CHARGE_BACKLIGHT_DIM);
    }
    screenLevel = want;
  }
#endif
}

#define MAX_BRIGHT      255
#define CHARGING_BRIGHT  20

void rgbPowerOn(uint8_t color) {
  // 若上次运行时 Lua 保存过 boot 配置，直接用它替换固化在代码里的默认开机灯效。
  // 没有有效配置时返回 false，继续原来的顺序点亮逻辑。
  if (ledBootEarlyFrame(timersGetMsTick())) return;

  // Sequential RGB On
  static uint8_t power_on_step = RGB_STEP_POWER_AROUND;
  switch (power_on_step)
  {
    case RGB_STEP_POWER_AROUND:
      setLedGroupColor(4, color, MAX_BRIGHT);
      setLedGroupColor(5, color, MAX_BRIGHT);
      setLedGroupColor(6, color, MAX_BRIGHT);
      break;

    case RGB_STEP_FUNC1:
      setLedGroupColor(0, color, MAX_BRIGHT);
      break;
      
    case RGB_STEP_FUNC2:
      setLedGroupColor(1, color, MAX_BRIGHT);
      break;   

    case RGB_STEP_FUNC3:
      setLedGroupColor(2, color, MAX_BRIGHT);
      break;  

    case RGB_STEP_FUNC4:
      setLedGroupColor(3, color, MAX_BRIGHT);
      break;  

  default:
    break;
  }
  power_on_step++;
}

#define BRIGHTNESS_MAX       255
void rgbBatteryDis(uint8_t color, uint8_t power_level ) {
  // 真正生效的开机分支走的是 BAT_DIS 灯效；同样用已保存的 boot 配置替换
  if (ledBootEarlyFrame(timersGetMsTick())) return;

  switch (power_level)
  {
  case POWER_LEVEL_CRITICAL:
      setLedGroupColor(0, color, 0);
      setLedGroupColor(1, color, 0);
      setLedGroupColor(2, color, 0);
      setLedGroupColor(3, color, 0);
      break;
  case POWER_LEVEL_LOW:
      setLedGroupColor(0, color, BRIGHTNESS_MAX);
      setLedGroupColor(1, color, 0);
      setLedGroupColor(2, color, 0);
      setLedGroupColor(3, color, 0);
    break;
  case POWER_LEVEL_MEDIUM:
      setLedGroupColor(0, color, BRIGHTNESS_MAX);
      setLedGroupColor(1, color, BRIGHTNESS_MAX);
      setLedGroupColor(2, color, 0);
      setLedGroupColor(3, color, 0);
    break;
  case POWER_LEVEL_HIGH:
      setLedGroupColor(0, color, BRIGHTNESS_MAX);
      setLedGroupColor(1, color, BRIGHTNESS_MAX);
      setLedGroupColor(2, color, BRIGHTNESS_MAX);
      setLedGroupColor(3, color, 0);
    break;
  case POWER_LEVEL_NEAR_FULL:
  case POWER_LEVEL_FULL:
      setLedGroupColor(0, color, BRIGHTNESS_MAX);
      setLedGroupColor(1, color, BRIGHTNESS_MAX);
      setLedGroupColor(2, color, BRIGHTNESS_MAX);
      setLedGroupColor(3, color, BRIGHTNESS_MAX);
    break;
  default:
    break;
  }
    setLedGroupColor(4, color, BRIGHTNESS_MAX);
    setLedGroupColor(5, color, BRIGHTNESS_MAX);
    setLedGroupColor(6, color, BRIGHTNESS_MAX);
}

void rgbBatteryLevelInfo(uint8_t power_level, uint8_t rgb_state) {
  uint8_t color = 0;
  uint8_t breath_index = 0;
  switch (power_level)
  {
  case POWER_LEVEL_CRITICAL:
    color = RGB_COLOR_RED;
    if (rgb_state == RGB_STATE_CHARGE) {
      color = RGB_COLOR_RED;
      breath_index |= RGB_GROUP_MASK_FUNC_1;
      setLedGroupColor(1, color, 0);
      setLedGroupColor(2, color, 0);
      setLedGroupColor(3, color, 0);
    }
    break;

  case POWER_LEVEL_LOW:
    color = RGB_COLOR_PURPLE;
    if (rgb_state == RGB_STATE_CHARGE) {
      breath_index |= RGB_GROUP_MASK_FUNC_1;
      setLedGroupColor(1, color, 0);
      setLedGroupColor(2, color, 0);
      setLedGroupColor(3, color, 0);
    }
    break; 

  case POWER_LEVEL_MEDIUM:
    color = RGB_COLOR_YELLOW;
    if (rgb_state == RGB_STATE_CHARGE) {
      breath_index |= RGB_GROUP_MASK_FUNC_2;
      setLedGroupColor(0, color, CHARGING_BRIGHT);
      setLedGroupColor(2, color, 0);
      setLedGroupColor(3, color, 0);
    }
    break;  

  case POWER_LEVEL_HIGH:
    color = RGB_COLOR_PINK;
    if (rgb_state == RGB_STATE_CHARGE) {
      breath_index |= RGB_GROUP_MASK_FUNC_3;
      setLedGroupColor(0, color, CHARGING_BRIGHT);
      setLedGroupColor(1, color, CHARGING_BRIGHT);
      setLedGroupColor(3, color, 0);
    }
    break;  

  case POWER_LEVEL_NEAR_FULL:
    color = RGB_COLOR_PINK;
    if (rgb_state == RGB_STATE_CHARGE) {
      breath_index |= RGB_GROUP_MASK_FUNC_4;
      setLedGroupColor(0, color, CHARGING_BRIGHT);
      setLedGroupColor(1, color, CHARGING_BRIGHT);
      setLedGroupColor(2, color, CHARGING_BRIGHT);
    }
    break;  

  case POWER_LEVEL_FULL:
    color = RGB_COLOR_PINK;
    if (rgb_state == RGB_STATE_CHARGE) {
      setLedGroupColor(0, color, CHARGING_BRIGHT);
      setLedGroupColor(1, color, CHARGING_BRIGHT);
      setLedGroupColor(2, color, CHARGING_BRIGHT);
      setLedGroupColor(3, color, CHARGING_BRIGHT);
    }
    break;  
  default:
    break;
  }

  switch (rgb_state)
  {
  case RGB_STATE_BREATH:
    ledSetGroup(RGB_GROUP_MASK_ALL);
    break;

  case RGB_STATE_CHARGE:
    breath_index |= RGB_GROUP_MASK_AROUND_L | RGB_GROUP_MASK_AROUND_R | RGB_GROUP_MASK_POWER;
    ledSetGroup(breath_index);
    break;
  
  case RGB_STATE_POWER_ON:
    rgbPowerOn(color);
    break;
  case RGB_STATE_BAT_DIS:
    rgbBatteryDis(color, power_level);
  default:
    break;
  }

  ledSetState(rgb_state);
  ledSetColor(color);
}

#define BREATH_INTERVAL 7
#define GAMMA       6
#define PI 3.14159265358979323846f
#define BREATH_STEP 0.0089759771428571  // PI / 350.0f; // step size

typedef struct 
{
  uint8_t led_color;
  uint8_t led_state;
  uint8_t led_group;
} ledInfo_t;

// used to map all led number in the rgbled chain
// function 1~4 power right left
uint8_t rgbMapping[] = {4, 6, 0, 2, 8, 12, 10};

ledInfo_t led_info = {0};

void ledSetColor(uint8_t color) {
  led_info.led_color = color;
}

void ledSetState(uint8_t state) {
  led_info.led_state = state;
}

void ledSetGroup(uint8_t group) {
  led_info.led_group = group;
}

void setLedGroupColor(uint8_t index, uint8_t color, uint8_t brightness) {

  uint8_t scaled_r = 0;
  uint8_t scaled_g = 0;
  uint8_t scaled_b = 0;

  // Pink is a fixed-ratio color (R:G:B = 20:6:13), not an RGB bitmask.
  // Power-key LED (group 4 -> strip 8/9) is always pink; other groups are
  // pink whenever the battery-level logic tags them with RGB_COLOR_PINK.
  if (color == RGB_COLOR_PINK || index == 4) {
    scaled_r = brightness;
    scaled_g = (uint8_t)(brightness * 6 / 20);
    scaled_b = (uint8_t)(brightness * 13 / 20);
    rgbSetLedColor(rgbMapping[index], scaled_r, scaled_g, scaled_b);
    rgbSetLedColor(rgbMapping[index] + 1, scaled_r, scaled_g, scaled_b);
    return;
  }

  if (color & RGB_COLOR_RED) {
    scaled_r = brightness;
  }
  if (color & RGB_COLOR_GREEN) {
    scaled_g = brightness;
  }
  if (color & RGB_COLOR_BLUE) {
    scaled_b = brightness;
  }

  rgbSetLedColor(rgbMapping[index], scaled_r, scaled_g, scaled_b);
  rgbSetLedColor(rgbMapping[index] + 1, scaled_r, scaled_g, scaled_b);
}

uint8_t ledBreathBright(float angle, uint8_t maxBright) {
  float brightness = sin(angle) * 0.5 + 0.5; // map range 0-1
  brightness = pow(brightness, GAMMA); // gamma correction
  uint8_t bright = (uint8_t)(brightness * maxBright);
  return bright;
}

void ledBreathUpdate(uint8_t state, uint8_t color, uint8_t group) {
  static uint32_t breath_tick = 0;
  
  if (timersGetMsTick() - breath_tick < BREATH_INTERVAL && breath_tick != 0) {
    if (state == RGB_STATE_BREATH || state == RGB_STATE_CHARGE) {
      return;
    }
  }
  breath_tick = timersGetMsTick();
  uint8_t bright = 0;
  static float breath_angle = 0;
 
  bright = ledBreathBright(breath_angle, state == RGB_STATE_CHARGE ? CHARGING_BRIGHT : MAX_BRIGHT);
  breath_angle += BREATH_STEP;
  if (breath_angle > PI) breath_angle = 0; // reset angle

  if (group & RGB_GROUP_MASK_FUNC_1)    setLedGroupColor(0, color, bright);
  if (group & RGB_GROUP_MASK_FUNC_2)    setLedGroupColor(1, color, bright);
  if (group & RGB_GROUP_MASK_FUNC_3)    setLedGroupColor(2, color, bright);
  if (group & RGB_GROUP_MASK_FUNC_4)    setLedGroupColor(3, color, bright);
  if (group & RGB_GROUP_MASK_POWER)     setLedGroupColor(4, color, bright);
  if (group & RGB_GROUP_MASK_AROUND_L)  setLedGroupColor(5, color, bright);
  if (group & RGB_GROUP_MASK_AROUND_R)  setLedGroupColor(6, color, bright);

  rgbLedColorApply();
}

void ledLoop(void) {
  // 场景引擎生效时由它接管所有灯效
  if (ledSceneTick()) return;

  if (led_info.led_state == RGB_STATE_OFF) {
    led_info.led_state = RGB_STATE_NONE;
    rgbLedClearAll();
    return;
  } else if ( led_info.led_state == RGB_STATE_POWER_ON || led_info.led_state == RGB_STATE_BAT_DIS ) {
    rgbLedColorApply();
    return;
  } else if (led_info.led_state == RGB_STATE_NONE) {
    return;
  }
  ledBreathUpdate(led_info.led_state, led_info.led_color, led_info.led_group);
}

void rgbChargeInit(void) {
  // Bring up the WS2812 strip without starting the periodic refresh timer
  // (the FreeRTOS scheduler isn't running yet during the charging UI).
  rgbLedHwInit();
}

constexpr uint16_t vbatLedTable[] = {650, 720, 760, 800, 823 };
void updateBatteryState(uint8_t rgb_state) {
  uint16_t  bat_v = getBatteryVoltage()*BAT_VOL_FACTOR;
  uint8_t power_level = POWER_LEVEL_NONE;
  static uint8_t last_power_level = POWER_LEVEL_NONE;

  if (bat_v < vbatLedTable[0]) {
    power_level = POWER_LEVEL_CRITICAL;
  } else if (bat_v < vbatLedTable[1]) {
    power_level = POWER_LEVEL_LOW;
  } else if (bat_v < vbatLedTable[2]) {
    power_level = POWER_LEVEL_MEDIUM;
  } else if (bat_v < vbatLedTable[3]) {
    power_level = POWER_LEVEL_HIGH;
  }else if (bat_v < vbatLedTable[4]) {
    power_level = POWER_LEVEL_NEAR_FULL;
  } else {
    power_level = POWER_LEVEL_FULL;
  }

  if( last_power_level != POWER_LEVEL_NONE )
  {
    if(power_level<last_power_level)
    {
      power_level=last_power_level;
    }
  }

  rgbBatteryLevelInfo(power_level, rgb_state);
  ledLoop();
  last_power_level = power_level;
}

