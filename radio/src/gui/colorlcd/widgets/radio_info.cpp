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

#include "edgetx.h"
#include "hal/usb_driver.h"
#include "layout.h"
#include "theme_manager.h"

#if defined(PCBPA01)
// PRIMARY2 is a *surface* colour on the PA01 palette, so icons drawn with it
// vanish into the status bar; PRIMARY3 is a dim surface too. Use the light text
// colour for foreground glyphs and a mid grey for the inactive parts.
  #define RI_FG     COLOR_THEME_PRIMARY1_INDEX
  #define RI_FG_DIM COLOR_THEME_SECONDARY2_INDEX
#else
  #define RI_FG     COLOR_THEME_PRIMARY2_INDEX
  #define RI_FG_DIM COLOR_THEME_PRIMARY3_INDEX
#endif

class RadioInfoWidget : public Widget
{
 public:
  RadioInfoWidget(const WidgetFactory* factory, Window* parent, const rect_t& rect,
                  int screenNum, int zoneNum) :
      Widget(factory, parent, rect, screenNum, zoneNum)
  {
#if !defined(PCBPA01)
    // Logs
    logsIcon = new StaticIcon(this, W_LOG_X, PAD_THREE, ICON_DOT,
                              RI_FG);
    logsIcon->hide();

    usbIcon =
        new StaticIcon(this, W_USB_X, W_USB_Y, ICON_TOPMENU_USB,
                       RI_FG);
    usbIcon->hide();

#if defined(AUDIO)
#if !defined(PCBPA01)
    // PA01: the scale is painted with the dim colour and ends up as an
    // unreadable ghost wedged against the white speaker glyph. The speaker on
    // its own reads fine at 320x240, so drop the backdrop.
    audioScale = new StaticIcon(this, W_AUDIO_SCALE_X, PAD_TINY,
                                ICON_TOPMENU_VOLUME_SCALE,
                                RI_FG_DIM);
#endif

    for (int i = 0; i < 5; i += 1) {
      audioVol[i] = new StaticIcon(
          this, W_AUDIO_X, PAD_TINY,
         (EdgeTxIcon)(ICON_TOPMENU_VOLUME_0 + i),
          RI_FG);
      audioVol[i]->hide();
    }
    audioVol[0]->show();
#endif

    batteryIcon = new StaticIcon(this, W_AUDIO_X, W_BATT_Y,
                                 ICON_TOPMENU_TXBATT,
                                 RI_FG);
#if defined(USB_CHARGER)
    batteryChargeIcon = new StaticIcon(
        this, W_BATT_CHG_X, W_BATT_CHG_Y,
        ICON_TOPMENU_TXBATT_CHARGE, RI_FG);
    batteryChargeIcon->hide();
#endif

#if defined(EXTERNAL_ANTENNA)
    extAntenna = new StaticIcon(this, W_RSSI_X - PAD_SMALL, 1,
                                ICON_TOPMENU_ANTENNA,
                                RI_FG);
    extAntenna->hide();
#endif

    batteryFill = lv_obj_create(lvobj);
    lv_obj_set_pos(batteryFill, W_AUDIO_X + 1, W_BATT_Y + 1);
    lv_obj_set_size(batteryFill, 0, W_BATT_FILL_H);
    lv_obj_set_style_bg_opa(batteryFill, LV_OPA_COVER, LV_PART_MAIN);
    update();

    // RSSI bars
    LAYOUT_VAL_SCALED(RSSI_BH, 31)
    const uint8_t rssiBarsHeight[] = {
      (RSSI_BH * 5 + 15) / 31,
      (RSSI_BH * 10 + 15) / 31,
      (RSSI_BH * 15 + 15) / 31,
      (RSSI_BH * 21 + 15) / 31,
      RSSI_BH};
    for (unsigned int i = 0; i < DIM(rssiBarsHeight); i++) {
      uint8_t height = rssiBarsHeight[i];
      rssiBars[i] = lv_obj_create(lvobj);
      lv_obj_set_pos(rssiBars[i], W_RSSI_X + i * W_RSSI_BAR_SZ,
                     W_RSSI_BAR_H - height);
      lv_obj_set_size(rssiBars[i], W_RSSI_BAR_W, height);
      etx_solid_bg(rssiBars[i], RI_FG_DIM);
      etx_bg_color(rssiBars[i], RI_FG, LV_STATE_USER_1);
    }
#else
    // PA01 status cluster, laid out to match the approved design: four rounded
    // signal bars, then an outlined battery with a percentage. The volume
    // glyph is dropped - at 320x240 it collided with the scale backdrop and
    // carried less information than the percentage does.
    // Four rounded bars. Heights stop at PA01_BATT_H (9), so the tallest bar and
    // the battery shell share a top edge; the old 4/7/10/13 run put the bars 4px
    // above the battery and the whole right end read as ragged.
    static const coord_t barH[PA01_BARS] = {3, 5, 7, 9};
    // Shared foot line for the whole cluster. The date caption occupies y=6..16
    // in the 36px header, so putting the cluster's feet on 16 lands it on the
    // caption's baseline: the battery top ends up at 7, one pixel off the
    // caption top. That is the "moved to the top" look the user picked - the
    // cluster used to sit on 24 (level with the clock) and read as too low.
    const coord_t clusterBottom = 14;
    const coord_t rssiW =
        PA01_BARS * PA01_BAR_W + (PA01_BARS - 1) * PA01_BAR_GAP;
    const coord_t totalW = rssiW + PA01_GAP + PA01_BATT_W + 2 + PA01_PCT_W;
    // Reserve the clock block and the 1px rule (plus clearance) at the far
    // right. Both this cluster and the clock anchored to the widget's right
    // edge, so the percentage was printed on top of the date.
    const coord_t reserved = HeaderDateTime::HDR_DATE_WIDTH_PA01 + 6;
    const coord_t x0 =
        width() > totalW + reserved ? width() - totalW - reserved : 0;

    for (int i = 0; i < PA01_BARS; i += 1) {
      rssiBars[i] = lv_obj_create(lvobj);
      lv_obj_remove_style_all(rssiBars[i]);
      lv_obj_set_style_radius(rssiBars[i], 1, LV_PART_MAIN);
      lv_obj_set_pos(rssiBars[i], x0 + i * (PA01_BAR_W + PA01_BAR_GAP),
                     clusterBottom - barH[i]);
      lv_obj_set_size(rssiBars[i], PA01_BAR_W, barH[i]);
      etx_solid_bg(rssiBars[i], COLOR_THEME_SECONDARY2_INDEX);
      etx_bg_color(rssiBars[i], COLOR_THEME_FOCUS_INDEX, LV_STATE_USER_1);
    }

    const coord_t bx = x0 + rssiW + PA01_GAP;
    const coord_t by = clusterBottom - PA01_BATT_H;

    battShell = lv_obj_create(lvobj);
    lv_obj_remove_style_all(battShell);
    lv_obj_set_style_radius(battShell, 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(battShell, 1, LV_PART_MAIN);
    etx_border_color(battShell, COLOR_THEME_SECONDARY2_INDEX);
    lv_obj_set_pos(battShell, bx, by);
    lv_obj_set_size(battShell, PA01_BATT_W, PA01_BATT_H);

    battCap = lv_obj_create(lvobj);
    lv_obj_remove_style_all(battCap);
    lv_obj_set_style_radius(battCap, 1, LV_PART_MAIN);
    etx_solid_bg(battCap, COLOR_THEME_SECONDARY2_INDEX);
    // Tucked into the shell's right edge so it reads as a nub. At +1 it floated
    // 1px clear of the outline and looked like a stray block.
    lv_obj_set_pos(battCap, bx + PA01_BATT_W - 1, by + 3);
    lv_obj_set_size(battCap, 2, PA01_BATT_H - 6);

    batteryFill = lv_obj_create(lvobj);
    lv_obj_remove_style_all(batteryFill);
    lv_obj_set_style_radius(batteryFill, 1, LV_PART_MAIN);
    lv_obj_set_pos(batteryFill, bx + 1, by + 1);
    lv_obj_set_size(batteryFill, 0, PA01_BATT_H - 2);
    etx_solid_bg(batteryFill, COLOR_THEME_FOCUS_INDEX);
    etx_bg_color(batteryFill, COLOR_THEME_WARNING_INDEX, LV_STATE_USER_2);

    // 10px step, like the date: at FONT_XS the percentage stood taller than the
    // battery it describes and pulled the eye away from the clock.
    battPct = etx_label_create(lvobj, FONT_XXS_INDEX);
    // Two clear pixels after the cap. With the cap at +1 and the label at +3
    // they touched, and the nub merged into the leading "3".
    lv_obj_set_pos(battPct, bx + PA01_BATT_W + 2, by - 1);
    lv_obj_set_size(battPct, PA01_PCT_W, PA01_BATT_H + 1);
    etx_txt_color(battPct, COLOR_THEME_PRIMARY1_INDEX);
    // The palette has no mid grey between the dim border colour and near-white;
    // 70% on the text colour lands on the design's #B6C2D4.
    lv_obj_set_style_text_opa(battPct, LV_OPA_70, LV_PART_MAIN);

    update();
#endif

    foreground();
  }

  void update() override
  {
    if (deleted()) return;

#if defined(PCBPA01)
    // The PA01 cluster keeps one accent colour and only flips to WARNING when
    // the pack is nearly flat: three user colours on an 18px pill turned into
    // mush, and the design calls for a single pink fill.
    uint8_t bars = GET_TXBATT_BARS(PA01_BATT_W - 2);
    if (bars != lastBatt) {
      lastBatt = bars;
      lv_obj_set_size(batteryFill, bars, PA01_BATT_H - 2);
    }

    uint8_t pct = GET_TXBATT_BARS(100);
    if (pct != lastPct) {
      lastPct = pct;
      char buf[8];
      sprintf(buf, "%u%%", pct);
      lv_label_set_text(battPct, buf);
    }

    // Follow the actual low-battery alarm instead of a fixed percentage, so the
    // red fill and the warning sound cannot disagree.
    bool low = IS_TXBATT_WARNING();
    if (low != lastLow) {
      lastLow = low;
      if (low)
        lv_obj_add_state(batteryFill, LV_STATE_USER_2);
      else
        lv_obj_clear_state(batteryFill, LV_STATE_USER_2);
    }
    return;
#endif

    auto widgetData = getPersistentData();

    // get colors from options
    etx_bg_color_from_flags(batteryFill, widgetData->options[2].value.unsignedValue, LV_PART_MAIN);
    etx_bg_color_from_flags(batteryFill, widgetData->options[1].value.unsignedValue, LV_STATE_USER_1);
    etx_bg_color_from_flags(batteryFill, widgetData->options[0].value.unsignedValue, LV_STATE_USER_2);
  }

  void foreground() override
  {
    if (deleted()) return;

#if !defined(PCBPA01)
    usbIcon->show(usbPlugged());
    if (getSelectedUsbMode() == USB_UNSELECTED_MODE)
      usbIcon->setColor(RI_FG_DIM);
    else
      usbIcon->setColor(RI_FG);

    logsIcon->show(!usbPlugged() && isFunctionActive(FUNCTION_LOGS) &&
                   BLINK_ON_PHASE);

#if defined(AUDIO)
    /* Audio volume */
    uint8_t vol = 4;
    if (requiredSpeakerVolume == 0 || g_eeGeneral.beepMode == e_mode_quiet)
      vol = 0;
    else if (requiredSpeakerVolume < 7)
      vol = 1;
    else if (requiredSpeakerVolume < 13)
      vol = 2;
    else if (requiredSpeakerVolume < 19)
      vol = 3;
    if (vol != lastVol) {
      audioVol[vol]->show();
      audioVol[lastVol]->hide();
      lastVol = vol;
    }
#endif

#if defined(USB_CHARGER)
    batteryChargeIcon->show(usbChargerLed());
#endif

#if defined(EXTERNAL_ANTENNA)
    extAntenna->show(isExternalAntennaEnabled());
#endif

    // Battery level
    uint8_t bars = GET_TXBATT_BARS(W_BATT_FILL_W);
    if (bars != lastBatt) {
      lastBatt = bars;
      lv_obj_set_size(batteryFill, bars, W_BATT_FILL_H);
      if (bars >= W_BATT_FILL_GRN) {
        lv_obj_clear_state(batteryFill, LV_STATE_USER_1 | LV_STATE_USER_2);
      } else if (bars >= W_BATT_FILL_ORA) {
        lv_obj_add_state(batteryFill, LV_STATE_USER_1);
        lv_obj_clear_state(batteryFill, LV_STATE_USER_2);
      } else {
        lv_obj_clear_state(batteryFill, LV_STATE_USER_1);
        lv_obj_add_state(batteryFill, LV_STATE_USER_2);
      }
    }
#else
    // Battery percentage and fill live in update(); call it here so the pill
    // tracks the pack without waiting for an option change.
    update();
#endif

    // RSSI
#if defined(PCBPA01)
    const uint8_t rssiBarsValue[] = {30, 40, 50, 60};
#else
    const uint8_t rssiBarsValue[] = {30, 40, 50, 60, 80};
#endif
    uint8_t rssi = TELEMETRY_RSSI();
    if (rssi != lastRSSI) {
      lastRSSI = rssi;
      for (unsigned int i = 0; i < DIM(rssiBarsValue); i++) {
        if (rssi >= rssiBarsValue[i])
          lv_obj_add_state(rssiBars[i], LV_STATE_USER_1);
        else
          lv_obj_clear_state(rssiBars[i], LV_STATE_USER_1);
      }
    }
  }

  static const WidgetOption options[];

  static constexpr coord_t W_AUDIO_X = 0;
  static LAYOUT_VAL_SCALED(W_AUDIO_SCALE_X, 15)
  static LAYOUT_VAL_SCALED(W_USB_X, 32)
  static LAYOUT_VAL_SCALED(W_USB_Y, 5)
  static constexpr coord_t W_LOG_X = W_USB_X;
  static LAYOUT_VAL_SCALED(W_RSSI_X, 40)
  static LAYOUT_VAL_SCALED(W_RSSI_BAR_W, 4)
  static LAYOUT_VAL_SCALED(W_RSSI_BAR_H, 35)
  static LAYOUT_VAL_SCALED(W_RSSI_BAR_SZ, 6)
  static LAYOUT_VAL_SCALED(W_BATT_Y, 25)
  static LAYOUT_VAL_SCALED(W_BATT_FILL_W, 20)
  static LAYOUT_VAL_SCALED(W_BATT_FILL_H, 10)
  static LAYOUT_VAL_SCALED(W_BATT_FILL_GRN, 12)
  static LAYOUT_VAL_SCALED(W_BATT_FILL_ORA, 5)
  static LAYOUT_VAL_SCALED(W_BATT_CHG_X, 25)
  static LAYOUT_VAL_SCALED(W_BATT_CHG_Y, 23)

#if defined(PCBPA01)
  // Status cluster geometry, in LCD pixels (320x240). Kept as plain constants:
  // LAYOUT_VAL_SCALED follows the 480-wide reference and would overshoot here.
  static constexpr coord_t PA01_BARS = 4;
  // 3px, not 2px: at 2px the corner radius is capped at 1 and reads as a bevel
  // rather than a rounded end.
  static constexpr coord_t PA01_BAR_W = 3;
  static constexpr coord_t PA01_BAR_GAP = 2;
  static constexpr coord_t PA01_GAP = 6;
  static constexpr coord_t PA01_BATT_W = 16;
  static constexpr coord_t PA01_BATT_H = 9;
  static constexpr coord_t PA01_PCT_W = 21;
#endif

 protected:
  uint8_t lastVol = 0;
  uint8_t lastBatt = 0;
  uint8_t lastRSSI = 0;
#if defined(PCBPA01)
  lv_obj_t* battShell = nullptr;
  lv_obj_t* battCap = nullptr;
  lv_obj_t* battPct = nullptr;
  uint8_t lastPct = 255;
  bool lastLow = false;
#else
  StaticIcon* logsIcon;
  StaticIcon* usbIcon;
#endif
#if defined(AUDIO)
  StaticIcon* audioScale = nullptr;
  StaticIcon* audioVol[5];
#endif
  StaticIcon* batteryIcon;
  lv_obj_t* batteryFill = nullptr;
  lv_obj_t* rssiBars[5] = {nullptr};
#if defined(USB_CHARGER)
  StaticIcon* batteryChargeIcon;
#endif
#if defined(EXTERNAL_ANTENNA)
  StaticIcon* extAntenna;
#endif
};

const WidgetOption RadioInfoWidget::options[] = {
    {STR_LOW_BATT_COLOR, WidgetOption::Color, RGB2FLAGS(0xF4, 0x43, 0x36)},
    {STR_MID_BATT_COLOR, WidgetOption::Color, RGB2FLAGS(0xFF, 0xC1, 0x07)},
    {STR_HIGH_BATT_COLOR, WidgetOption::Color, RGB2FLAGS(0x4C, 0xAF, 0x50)},
    {nullptr, WidgetOption::Bool}};

BaseWidgetFactory<RadioInfoWidget> RadioInfoWidget("Radio Info", RadioInfoWidget::options,
                                                   STR_RADIO_INFO_WIDGET);

class DateTimeWidget : public Widget
{
 public:
  DateTimeWidget(const WidgetFactory* factory, Window* parent, const rect_t& rect,
                 int screenNum, int zoneNum) :
      Widget(factory, parent, rect, screenNum, zoneNum)
  {
#if defined(PCBPA01)
    coord_t x = rect.w - HeaderDateTime::HDR_DATE_WIDTH_PA01;
    // The block starts level with the status cluster, not with the header's
    // bottom. The cluster's top edge sits on screen y=7 and the 10px date draws
    // its first lit row 5px into its own box, so an origin of 2 puts the date's
    // cap line on 7 as well - same top gap as the bars and the battery. The
    // clock then hangs below it at full size.
    dateTime = new HeaderDateTime(this, x, 2);
#else
    coord_t x = rect.w - HeaderDateTime::HDR_DATE_WIDTH - DT_XO;
    dateTime = new HeaderDateTime(this, x, PAD_THREE);
#endif
#if defined(PCBPA01)
    // Thin rule separating the status cluster from the clock, as in the design.
    auto rule = lv_obj_create(lvobj);
    lv_obj_remove_style_all(rule);
    etx_solid_bg(rule, COLOR_THEME_SECONDARY2_INDEX);
    lv_obj_set_style_bg_opa(rule, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_pos(rule, x - 2, 6);
    lv_obj_set_size(rule, 1, 24);
    lv_obj_clear_flag(rule, LV_OBJ_FLAG_CLICKABLE);
#endif
    update();
  }

  void foreground() override
  {
    if (deleted()) return;

    Widget::checkEvents();
  }

  void update() override
  {
    if (deleted()) return;

    auto widgetData = getPersistentData();

    // get color from options
    uint32_t color;
    memcpy(&color, &widgetData->options[0].value.unsignedValue, sizeof(color));
#if defined(PCBPA01)
    // Models store COLIDX1, which maps to PRIMARY2 - a surface colour on the
    // PA01 palette, i.e. black text on a black bar.
    color = pa01ReadableColor(color, COLOR_THEME_PRIMARY1_INDEX);
#endif
    dateTime->setColor(color);
#if defined(PCBPA01)
    // Position stays as set in the constructor. Re-posing it here used the
    // shared 45px width and DT_XO, which both resets the PA01 block to the top
    // and shifts it sideways - that is why the y passed to the constructor had
    // no visible effect.
#else
    coord_t x = width() - HeaderDateTime::HDR_DATE_WIDTH - DT_XO;
    dateTime->setPos(x, PAD_THREE);
#endif
  }

  HeaderDateTime* dateTime = nullptr;
  int8_t lastMinute = -1;

  static const WidgetOption options[];

  // Adjustment to make main view date/time align with model/radio settings views
  static LAYOUT_VAL_SCALED(DT_XO, 1)
};

const WidgetOption DateTimeWidget::options[] = {
    {STR_COLOR, WidgetOption::Color, COLOR2FLAGS(RI_FG)},
    {nullptr, WidgetOption::Bool}};

BaseWidgetFactory<DateTimeWidget> DateTimeWidget("Date Time",
                                                 DateTimeWidget::options,
                                                 STR_DATE_TIME_WIDGET);

#if defined(INTERNAL_GPS)

class InternalGPSWidget : public Widget
{
 public:
  InternalGPSWidget(const WidgetFactory* factory, Window* parent, const rect_t& rect,
                    int screenNum, int zoneNum) :
      Widget(factory, parent, rect, screenNum, zoneNum)
  {
    icon =
        new StaticIcon(this, width() / 2 - PAD_LARGE - PAD_TINY, ICON_H,
                       ICON_TOPMENU_GPS, RI_FG_DIM);

    numSats = new DynamicNumber<uint16_t>(
        this, {0, 1, width(), SATS_H}, [=] { return gpsData.numSat; },
        RI_FG, CENTERED | FONT(XS));
  }

  void foreground() override
  {
    if (deleted()) return;

    bool hasGPS = serialGetModePort(UART_MODE_GPS) >= 0;

    numSats->show(hasGPS && (gpsData.numSat > 0));
    icon->show(hasGPS);

    if (gpsData.fix)
      icon->setColor(RI_FG);
    else
      icon->setColor(RI_FG_DIM);
  }

 protected:
  StaticIcon* icon;
  DynamicNumber<uint16_t>* numSats;

  static LAYOUT_VAL_SCALED(ICON_H, 19)
  static LAYOUT_VAL_SCALED(SATS_H, 12)
};

BaseWidgetFactory<InternalGPSWidget> InternalGPSWidget("Internal GPS", nullptr,
                                                       STR_INT_GPS_LABEL);

#endif
