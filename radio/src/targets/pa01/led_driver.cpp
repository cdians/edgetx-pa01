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

#include "hal/gpio.h"
#include "hal/rgbleds.h"
#include "stm32_gpio.h"

#include "boards/generic_stm32/rgb_leds.h"
#include "board.h"
#include "led_scene.h"

#define GET_RED(color) (((color) & 0xF80000) >>16)
#define GET_GREEN(color) (((color) & 0x000F800) >> 8)
#define GET_BLUE(color) (((color) & 0xF8))

// used to map switch number to led number in the rgbled chain
static const uint8_t ledMapping[] = {4, 6, 0, 2};

uint8_t fsLedFirstIndex(uint8_t index)
{
  return ledMapping[index];
}

void fsLedRGB(uint8_t index, uint32_t color)
{
  // 场景引擎接管时，功能开关灯由 led_scene 负责
  if (ledSceneEnabled()) return;

  rgbSetLedColor(fsLedFirstIndex(index), GET_RED(color),
     GET_GREEN(color),GET_BLUE(color));
  rgbSetLedColor(fsLedFirstIndex(index)+1, GET_RED(color),
     GET_GREEN(color),GET_BLUE(color));
  rgbLedColorApply();
}

void ledOff()
{
  rgbSetLedColor(8, 0, 0, 0);
  rgbSetLedColor(9, 0, 0, 0);
  rgbLedColorApply();
}

// PA01 power-key LED (indices 8/9) is fixed to a deeper pink (R:G:B = 20:6:13),
// independent of the status phase selected by checkStatusLed().
static void ledPowerPink()
{
  rgbSetLedColor(8, 20, 6, 13);
  rgbSetLedColor(9, 20, 6, 13);
  rgbLedColorApply();
}

void ledRed()
{
  ledPowerPink();
}

void ledGreen()
{
  ledPowerPink();
}

void ledBlue()
{
  ledPowerPink();
}

void ledBoot()
{
  ledPowerPink();
}
