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

#include "trims.h"

#include "bitmaps.h"
#include "input_mapping.h"
#include "edgetx.h"
#include "sliders.h"

class TrimIcon : public SliderIcon
{
 public:
  TrimIcon(Window* parent, bool isVertical) : SliderIcon(parent)
  {
#if defined(PCBPA01)
    // A trim sits in the same slot as a slider, 12px away, so one cue is not
    // enough. Two: size (trim 10px against the slider's 15px) and colour (the
    // muted rail slate against the pink accent). Out-of-range still turns the
    // trim red.
    if (knob) {
      constexpr coord_t TRIM_KNOB = 10;
      lv_obj_set_size(knob, TRIM_KNOB, TRIM_KNOB);
      lv_obj_set_pos(knob, (SliderIcon::SLIDER_ICON_SIZE - TRIM_KNOB) / 2,
                     (SliderIcon::SLIDER_ICON_SIZE - TRIM_KNOB) / 2);
      // Rounded square, not a disc. Shape is the third cue on top of size and
      // colour: a 10px square reads as a different *kind* of handle than the
      // 15px pink disc even when you only catch it out of the corner of your eye.
      lv_obj_set_style_radius(knob, 3, LV_PART_MAIN);
      etx_solid_bg(knob, COLOR_THEME_SECONDARY2_INDEX);
      etx_bg_color(knob, COLOR_THEME_WARNING_INDEX, LV_STATE_USER_1);
      // A 1px rim keeps the dot readable where it overlaps the rail.
      lv_obj_set_style_border_width(knob, 1, LV_PART_MAIN);
      etx_border_color(knob, COLOR_THEME_PRIMARY2_INDEX);
    }
    return;
#else
    lv_coord_t lw = SLIDER_MASK_SIZE > 15 ? 2 : 1;
    lv_coord_t x1 = SLIDER_MASK_SIZE / 5 + 1;
    lv_coord_t x2 = SLIDER_ICON_SIZE - x1;
    lv_coord_t y1 = (SLIDER_MASK_SIZE - lw * 2) / 3 + 1;
    lv_coord_t y2 = SLIDER_MASK_SIZE + 1 - y1;
    if (lw > 1) y2 += 1;
    if (isVertical) {
      barPoints[0] = {x1, y1};
      barPoints[1] = {x2, y1};
      barPoints[2] = {x1, y2};
      barPoints[3] = {x2, y2};
    } else {
      barPoints[0] = {y2, x1};
      barPoints[1] = {y2, x2};
      barPoints[2] = {y1, x1};
      barPoints[3] = {y1, x2};
    }

    bar1 = lv_line_create(lvobj);
    etx_obj_add_style(bar1, styles->div_line_white, LV_PART_MAIN);
    lv_obj_set_style_line_width(bar1, lw, LV_PART_MAIN);
    lv_line_set_points(bar1, &barPoints[0], 2);
    bar2 = lv_line_create(lvobj);
    etx_obj_add_style(bar2, styles->div_line_white, LV_PART_MAIN);
    lv_obj_set_style_line_width(bar2, lw, LV_PART_MAIN);
    lv_line_set_points(bar2, &barPoints[2], 2);

    etx_img_color(mask, COLOR_THEME_ACTIVE_INDEX, LV_STATE_USER_1);
#endif
  }

  void setState(int value)
  {
#if defined(PCBPA01)
    if (value < TRIM_MIN || value > TRIM_MAX)
      lv_obj_add_state(knob, LV_STATE_USER_1);
    else
      lv_obj_clear_state(knob, LV_STATE_USER_1);
#else
    if (value < TRIM_MIN || value > TRIM_MAX) {
      lv_obj_add_state(mask, LV_STATE_USER_1);
      lv_obj_add_state(bar1, LV_STATE_USER_1);
      lv_obj_add_state(bar2, LV_STATE_USER_1);
    } else {
      lv_obj_clear_state(mask, LV_STATE_USER_1);
      lv_obj_clear_state(bar1, LV_STATE_USER_1);
      lv_obj_clear_state(bar2, LV_STATE_USER_1);
    }

    if (value >= 0)
      lv_obj_clear_flag(bar1, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(bar1, LV_OBJ_FLAG_HIDDEN);

    if (value <= 0)
      lv_obj_clear_flag(bar2, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(bar2, LV_OBJ_FLAG_HIDDEN);
#endif
  }

 protected:
  lv_obj_t* bar1 = nullptr;
  lv_obj_t* bar2 = nullptr;
  lv_point_t barPoints[4];
};

MainViewTrim::MainViewTrim(Window* parent, const rect_t& rect, uint8_t idx,
                           bool isVertical) :
    Window(parent, rect), idx(idx), isVertical(isVertical)
{
  trimBar = lv_obj_create(lvobj);
#if defined(PCBPA01)
  // Match the slider rail in colour, width, radius *and* span: a trim sitting
  // in the same slot as a slider then reads as one track with two knobs
  // instead of two nested bars of different sizes.
  etx_solid_bg(trimBar, COLOR_THEME_SECONDARY1_INDEX);
  etx_obj_add_style(trimBar, styles->rounded, LV_PART_MAIN);
  constexpr coord_t rw = MainViewSlider::RAIL_W;
  if (isVertical) {
    lv_obj_set_pos(trimBar, (SliderIcon::SLIDER_ICON_SIZE - rw) / 2, 0);
    lv_obj_set_size(trimBar, rw, height());
  } else {
    lv_obj_set_pos(trimBar, 0, (SliderIcon::SLIDER_ICON_SIZE - rw) / 2);
    lv_obj_set_size(trimBar, width(), rw);
  }
#else
  etx_solid_bg(trimBar, COLOR_THEME_SECONDARY1_INDEX);
  etx_obj_add_style(trimBar, styles->rounded, LV_PART_MAIN);
  if (isVertical) {
    lv_obj_set_pos(trimBar, (SliderIcon::SLIDER_ICON_SIZE - TRIM_LINE_WIDTH) / 2,
                   SliderIcon::SLIDER_ICON_SIZE / 2);
    lv_obj_set_size(trimBar, TRIM_LINE_WIDTH,
                    MainViewSlider::VERTICAL_SLIDERS_HEIGHT - SliderIcon::SLIDER_ICON_SIZE + 1);
  } else {
    lv_obj_set_pos(trimBar, SliderIcon::SLIDER_ICON_SIZE / 2,
                   (SliderIcon::SLIDER_ICON_SIZE - TRIM_LINE_WIDTH) / 2);
    lv_obj_set_size(trimBar, MainViewSlider::HORIZONTAL_SLIDERS_WIDTH - SliderIcon::SLIDER_ICON_SIZE + 1,
                    TRIM_LINE_WIDTH);
  }
#endif

#if defined(PCBPA01)
  // Centre cue: a 14x2px accent bar across the rail. It is created before the
  // knob so the 10px knob covers its middle when home, leaving a 2px stub on
  // each side; it is hidden whenever the trim is off centre.
  centerMark = lv_obj_create(lvobj);
  lv_obj_remove_style_all(centerMark);
  lv_obj_clear_flag(centerMark, LV_OBJ_FLAG_CLICKABLE);
  etx_solid_bg(centerMark, COLOR_THEME_FOCUS_INDEX);
  lv_obj_set_style_radius(centerMark, 1, LV_PART_MAIN);
  if (isVertical) {
    lv_obj_set_size(centerMark, 14, 2);
    lv_obj_set_pos(centerMark, (width() - 14) / 2, height() / 2 - 1);
  } else {
    lv_obj_set_size(centerMark, 2, 14);
    lv_obj_set_pos(centerMark, width() / 2 - 1, (height() - 14) / 2);
  }
  lv_obj_add_flag(centerMark, LV_OBJ_FLAG_HIDDEN);
#endif

  trimIcon = new TrimIcon(this, isVertical);

  trimValue = new DynamicNumber<int16_t>(
      this, {0, 0, SliderIcon::SLIDER_ICON_SIZE, SliderIcon::SLIDER_MASK_SIZE},
      [=]() { return divRoundClosest(abs(value) * 100, trimMax); },
      COLOR_THEME_ON_DARK_INDEX, FONT(XXS) | CENTERED);
  etx_solid_bg(trimValue->getLvObj(), COLOR_THEME_SECONDARY1_INDEX);
  trimValue->padTop(TRIM_VAL_YO);
  trimValue->hide();

  setRange();
  setPos();
}

void MainViewTrim::setRange()
{
  extendedTrims = g_model.extendedTrims;
  if (extendedTrims) {
    trimMin = TRIM_EXTENDED_MIN;
    trimMax = TRIM_EXTENDED_MAX;
  } else {
    trimMin = TRIM_MIN;
    trimMax = TRIM_MAX;
  }
}

void MainViewTrim::setPos()
{
  coord_t x = sx();
  coord_t y = sy();

  lv_obj_set_pos(trimIcon->getLvObj(), x, y);
  trimIcon->setState(value);

#if defined(PCBPA01)
  if (value == 0)
    lv_obj_clear_flag(centerMark, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(centerMark, LV_OBJ_FLAG_HIDDEN);
#endif

  if ((g_model.displayTrims == DISPLAY_TRIMS_ALWAYS) ||
      ((g_model.displayTrims == DISPLAY_TRIMS_CHANGE) &&
       (trimsDisplayTimer > 0) && (trimsDisplayMask & (1 << idx)))) {
    if (value) {
      if (isVertical) {
        x = 0;
        y = (value > 0) ? MainViewSlider::VERTICAL_SLIDERS_HEIGHT - SliderIcon::SLIDER_MASK_SIZE * 2
                        : SliderIcon::SLIDER_MASK_SIZE;
      } else {
        x = ((value < 0) ? MainViewSlider::HORIZONTAL_SLIDERS_WIDTH - SliderIcon::SLIDER_MASK_SIZE - SliderIcon::SLIDER_ICON_SIZE
                         : SliderIcon::SLIDER_MASK_SIZE);
        y = 1;
      }
      lv_obj_set_pos(trimValue->getLvObj(), x, y);
      trimValue->show();
      showChange = true;
    } else {
      trimValue->hide();
    }
  } else {
    showChange = false;
    trimValue->hide();
  }
}

void MainViewTrim::setVisible(bool visible)
{
  hidden = !visible;
  setDisplayState();
}

bool MainViewTrim::setDisplayState()
{
  trim_t v = getRawTrimValue(mixerCurrentFlightMode, inputMappingConvertMode(idx));
  if (hidden || v.mode == TRIM_MODE_NONE || v.mode == TRIM_MODE_3POS) {
    // Hide trim if not being used
    hide();
    return false;
  } else {
    show();
  }

  return true;
}

void MainViewTrim::checkEvents()
{
  Window::checkEvents();

  // Do nothing if trims turned off
  if (hidden) return;

  // Don't update if not visible
  if (!setDisplayState()) return;

  int newValue = getTrimValue(mixerCurrentFlightMode, inputMappingConvertMode(idx));

  bool update = false;

  if (extendedTrims != g_model.extendedTrims) {
    update = true;
    setRange();
    newValue = limit(trimMin, newValue, trimMax);
  }

  if (update || value != newValue || (g_model.displayTrims == DISPLAY_TRIMS_CHANGE &&
                            showChange && trimsDisplayTimer == 0)) {
    value = newValue;
    setPos();
  }
}

coord_t MainViewTrim::sx()
{
  if (isVertical) return 0;

  return divRoundClosest(
      (MainViewSlider::HORIZONTAL_SLIDERS_WIDTH - SliderIcon::SLIDER_ICON_SIZE) * (value - trimMin),
      trimMax - trimMin);
}

coord_t MainViewTrim::sy()
{
  if (!isVertical) return 0;

  return divRoundClosest(
             (MainViewSlider::VERTICAL_SLIDERS_HEIGHT - SliderIcon::SLIDER_ICON_SIZE) * (trimMax - value),
             trimMax - trimMin);
}

MainViewHorizontalTrim::MainViewHorizontalTrim(Window* parent, uint8_t idx) :
    MainViewTrim(parent,
                 rect_t{0, 0, MainViewSlider::HORIZONTAL_SLIDERS_WIDTH, SliderIcon::SLIDER_ICON_SIZE}, idx,
                 false)
{
}

MainViewVerticalTrim::MainViewVerticalTrim(Window* parent, uint8_t idx) :
    MainViewTrim(parent,
                 rect_t{0, 0, SliderIcon::SLIDER_ICON_SIZE, MainViewSlider::VERTICAL_SLIDERS_HEIGHT}, idx,
                 true)
{
}
