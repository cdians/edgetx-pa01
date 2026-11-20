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
#include "messaging.h"

constexpr int16_t OUTPUT_INVALID_VALUE = INT16_MIN;

#define ETX_STATE_BG_FILL LV_STATE_USER_1

#if defined(PCBPA01)
// Widget colours are stored in the model file. Models authored against the
// stock light-blue background store near-black text and a dark bar colour,
// which disappear on the PA01 dark plate. Resolve the stored flag to RGB565
// and fall back to theme colours when the contrast would be too low.
static uint16_t pa01Rgb565(LcdFlags f)
{
  if (f & RGB_FLAG) return (uint16_t)COLOR_VAL(f);
  unsigned idx = COLOR_VAL(f) & 0xFF;
  if (idx >= TOTAL_COLOR_COUNT) return 0xFFFF;
  return lcdColorTable[idx];
}

static int pa01Luma(LcdFlags f)
{
  uint16_t c = pa01Rgb565(f);
  return 2 * ((c >> 11) & 0x1F) + 4 * ((c >> 5) & 0x3F) + (c & 0x1F);
}

#define PA01_MIN_LUMA 110
#endif


class ChannelValue : public Window
{
 public:
  ChannelValue(Widget* parent, uint8_t col, uint8_t row, coord_t colWidth,
               uint8_t channel, LcdFlags txtColor, LcdFlags barColor) :
      Window(parent,
             {col * colWidth, row * ROW_HEIGHT, colWidth - 1 + (colWidth & 1), (ROW_HEIGHT + 1)}),
      channel(channel), txtColor(txtColor), barColor(barColor)
  {
    setWindowFlag(NO_FOCUS | NO_CLICK);

    refreshMsg.subscribe(Messaging::REFRESH_OUTPUTS_WIDGET, [=](uint32_t param) { refresh(); });
  
    delayLoad();
  }

  void delayedInit() override
  {
#if defined(PCBPA01)
    // Keep the model colours when they are readable on the dark plate,
    // otherwise fall back to the theme text / accent colours.
    // NOTE: these must be *index* flags (COLOR2FLAGS). COLOR_THEME_xxx is a
    // packed RGB565 value without RGB_FLAG, and the *_from_flags helpers would
    // read it as an index and walk past the end of styles->txt_color[].
    if (pa01Luma(txtColor) < PA01_MIN_LUMA)
      txtColor = COLOR2FLAGS(COLOR_THEME_PRIMARY1_INDEX);
    if (pa01Luma(barColor) < PA01_MIN_LUMA)
      barColor = COLOR2FLAGS(COLOR_THEME_FOCUS_INDEX);

    // PA01: the stock 1px black frame around every cell made this panel read
    // like a spreadsheet pasted over the model image. Rows are separated by a
    // single hairline instead, and the cells themselves stay transparent so the
    // widget plate is the only surface the user sees.
    etx_obj_add_style(lvobj, styles->border_thin, LV_PART_MAIN);
    etx_obj_add_style(lvobj, styles->border_color[COLOR_THEME_SECONDARY2_INDEX], LV_PART_MAIN);
    lv_obj_set_style_border_side(lvobj, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_opa(lvobj, LV_OPA_20, LV_PART_MAIN);
#else
    etx_obj_add_style(lvobj, styles->border_thin, LV_PART_MAIN);
    etx_obj_add_style(lvobj, styles->border_color[COLOR_BLACK_INDEX], LV_PART_MAIN);
#endif

    padAll(PAD_ZERO);

    bar = lv_obj_create(lvobj);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
#if defined(PCBPA01)
    // A 3px pill along the bottom edge: the value reads as a gauge without
    // flooding the row when a channel sits at full deflection.
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_size(bar, 0, BAR_H);
#else
    lv_obj_set_size(bar, 0, ROW_HEIGHT - 1);
#endif
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    etx_bg_color_from_flags(bar, barColor);

    valueLabel = etx_label_create(lvobj, FONT_XS_INDEX);
    etx_obj_add_style(valueLabel, styles->text_align_right, LV_PART_MAIN);
    etx_txt_color_from_flags(valueLabel, txtColor);
    lv_obj_set_width(valueLabel, LV_PCT(100));
    lv_label_set_text(valueLabel, "");

    chanLabel = etx_label_create(lvobj, FONT_XS_INDEX);
    etx_obj_add_style(chanLabel, styles->text_align_left, LV_PART_MAIN);
    etx_txt_color_from_flags(chanLabel, txtColor);
    lv_label_set_text(chanLabel, "");

#if defined(PCBPA01)
    // Values snap to the right edge of the cell so the column scans as one
    // block of numbers; labels take the same inset on the left.
    lv_obj_set_style_pad_right(valueLabel, PAD_SMALL, LV_PART_MAIN);
    lv_obj_set_pos(valueLabel, 0, PAD_TINY);
    lv_obj_set_pos(chanLabel, PAD_SMALL, PAD_TINY);
#endif

    chanHasName = g_model.limitData[channel].name[0] != 0;
    setChannel();

#if !defined(PCBPA01)
    divPoints[0] = {(lv_coord_t)(width() / 2 - 1), 0};
    divPoints[1] = {(lv_coord_t)(width() / 2 - 1), ROW_HEIGHT - 1};

    auto divLine = lv_line_create(lvobj);
    lv_line_set_points(divLine, divPoints, 2);
    etx_obj_add_style(divLine, styles->div_line, LV_PART_MAIN);
#endif

    refresh();
  }

  void setChannel()
  {
    char s[16];
    if (chanHasName) {
      formatNumberAsString(s, 16, channel + 1, LEADING0, 2, "", " ");
      strAppend(s + 3, g_model.limitData[channel].name, LEN_CHANNEL_NAME);
    } else {
      getSourceString(s, MIXSRC_FIRST_CH + channel);
    }
    lv_label_set_text(chanLabel, s);
  }

  void refresh()
  {
    if (!loaded || deleted()) return;

    int16_t value = channelOutputs[channel];

    if (value != lastValue || lastExtendedLimits != g_model.extendedLimits) {
      lastValue = value;
      lastExtendedLimits = g_model.extendedLimits;

      std::string s;
      if (g_eeGeneral.ppmunit == PPM_US)
        s = formatNumberAsString(PPM_CH_CENTER(channel) + value / 2, 0, 0, "", STR_US);
      else if (g_eeGeneral.ppmunit == PPM_PERCENT_PREC1)
        s = formatNumberAsString(calcRESXto1000(value), PREC1, 0, "", "%");
      else
        s = formatNumberAsString(calcRESXto100(value), 0, 0, "", "%");

      if (s != lastText) {
        lastText = s;
        lv_label_set_text(valueLabel, s.c_str());
      }

      const int lim = (g_model.extendedLimits ? (1024 * LIMIT_EXT_PERCENT / 100) : 1024);
      uint16_t w = width() - PAD_TINY;
      int16_t scaledValue = divRoundClosest(w * limit<int16_t>(-lim, value, lim), lim * 2);

      if (scaledValue != lastScaledValue) {
        lastScaledValue = scaledValue;

        uint16_t fillW = abs(scaledValue);
        uint16_t x = value > 0 ? w / 2 : w / 2 - fillW + 1;

#if defined(PCBPA01)
        lv_obj_set_pos(bar, x, ROW_HEIGHT - BAR_H);
        lv_obj_set_size(bar, fillW, BAR_H);
#else
        lv_obj_set_pos(bar, x, 0);
        lv_obj_set_size(bar, fillW, ROW_HEIGHT - 1);
#endif
      }
    }

    bool hasName = g_model.limitData[channel].name[0] != 0;
    if (hasName != chanHasName) {
      chanHasName = hasName;
      setChannel();
    }
  }

  static LAYOUT_VAL_SCALED(ROW_HEIGHT, 16)
#if defined(PCBPA01)
  static constexpr coord_t BAR_H = 3;
#endif

 protected:
  uint8_t channel;
  LcdFlags txtColor;
  LcdFlags barColor;
  int16_t lastValue = OUTPUT_INVALID_VALUE;
  int16_t lastScaledValue = OUTPUT_INVALID_VALUE;
  std::string lastText;
  bool lastExtendedLimits = false;
  bool chanHasName = false;
  lv_obj_t* valueLabel = nullptr;
  lv_obj_t* chanLabel = nullptr;
  lv_point_t divPoints[2];
  lv_obj_t* bar = nullptr;
  Messaging refreshMsg;
};

class OutputsWidget : public Widget
{
 public:
  OutputsWidget(const WidgetFactory* factory, Window* parent, const rect_t& rect,
                int screenNum, int zoneNum) :
      Widget(factory, parent, rect, screenNum, zoneNum)
  {
    padAll(PAD_ZERO);

    etx_obj_add_style(lvobj, styles->bg_opacity_transparent, LV_PART_MAIN);
    etx_obj_add_style(lvobj, styles->bg_opacity_cover,
                      LV_PART_MAIN | ETX_STATE_BG_FILL);

#if defined(PCBPA01)
    // Rounded, translucent plate: the rows float above the background image
    // instead of sitting directly on it. Still honours the "fill background"
    // widget option, only the strength changes.
    lv_obj_set_style_radius(lvobj, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lvobj, LV_OPA_COVER, LV_PART_MAIN | ETX_STATE_BG_FILL);
    lv_obj_set_style_clip_corner(lvobj, true, LV_PART_MAIN);
    etx_obj_add_style(lvobj, styles->border_thin, LV_PART_MAIN);
    etx_obj_add_style(lvobj, styles->border_color[COLOR_THEME_SECONDARY2_INDEX],
                      LV_PART_MAIN);
    lv_obj_set_style_border_opa(lvobj, LV_OPA_30,
                                LV_PART_MAIN | ETX_STATE_BG_FILL);
    lv_obj_set_style_border_opa(lvobj, LV_OPA_TRANSP, LV_PART_MAIN);
#endif

    update();
  }

  void update() override
  {
    auto widgetData = getPersistentData();

    // get background color from options[3]
    etx_bg_color_from_flags(lvobj, widgetData->options[3].value.unsignedValue);
#if defined(PCBPA01)
    // Ignore the colour stored in the model: on the dark PA01 palette the plate
    // has to match the timer card, otherwise an old model file leaves the rows
    // floating on the wallpaper again. SECONDARY3 is the neutral step that
    // survives the new wallpaper - see the note in timer.cpp.
    etx_solid_bg(lvobj, COLOR_THEME_SECONDARY3_INDEX);
#endif

    // Set background opacity from options[2]
    if (widgetData->options[2].value.boolValue)
      lv_obj_add_state(lvobj, ETX_STATE_BG_FILL);
    else
      lv_obj_clear_state(lvobj, ETX_STATE_BG_FILL);

    if (height() <= SHOW_MIN_H || width() <= SHOW_MIN_W)
      return;

    bool changed = false;

    // Colors
    LcdFlags f = widgetData->options[4].value.unsignedValue;
    if (f != txtColor) { txtColor = f; changed = true; }
    f = widgetData->options[5].value.unsignedValue;
    if (f != barColor) { barColor = f; changed = true; }

    // Setup channels
    uint8_t chan = widgetData->options[0].value.unsignedValue;
    if (chan != firstChan) { firstChan = chan; changed = true; }
    chan = widgetData->options[1].value.unsignedValue;
    if (chan != lastChan) { lastChan = chan; changed = true; }

    // Get size
    if (width() != lastWidth) { lastWidth = width(); changed = true; }
    if (height() != lastHeight) { lastHeight = height(); changed = true; }
    uint8_t n = lastHeight / ChannelValue::ROW_HEIGHT;
    if (n != rows) { rows = n; changed = true; }
    n = (lastWidth > COLS_MIN_W) ? 2 : 1;
    if (n != cols) { cols = n; changed = true; }

    if (changed) {
      clear();
      coord_t colWidth = lastWidth / cols;
      uint8_t chan = firstChan;
      for (uint8_t c = 0; c < cols && chan <= lastChan; c += 1) {
        for (uint8_t r = 0; r < rows && chan <= lastChan;
             r += 1, chan += 1) {
          new ChannelValue(this, c, r, colWidth, chan - 1, txtColor, barColor);
        }
      }
    }
  }

  void foreground() override
  {
    Messaging::send(Messaging::REFRESH_OUTPUTS_WIDGET);
  }

  static const WidgetOption options[];

 protected:
  coord_t lastWidth = -1;
  coord_t lastHeight = -1;
  uint8_t firstChan = 255;
  uint8_t lastChan = 255;
  uint8_t cols = 0;
  uint8_t rows = 0;
  LcdFlags txtColor = 0;
  LcdFlags barColor = 0;

  static LAYOUT_VAL_SCALED(SHOW_MIN_W, 100)
  static LAYOUT_VAL_SCALED(SHOW_MIN_H, 20)
  static LAYOUT_VAL_SCALED(COLS_MIN_W, 300)
};

const WidgetOption OutputsWidget::options[] = {
    {STR_FIRST_CHANNEL, WidgetOption::Integer, {1}, {1}, {MAX_OUTPUT_CHANNELS}},
    {STR_LAST_CHANNEL, WidgetOption::Integer, {MAX_OUTPUT_CHANNELS}, {1}, {MAX_OUTPUT_CHANNELS}},
#if defined(PCBPA01)
    // On the dark PA01 theme the table needs the plate to stay readable over
    // the background image, so new widgets start with it enabled.
    {STR_FILL_BACKGROUND, WidgetOption::Bool, true},
#else
    {STR_FILL_BACKGROUND, WidgetOption::Bool, false},
#endif
    {STR_BG_COLOR, WidgetOption::Color, COLOR2FLAGS(COLOR_THEME_SECONDARY3_INDEX)},
    {STR_TEXT_COLOR, WidgetOption::Color, COLOR2FLAGS(COLOR_THEME_PRIMARY1_INDEX)},
    {STR_COLOR, WidgetOption::Color, COLOR2FLAGS(COLOR_THEME_SECONDARY1_INDEX)},
    {nullptr, WidgetOption::Bool}};

// Note: Must be a template class otherwise the linker will discard the 'outputsWidget' object
template <class T>
class OutputsWidgetFactory : public WidgetFactory
{
 public:
  OutputsWidgetFactory(const char* name, const WidgetOption* options,
                    const char* displayName = nullptr) :
      WidgetFactory(name, options, displayName)
  {
  }

  Widget* createNew(Window* parent, const rect_t& rect,
                 int screenNum, int zoneNum) const override
  {
    return new T(this, parent, rect, screenNum, zoneNum);
  }

  // Fix the options loaded from the model file to account for
  // addition of the 'last channel' option
  const void checkOptions(int screenNum, int zoneNum) const override
  {
    auto widgetData = g_model.getWidgetData(screenNum, zoneNum);
    if (widgetData && widgetData->options.size() >= 2 &&
        widgetData->options.size() < 6 &&
        widgetData->options[1].type == WOV_Bool) {
      WidgetOptionValueTyped v;
      v.type = WOV_Signed;
      v.value.signedValue = MAX_OUTPUT_CHANNELS;
      widgetData->options.insert(widgetData->options.begin() + 1, v);
      storageDirty(EE_MODEL);
    }
  }
};

OutputsWidgetFactory<OutputsWidget> outputsWidget("Outputs",
                                               OutputsWidget::options,
                                               STR_WIDGET_OUTPUTS);
