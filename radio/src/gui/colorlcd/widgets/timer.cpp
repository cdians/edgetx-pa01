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

#include "widget.h"

#include "bitmaps.h"
#include "edgetx.h"
#include "static.h"

#define ETX_STATE_WARNING    LV_STATE_USER_1
#define EXT_NAME_ALIGN_RIGHT LV_STATE_USER_2
#define ETX_NAME_COLOR_WHITE LV_STATE_USER_3
#define ETX_SMALL_FONT       LV_STATE_USER_4

class TimerWidget : public Widget
{
 public:
  TimerWidget(const WidgetFactory* factory, Window* parent, const rect_t& rect,
              int screenNum, int zoneNum) :
      Widget(factory, parent, rect, screenNum, zoneNum)
  {
    updateMsg.subscribe(Messaging::UPDATE_TIMER, [=](uint32_t p) {
      if (p == getPersistentData()->options[0].value.unsignedValue)
        update();
    });
    delayLoad();
  }

  void delayedInit() override
  {
    etx_solid_bg(lvobj, COLOR_THEME_WARNING_INDEX,
                 LV_PART_MAIN | ETX_STATE_WARNING);

    // Timer name
    nameLabel = etx_label_create(lvobj, FONT_XS_INDEX);
    lv_label_set_text(nameLabel, "");
    etx_obj_add_style(nameLabel, styles->text_align_left, LV_PART_MAIN);
    etx_obj_add_style(nameLabel, styles->text_align_right,
                      LV_PART_MAIN | EXT_NAME_ALIGN_RIGHT);
    etx_txt_color(nameLabel, COLOR_THEME_SECONDARY1_INDEX);
#if defined(PCBPA01)
    // SECONDARY1 is a surface colour in the PA01 palette, so the timer name
    // needs the light text colour to stay readable on the dark background.
    etx_txt_color(nameLabel, COLOR_THEME_PRIMARY1_INDEX);
#endif
    etx_txt_color(nameLabel, COLOR_THEME_SECONDARY2_INDEX,
                  LV_PART_MAIN | ETX_STATE_WARNING);
    etx_txt_color(nameLabel, COLOR_THEME_PRIMARY2_INDEX,
                  LV_PART_MAIN | ETX_NAME_COLOR_WHITE);
    etx_font(nameLabel, FONT_XXS_INDEX, LV_PART_MAIN | ETX_SMALL_FONT);

    update();
    foreground();
  }

  void foreground() override
  {
    if (!loaded || deleted()) return;

    auto widgetData = getPersistentData();

    uint32_t index = widgetData->options[0].value.unsignedValue;
    TimerData& timerData = g_model.timers[index];
    TimerState& timerState = timersStates[index];

    if (lastValue != timerState.val || lastStartValue != timerData.start) {
      lastValue = timerState.val;
      lastStartValue = timerData.start;

      int val = lastValue;
      if (lastStartValue && timerData.showElapsed &&
          (int)lastStartValue != lastValue)
        val = (int)lastStartValue - lastValue;

      if (isLarge) {
#if defined(PCBPA01)
        updateCard(val, timerData.showElapsed);
        if (digits0) {
          lv_obj_add_flag(digits0, LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(digits1, LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(unit0, LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(unit1, LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(minusLabel, LV_OBJ_FLAG_HIDDEN);
        }
#else
        splitTimer(abs(val));

        lv_label_set_text(digits0, sDigit0);
        lv_label_set_text(digits1, sDigit1);
        lv_label_set_text(unit0, sUnit0);
        lv_label_set_text(unit1, sUnit1);

        if (val < 0)
          lv_obj_clear_flag(minusLabel, LV_OBJ_FLAG_HIDDEN);
        else
          lv_obj_add_flag(minusLabel, LV_OBJ_FLAG_HIDDEN);

        if (lastValue > 0 && lastStartValue > 0) {
          int pieEnd = 360 * (lastStartValue - lastValue) / lastStartValue;
          if (!timerData.showElapsed) {
            pieEnd = 360 - pieEnd;
          }
          lv_arc_set_end_angle(timerArc, pieEnd);

          lv_obj_clear_flag(timerArc, LV_OBJ_FLAG_HIDDEN);
          timerIcon->hide();
        } else {
          lv_obj_add_flag(timerArc, LV_OBJ_FLAG_HIDDEN);
          timerIcon->show();
        }
#endif
      } else {
        char str[LEN_TIMER_STRING];

        TimerOptions timerOptions;
        timerOptions.options = (abs(val) >= 3600) ? SHOW_TIME : SHOW_TIMER;
        getTimerString(str, val, timerOptions);
        lv_label_set_text(valLabel, str);

        if (width() <= SMALL_TXT_MAX_W && height() <= SMALL_TXT_MAX_H && abs(val) >= 3600)
          lv_obj_add_state(valLabel, ETX_SMALL_FONT);
        else
          lv_obj_clear_state(valLabel, ETX_SMALL_FONT);
      }
    }

    // Set colors if timer has elapsed.
    bool warning = (lastValue < 0) && BLINK_ON_PHASE;
    if (warning != lastWarning) {
      lastWarning = warning;
      if (warning) {
        if (isLarge) {
          lv_obj_add_state(nameLabel, ETX_STATE_WARNING);
#if defined(PCBPA01)
          if (cardVal)
            lv_obj_add_state(cardVal, ETX_STATE_WARNING);
          if (cardFill)
            lv_obj_add_state(cardFill, ETX_STATE_WARNING);
#else
          lv_obj_add_state(digits0, ETX_STATE_WARNING);
          lv_obj_add_state(digits1, ETX_STATE_WARNING);
          lv_obj_add_state(unit0, ETX_STATE_WARNING);
          lv_obj_add_state(unit1, ETX_STATE_WARNING);
          lv_obj_add_state(minusLabel, ETX_STATE_WARNING);
#endif
          lv_obj_clear_state(lvobj, ETX_STATE_WARNING);
          if (timerBg)
            lv_obj_add_state(timerBg, ETX_STATE_WARNING);
          if (timerIcon)
            timerIcon->setColor(COLOR_THEME_SECONDARY2_INDEX);
        } else {
          lv_obj_add_state(lvobj, ETX_STATE_WARNING);
        }
      } else {
        if (isLarge) {
          lv_obj_clear_state(nameLabel, ETX_STATE_WARNING);
#if defined(PCBPA01)
          if (cardVal)
            lv_obj_clear_state(cardVal, ETX_STATE_WARNING);
          if (cardFill)
            lv_obj_clear_state(cardFill, ETX_STATE_WARNING);
#else
          lv_obj_clear_state(digits0, ETX_STATE_WARNING);
          lv_obj_clear_state(digits1, ETX_STATE_WARNING);
          lv_obj_clear_state(unit0, ETX_STATE_WARNING);
          lv_obj_clear_state(unit1, ETX_STATE_WARNING);
          lv_obj_clear_state(minusLabel, ETX_STATE_WARNING);
#endif
          if (timerBg)
            lv_obj_clear_state(timerBg, ETX_STATE_WARNING);
          if (timerIcon)
            timerIcon->setColor(COLOR_THEME_SECONDARY1_INDEX);
#if defined(PCBPA01)
          if (timerIcon)
            timerIcon->setColor(COLOR_THEME_PRIMARY1_INDEX);
#endif
        }
        lv_obj_clear_state(lvobj, ETX_STATE_WARNING);
      }
    }
  }

  static const WidgetOption options[];

  static LAYOUT_VAL_SCALED(SMALL_TXT_MAX_W, 100)
  static LAYOUT_VAL_SCALED(SMALL_TXT_MAX_H, 40)
  static LAYOUT_VAL_SCALED(VAL_LBL_Y, 20)
  static LAYOUT_VAL_SCALED(TMR_LRG_W, 174)
  static LAYOUT_VAL_SCALED(TMR_LRG_H, 70)
  static LAYOUT_VAL_SCALED(TMR_BG_RX, 35)
  static LAYOUT_VAL_SCALED(TMR_BG_RY, 14)
  static LAYOUT_VAL_SCALED(TMR_ARC_SZ, 64)
  static LAYOUT_VAL_SCALED(TMR_ARC_W, 10)
  static LAYOUT_VAL_SCALED(NM_LRG_X, 71)
  static LAYOUT_VAL_SCALED(NM_LRG_Y, 19)
  static LAYOUT_VAL_SCALED(NM_LRG_W, 100)
  static LAYOUT_VAL_SCALED(U0_X, 110)
  static LAYOUT_VAL_SCALED(U1_X, 160)
  static LAYOUT_VAL_SCALED(U_Y, 33)
  static LAYOUT_VAL_SCALED(D0_X, 74)
  static LAYOUT_VAL_SCALED(D1_X, 124)
  static LAYOUT_VAL_SCALED(D_Y, 31)
  static constexpr coord_t TMR_ARC_PAD = (TMR_LRG_H - TMR_ARC_SZ + 1) / 2;

 protected:
  tmrval_t lastValue = 0;
  uint32_t lastStartValue = -1;
  int8_t lastWarning = -1;
  bool isLarge = false;
  lv_obj_t* nameLabel = nullptr;
  lv_obj_t* valLabel = nullptr;
  lv_obj_t* digits0 = nullptr;
  lv_obj_t* digits1 = nullptr;
  lv_obj_t* unit0 = nullptr;
  lv_obj_t* unit1 = nullptr;
  lv_obj_t* minusLabel = nullptr;
  lv_obj_t* timerArc = nullptr;
  lv_obj_t* timerBg = nullptr;
#if defined(PCBPA01)
  // Card UI: one plate, a "T1" pill, a single big value label and a progress
  // groove along the bottom edge.
  lv_obj_t* cardPlate = nullptr;
  lv_obj_t* cardPill = nullptr;
  lv_obj_t* cardVal = nullptr;
  lv_obj_t* cardGroove = nullptr;
  lv_obj_t* cardFill = nullptr;
  bool cardUI = false;
  static constexpr coord_t CARD_PAD = PAD_MEDIUM;
  static constexpr coord_t CARD_BAR_H = 4;
#endif
  static uint8_t timerBgBuffer[TMR_LRG_W * TMR_LRG_H];
  static bool timerBgDrawn;
  StaticIcon* timerIcon = nullptr;
  Messaging updateMsg;

  void update() override
  {
    if (!loaded || deleted()) return;

    auto widgetData = getPersistentData();

    // Set up widget from options.
    char s[16];

    uint32_t index = widgetData->options[0].value.unsignedValue;
    TimerData& timerData = g_model.timers[index];

    // name
    if (timerData.name[0]) {
      strAppend(s, timerData.name, LEN_TIMER_NAME);
    } else {  // user name not exist "TMRn"
      formatNumberAsString(s, 16, index + 1, 1, 0, "TMR");
    }

    lv_label_set_text(nameLabel, s);

    // Force value and color refresh
    lastStartValue = -1;
    lastWarning = -1;

    if (width() >= TMR_LRG_W && height() >= TMR_LRG_H) {
      isLarge = true;
#if defined(PCBPA01)
      showCardUI();
#else
      showLargeUI();
#endif
    } else {
      isLarge = false;
      showSmallUI();

      if (getTextWidth(s, 0, FONT(XS)) > (width() - PAD_TINY))
        lv_obj_add_state(nameLabel, ETX_SMALL_FONT);
      else
        lv_obj_clear_state(nameLabel, ETX_SMALL_FONT);
    }
  }

  lv_obj_t* createUnitLabel()
  {
    auto lbl = etx_label_create(lvobj);
    lv_label_set_text(lbl, "");
    etx_txt_color(lbl, COLOR_THEME_SECONDARY1_INDEX);
#if defined(PCBPA01)
    etx_txt_color(lbl, COLOR_THEME_PRIMARY1_INDEX);
#endif
    etx_txt_color(lbl, COLOR_THEME_SECONDARY2_INDEX,
                  LV_PART_MAIN | ETX_STATE_WARNING);

    return lbl;
  }

  lv_obj_t* createDigitsLabel()
  {
    auto lbl = createUnitLabel();
    etx_font(lbl, FONT_XL_INDEX);

    return lbl;
  }

  void showLargeUI()
  {
    hideSmallUI();
    createLargeUI();

    lv_obj_add_state(nameLabel, EXT_NAME_ALIGN_RIGHT);
    lv_obj_set_pos(nameLabel, NM_LRG_X, NM_LRG_Y);
    lv_obj_set_width(nameLabel, NM_LRG_W);
    lv_obj_clear_state(nameLabel, ETX_NAME_COLOR_WHITE);

    lv_obj_clear_flag(digits0, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(digits1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(unit0, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(unit1, LV_OBJ_FLAG_HIDDEN);
    if (timerBg)
      lv_obj_clear_flag(timerBg, LV_OBJ_FLAG_HIDDEN);
  }

  void hideLargeUI()
  {
    if (digits0)
      lv_obj_add_flag(digits0, LV_OBJ_FLAG_HIDDEN);
    if (digits1)
      lv_obj_add_flag(digits1, LV_OBJ_FLAG_HIDDEN);
    if (unit0)
      lv_obj_add_flag(unit0, LV_OBJ_FLAG_HIDDEN);
    if (unit1)
      lv_obj_add_flag(unit1, LV_OBJ_FLAG_HIDDEN);
    if (minusLabel)
      lv_obj_add_flag(minusLabel, LV_OBJ_FLAG_HIDDEN);
    if (timerBg)
      lv_obj_add_flag(timerBg, LV_OBJ_FLAG_HIDDEN);
    if (timerArc)
      lv_obj_add_flag(timerArc, LV_OBJ_FLAG_HIDDEN);
    if (timerIcon)
      timerIcon->hide();
#if defined(PCBPA01)
    hideCardUI();
#endif
  }

  void createLargeUI()
  {
    if (!timerBg) {
      timerBg = lv_canvas_create(lvobj);
      if (timerBg) {
        lv_obj_move_background(timerBg);
        lv_canvas_set_buffer(timerBg, timerBgBuffer, TMR_LRG_W,
                             TMR_LRG_H, LV_IMG_CF_ALPHA_8BIT);
        etx_img_color(timerBg, COLOR_THEME_PRIMARY2_INDEX, LV_PART_MAIN);
        etx_img_color(timerBg, COLOR_THEME_WARNING_INDEX, LV_PART_MAIN | ETX_STATE_WARNING);
        lv_obj_set_style_img_opa(timerBg, LV_OPA_70, LV_PART_MAIN);

        if (!timerBgDrawn) {
          timerBgDrawn = true;
          lv_canvas_fill_bg(timerBg, lv_color_black(), LV_OPA_TRANSP);
          lv_draw_rect_dsc_t rect_dsc;
          lv_draw_rect_dsc_init(&rect_dsc);
          rect_dsc.bg_opa = LV_OPA_COVER;
          rect_dsc.radius = PAD_SMALL;
          lv_canvas_draw_rect(timerBg, TMR_BG_RX, TMR_BG_RY, TMR_LRG_W - TMR_BG_RX, TMR_LRG_H - TMR_BG_RY, &rect_dsc);
          rect_dsc.radius = LV_RADIUS_CIRCLE;
          lv_canvas_draw_rect(timerBg, 0, 0, TMR_LRG_H, TMR_LRG_H, &rect_dsc);
        }
      }
    }

#if defined(PCBPA01)
    constexpr LcdColorIndex iconColor = COLOR_THEME_PRIMARY1_INDEX;
#else
    constexpr LcdColorIndex iconColor = COLOR_THEME_SECONDARY1_INDEX;
#endif
    if (!timerIcon)
      timerIcon = new StaticIcon(this, PAD_SMALL, PAD_SMALL, ICON_TIMER, iconColor);

    if (!unit0) {
      // Timer value - on large widgets
      digits0 = createDigitsLabel();
      lv_obj_set_pos(digits0, D0_X, D_Y);
      digits1 = createDigitsLabel();
      lv_obj_set_pos(digits1, D1_X, D_Y);
      unit0 = createUnitLabel();
      lv_obj_set_pos(unit0, U0_X, U_Y);
      unit1 = createUnitLabel();
      lv_obj_set_pos(unit1, U1_X, U_Y);
      minusLabel = createDigitsLabel();
      lv_label_set_text(minusLabel, "-");
      lv_obj_align_to(minusLabel, digits0, LV_ALIGN_OUT_LEFT_TOP, 1, -1);
    }

    if (!timerArc) {
      timerArc = lv_arc_create(lvobj);
      lv_arc_set_rotation(timerArc, 270);
      lv_arc_set_bg_angles(timerArc, 0, 360);
      lv_arc_set_range(timerArc, 0, 360);
      lv_arc_set_angles(timerArc, 0, 360);
      lv_arc_set_start_angle(timerArc, 0);
      lv_obj_remove_style(timerArc, NULL, LV_PART_KNOB);
      lv_obj_clear_flag(timerArc, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_pos(timerArc, TMR_ARC_PAD, TMR_ARC_PAD);
      lv_obj_set_size(timerArc, TMR_ARC_SZ, TMR_ARC_SZ);
      lv_obj_set_style_arc_opa(timerArc, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_set_style_arc_width(timerArc, TMR_ARC_W, LV_PART_MAIN);
      lv_obj_set_style_arc_opa(timerArc, LV_OPA_COVER, LV_PART_INDICATOR);
      lv_obj_set_style_arc_width(timerArc, TMR_ARC_W, LV_PART_INDICATOR);
#if defined(PCBPA01)
      // Pink progress ring: the only strong accent on the main view.
      etx_arc_color(timerArc, COLOR_THEME_FOCUS_INDEX, LV_PART_INDICATOR);
#else
      etx_arc_color(timerArc, COLOR_THEME_SECONDARY1_INDEX, LV_PART_INDICATOR);
#endif
      lv_obj_add_flag(timerArc, LV_OBJ_FLAG_HIDDEN);
    }
  }

#if defined(PCBPA01)
  // PA01 card UI -----------------------------------------------------------
  // The legacy large UI splits the value into four labels positioned by fixed
  // offsets and hangs an arc off the left edge. On the new dark palette that
  // reads as loose fragments, so the card keeps a single value label and puts
  // the progress into a groove along the bottom of the plate.
  void createCardUI()
  {
    if (cardUI) return;
    cardUI = true;

    cardPlate = lv_obj_create(lvobj);
    lv_obj_remove_style_all(cardPlate);
    lv_obj_set_size(cardPlate, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_radius(cardPlate, 6, LV_PART_MAIN);
    // SECONDARY3 rather than PRIMARY2 or PRIMARY3: PRIMARY2 falls inside the
    // wallpaper's own range so the card vanished, and PRIMARY3 (28,40,51) reads
    // distinctly teal next to the pink glow. SECONDARY3 (28,33,43) is the one
    // neutral step that matches the design's #161C27.
    etx_solid_bg(cardPlate, COLOR_THEME_SECONDARY3_INDEX);
    lv_obj_set_style_bg_opa(cardPlate, LV_OPA_COVER, LV_PART_MAIN);
    etx_obj_add_style(cardPlate, styles->border_thin, LV_PART_MAIN);
    etx_obj_add_style(cardPlate, styles->border_color[COLOR_THEME_SECONDARY2_INDEX],
                      LV_PART_MAIN);
    lv_obj_set_style_border_opa(cardPlate, LV_OPA_30, LV_PART_MAIN);
    lv_obj_clear_flag(cardPlate, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_background(cardPlate);

    cardPill = lv_obj_create(lvobj);
    lv_obj_remove_style_all(cardPill);
    lv_obj_set_style_radius(cardPill, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    etx_solid_bg(cardPill, COLOR_THEME_FOCUS_INDEX);
    lv_obj_set_style_bg_opa(cardPill, LV_OPA_20, LV_PART_MAIN);
    lv_obj_clear_flag(cardPill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(cardPill, 24, 15);
    lv_obj_set_pos(cardPill, CARD_PAD, CARD_PAD);
    // The name label is created before the pill, so push the pill behind it.
    lv_obj_move_to_index(cardPill, 1);

    cardVal = etx_label_create(lvobj, FONT_XL_INDEX);
    etx_txt_color(cardVal, COLOR_THEME_PRIMARY1_INDEX);
    etx_txt_color(cardVal, COLOR_THEME_WARNING_INDEX,
                  LV_PART_MAIN | ETX_STATE_WARNING);
    etx_obj_add_style(cardVal, styles->text_align_center, LV_PART_MAIN);
    lv_label_set_text(cardVal, "00:00");
    // Full-width label with centred text: the value re-centres itself when the
    // string changes length, which a content-sized label does not do.
    lv_obj_set_width(cardVal, LV_PCT(100));
    lv_obj_align(cardVal, LV_ALIGN_CENTER, 0, PAD_TINY);

    cardGroove = lv_obj_create(lvobj);
    lv_obj_remove_style_all(cardGroove);
    lv_obj_set_style_radius(cardGroove, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    etx_solid_bg(cardGroove, COLOR_THEME_PRIMARY1_INDEX);
    lv_obj_set_style_bg_opa(cardGroove, LV_OPA_10, LV_PART_MAIN);
    lv_obj_clear_flag(cardGroove, LV_OBJ_FLAG_CLICKABLE);

    cardFill = lv_obj_create(lvobj);
    lv_obj_remove_style_all(cardFill);
    lv_obj_set_style_radius(cardFill, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    etx_solid_bg(cardFill, COLOR_THEME_FOCUS_INDEX);
    etx_solid_bg(cardFill, COLOR_THEME_WARNING_INDEX,
                 LV_PART_MAIN | ETX_STATE_WARNING);
    lv_obj_set_style_bg_opa(cardFill, LV_OPA_80, LV_PART_MAIN);
    lv_obj_clear_flag(cardFill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(cardFill, LV_OBJ_FLAG_HIDDEN);
  }

  void showCardUI()
  {
    hideSmallUI();
    createCardUI();

    // Name becomes the caption inside the pill.
    lv_obj_clear_state(nameLabel, EXT_NAME_ALIGN_RIGHT);
    lv_obj_clear_state(nameLabel, ETX_NAME_COLOR_WHITE);
    etx_txt_color(nameLabel, COLOR_THEME_FOCUS_INDEX);
    lv_obj_set_width(nameLabel, LV_SIZE_CONTENT);
    lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_CLIP);
    lv_obj_align(nameLabel, LV_ALIGN_TOP_LEFT, CARD_PAD + 6, CARD_PAD + 3);

    lv_obj_clear_flag(cardPlate, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cardVal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cardGroove, LV_OBJ_FLAG_HIDDEN);
    if (timerIcon) timerIcon->hide();
    if (timerArc) lv_obj_add_flag(timerArc, LV_OBJ_FLAG_HIDDEN);
    if (digits0) lv_obj_add_flag(digits0, LV_OBJ_FLAG_HIDDEN);
    if (digits1) lv_obj_add_flag(digits1, LV_OBJ_FLAG_HIDDEN);
    if (unit0) lv_obj_add_flag(unit0, LV_OBJ_FLAG_HIDDEN);
    if (unit1) lv_obj_add_flag(unit1, LV_OBJ_FLAG_HIDDEN);
    if (minusLabel) lv_obj_add_flag(minusLabel, LV_OBJ_FLAG_HIDDEN);

    layoutCard();
  }

  void hideCardUI()
  {
    if (cardPlate) lv_obj_add_flag(cardPlate, LV_OBJ_FLAG_HIDDEN);
    if (cardPill) lv_obj_add_flag(cardPill, LV_OBJ_FLAG_HIDDEN);
    if (cardVal) lv_obj_add_flag(cardVal, LV_OBJ_FLAG_HIDDEN);
    if (cardGroove) lv_obj_add_flag(cardGroove, LV_OBJ_FLAG_HIDDEN);
    if (cardFill) lv_obj_add_flag(cardFill, LV_OBJ_FLAG_HIDDEN);
  }

  void layoutCard()
  {
    if (!cardGroove) return;

    // Leave a hair of space under the plate so the timer card and the widget
    // stacked below it do not fuse into one tall panel.
    coord_t ch = height() - PAD_SMALL;
    if (cardPlate) lv_obj_set_size(cardPlate, width(), ch);

    coord_t gw = width() - 2 * CARD_PAD;
    if (gw < 8) gw = 8;

    lv_obj_set_size(cardGroove, gw, CARD_BAR_H);
    lv_obj_set_pos(cardGroove, CARD_PAD, ch - CARD_PAD - CARD_BAR_H);

    if (cardFill) {
      lv_obj_set_height(cardFill, CARD_BAR_H);
      lv_obj_set_pos(cardFill, CARD_PAD, ch - CARD_PAD - CARD_BAR_H);
    }
  }

  void updateCard(tmrval_t val, bool showElapsed)
  {
    if (!cardVal) return;

    char str[LEN_TIMER_STRING];
    TimerOptions timerOptions;
    timerOptions.options = (abs(val) >= 3600) ? SHOW_TIME : SHOW_TIMER;
    getTimerString(str, val, timerOptions);
    lv_label_set_text(cardVal, str);

    // Pill follows the label so "T1" and "TMR1" both look intentional.
    if (cardPill && nameLabel) {
      lv_obj_set_width(cardPill, lv_obj_get_self_width(nameLabel) + 12);
      lv_obj_set_height(cardPill, 15);
    }

    if (cardFill && cardGroove) {
      int pct = 0;
      if (lastStartValue > 0) {
        int done = showElapsed ? (int)lastValue
                               : (int)(lastStartValue - lastValue);
        if (done < 0) done = 0;
        if (done > (int)lastStartValue) done = (int)lastStartValue;
        pct = done * 100 / (int)lastStartValue;
      }

      coord_t gw = width() - 2 * CARD_PAD;
      if (gw < 8) gw = 8;
      if (pct <= 0) {
        lv_obj_add_flag(cardFill, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_clear_flag(cardFill, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(cardFill, gw * pct / 100);
      }
    }
  }
#endif

  void showSmallUI()
  {
    hideLargeUI();
    createSmallUI();

    lv_obj_clear_state(nameLabel, EXT_NAME_ALIGN_RIGHT);
    lv_obj_set_pos(nameLabel, PAD_TINY, 0);
    lv_obj_set_width(nameLabel, lv_pct(100));
    lv_obj_add_state(nameLabel, ETX_NAME_COLOR_WHITE);

    lv_obj_clear_flag(valLabel, LV_OBJ_FLAG_HIDDEN);
  }

  void hideSmallUI()
  {
    if (valLabel)
      lv_obj_add_flag(valLabel, LV_OBJ_FLAG_HIDDEN);
  }

  void createSmallUI()
  {
    if (!valLabel){
      // Timer value - on small size widgets
      valLabel = etx_label_create(lvobj);
      lv_label_set_text(valLabel, "");
      etx_txt_color(valLabel, COLOR_THEME_PRIMARY2_INDEX);
#if defined(PCBPA01)
      etx_txt_color(valLabel, COLOR_THEME_PRIMARY1_INDEX);
#endif
      etx_font(valLabel, FONT_XS_INDEX, LV_PART_MAIN | ETX_SMALL_FONT);
      lv_obj_set_pos(valLabel, PAD_THREE, VAL_LBL_Y);
    }
  }

  char sDigit0[3];
  char sDigit1[3];
  char sUnit0[2];
  char sUnit1[2];

  void tVal(char* d, char* u, int v, char unit)
  {
    d[0] = '0' + v / 10;
    d[1] = '0' + v % 10;
    d[2] = 0;
    u[0] = unit;
    u[1] = 0;
  }

  void splitTimer(int val)
  {
    div_t qr;

    // years
    qr = div(val, SECONDSPERYEAR);
    if (qr.quot != 0) {
      tVal(sDigit0, sUnit0, qr.quot, 'Y');
      tVal(sDigit1, sUnit1, qr.rem / SECONDSPERDAY, 'D');
      return;
    }
    // days
    qr = div(val, SECONDSPERDAY);
    if (qr.quot != 0) {
      tVal(sDigit0, sUnit0, qr.quot, 'D');
      tVal(sDigit1, sUnit1, qr.rem /SECONDSPERHOUR, 'H');
      return;
    }
    // hours
    qr = div(val, SECONDSPERHOUR);
    if (qr.quot != 0) {
      tVal(sDigit0, sUnit0, qr.quot, 'H');
      tVal(sDigit1, sUnit1, qr.rem / SECONDSPERMIN, 'M');
      return;
    }
    // minutes
    qr = div(val, SECONDSPERMIN);
    tVal(sDigit0, sUnit0, qr.quot, 'M');
    // seconds
    tVal(sDigit1, sUnit1, qr.rem, 'S');
  }
};

uint8_t TimerWidget::timerBgBuffer[];
bool TimerWidget::timerBgDrawn = false;

const WidgetOption TimerWidget::options[] = {
    {STR_TIMER_SOURCE, WidgetOption::Timer, 0},
    {nullptr, WidgetOption::Bool}};

BaseWidgetFactory<TimerWidget> timerWidget("Timer", TimerWidget::options,
                                           STR_WIDGET_TIMER);
