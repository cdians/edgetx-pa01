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

#pragma once

#include "lvgl/lvgl.h"

class LvglWrapper
{
 public:
  static LvglWrapper* instance();

  // Called from UI task: executes the LVGL timer handler 
  void run();

 protected:
  static LvglWrapper *_instance;

  LvglWrapper();
  ~LvglWrapper() {}
};

// Bring up the display + input drivers only, without building the main UI.
//
// The charging screen runs while the radio is still "off": the EEPROM has not
// been read yet, so g_eeGeneral / g_model are all zeroes. Creating MainWindow
// and ViewMain at that point would build the whole model view from empty data,
// which is both pointless (the charging screen covers it) and risky. This entry
// point stops right after the drivers, leaving the default screen free for the
// caller to draw on.
//
// Calling LvglWrapper::instance() afterwards is still correct: the driver setup
// is guarded, so only MainWindow / ViewMain get created then.
void lvglInitDisplayOnly();

#if defined(VOICE_CONTROL_SENSOR)
void lvglRequestKeyboardEnter();
#endif
