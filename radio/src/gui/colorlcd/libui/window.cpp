/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   libopenui - https://github.com/opentx/libopenui
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

#include "window.h"

#include "button.h"
#include "debug.h"
#include "etx_lv_theme.h"
#include "form.h"
#include "keyboard_base.h"
#include "keys.h"
#include "pagegroup.h"
#include "static.h"

//-----------------------------------------------------------------------------

class Layer
{
  static std::list<Layer> stack;

  Window*     window;
  lv_group_t* group;
  lv_group_t* prevGroup;

 public:
  explicit Layer(Window* w, lv_group_t* g, lv_group_t* pg) :
      window(w), group(g), prevGroup(pg)
  {
  }

  ~Layer() { lv_group_del(group); }

  static void push(Window* window);
  static void pop(Window* window);

  static Window* back();
  static Window* walk(std::function<bool(Window* w)> check);
};

std::list<Layer> Layer::stack;

void Layer::push(Window* w)
{
  // save prev group
  auto pg = lv_group_get_default();

  // create a new group
  auto g = lv_group_create();
  w->assignLvGroup(g, true);

  // and store
  stack.emplace_back(w, g, pg);
}

void Layer::pop(Window* w)
{
  if (stack.empty()) return;

  if (back() == w) {
    lv_group_t* prevGroup = stack.back().prevGroup;
    stack.pop_back();
    w = back();
    if (w) {
      if (prevGroup) {
        w->assignLvGroup(prevGroup, true);
      } else if (!stack.empty()) {
        w->assignLvGroup(stack.back().group, true);
      } else {
        lv_group_set_default(NULL);
      }
    } else {
      lv_group_set_default(NULL);
    }
  } else {
    for (auto layer = stack.crbegin(); layer != stack.crend(); layer++) {
      if (layer->window == w) {
        stack.erase(layer.base());
        return;
      }
    }
    return;
  }
}

Window* Layer::back()
{
  if (stack.empty()) return nullptr;
  return stack.back().window;
}

Window* Layer::walk(std::function<bool(Window* w)> check)
{
  for (auto layer = stack.crbegin(); layer != stack.crend(); layer++) {
    if (layer->window && check(layer->window))
      return layer->window;
  }

  return nullptr;
}

//-----------------------------------------------------------------------------

std::list<Window *> Window::trash;

Window* Window::topWindow() { return Layer::back(); }

Window* Window::firstOpaque()
{
  Window* w = Layer::walk([=](Window *w) mutable -> bool {
    return w->hasWindowFlag(OPAQUE);
  });
  return w;
}

PageGroup* Window::pageGroup()
{
  Window* w = Layer::walk([=](Window *w) mutable -> bool {
    return w->isPageGroup();
  });
  return (PageGroup*)w;
}

//-----------------------------------------------------------------------------

const lv_obj_class_t window_base_class = {
    .base_class = &lv_obj_class,
    .constructor_cb = nullptr,
    .destructor_cb = nullptr,
    .user_data = nullptr,
    .event_cb = nullptr,
    .width_def = LV_DPI_DEF,
    .height_def = LV_DPI_DEF,
    .editable = LV_OBJ_CLASS_EDITABLE_FALSE,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_FALSE,
    .instance_size = sizeof(lv_obj_t)};

lv_obj_t *window_create(lv_obj_t *parent)
{
  return etx_create(&window_base_class, parent);
}

void Window::window_event_cb(lv_event_t *e)
{
  Window *window = (Window *)lv_obj_get_user_data(lv_event_get_target(e));
  if (window)
    window->eventHandler(e);
}

void Window::eventHandler(lv_event_t *e)
{
  static bool _longPressed = false;

  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_DELETE || deleted()) return;

  if (customEventHandler(code, e)) return;

  switch (code) {
    case LV_EVENT_SCROLL: {
      lv_obj_t *target = lv_event_get_target(e);
      // exclude pointer based scrolling (only focus scrolling)
      if (!lv_obj_is_scrolling(target) && ((windowFlags & NO_FORCED_SCROLL) == 0)) {
        lv_point_t *p = (lv_point_t *)lv_event_get_param(e);
        lv_coord_t scroll_y = lv_obj_get_scroll_y(target);
        lv_coord_t scroll_bottom = lv_obj_get_scroll_bottom(target);

        // Force scroll to top or bottom when near either edge.
        // Only applies when using rotary encoder or keys.
        // Limit is 2 standard size labels with some extra padding
        constexpr lv_coord_t NEAR_LIMIT =
            (EdgeTxStyles::STD_FONT_HEIGHT + PAD_TINY * 2 + PAD_OUTLINE * 2) * 2 + PAD_MEDIUM * 2;

        TRACE("SCROLL[x=%d;y=%d;top=%d;bottom=%d,limit=%d]", p->x, p->y, scroll_y,
              scroll_bottom,NEAR_LIMIT);

        lv_coord_t scroll_by = 0;
        if (scroll_y > 0 && scroll_y <= NEAR_LIMIT && p->y > 0) {
          scroll_by = scroll_y;
        } else if (scroll_bottom > 0 && scroll_bottom <= NEAR_LIMIT && p->y < 0) {
          scroll_by = -scroll_bottom;
        }
        if (scroll_by != 0) {
          lv_obj_scroll_by(target, 0, scroll_by, LV_ANIM_OFF);
          // Don't call scrollHandler until next update
          return;
        }
      }

      lv_coord_t scroll_x = lv_obj_get_scroll_x(target);
      lv_coord_t scroll_y = lv_obj_get_scroll_y(target);
      if (scrollHandler) scrollHandler(scroll_x, scroll_y);

    } break;
    case LV_EVENT_CLICKED:
      if (!_longPressed) {
        TRACE("CLICKED[%p]", this);
        // Close keyboard when clicking outside edit / keyboard windows
        if (!isEditWindow())
          Keyboard::hideKeyboard();
        onClicked();
      }
      _longPressed = false;
      break;
    case LV_EVENT_CANCEL:
      TRACE("CANCEL[%p]", this);
      onCancel();
      break;
    case LV_EVENT_FOCUSED:
      if (focusHandler) focusHandler(true);
      break;
    case LV_EVENT_DEFOCUSED:
      if (focusHandler) focusHandler(false);
      break;
    case LV_EVENT_LONG_PRESSED:
      TRACE("LONG PRESS[%p]", this);
      _longPressed = onLongPress();
      break;
    default:
      break;
  }
}

//-----------------------------------------------------------------------------

Window::Window(Window *parent, const rect_t &rect, LvglCreate objConstruct) :
    rect(rect), parent(parent)
{
  lv_obj_t *lv_parent = parent ? parent->lvobj : nullptr;

  if (objConstruct == nullptr) objConstruct = window_create;
  lvobj = objConstruct(lv_parent);

  lv_obj_set_user_data(lvobj, this);
  lv_obj_add_event_cb(lvobj, Window::window_event_cb, LV_EVENT_ALL, nullptr);

  if (rect.x || rect.y) lv_obj_set_pos(lvobj, rect.x, rect.y);
  if (rect.w) lv_obj_set_width(lvobj, rect.w);
  if (rect.h) lv_obj_set_height(lvobj, rect.h);

  lv_obj_clear_flag(lvobj, LV_OBJ_FLAG_SCROLL_ELASTIC);

  if (parent) {
    parent->addChild(this);
  }
}

void Window::delayLoader(lv_event_t* e)
{
  auto w = (Window*)lv_obj_get_user_data(lv_event_get_target(e));
  if (w && !w->loaded) {
    w->loaded = true;
    w->delayedInit();
  }
}

void Window::delayLoad()
{
  lv_obj_add_event_cb(lvobj, Window::delayLoader, LV_EVENT_DRAW_MAIN_BEGIN, nullptr);
}

#if defined(DEBUG_WINDOWS)
std::string Window::getName() const { return "Window"; }

std::string Window::getRectString() const
{
  char result[32];
  sprintf(result, "[%d, %d, %d, %d]", left(), top(), width(), height());
  return result;
}

std::string Window::getIndentString() const
{
  std::string result;
  auto tmp = parent;
  while (tmp) {
    result += "  ";
    tmp = tmp->getParent();
  }
  return result;
}

std::string Window::getWindowDebugString(const char *name) const
{
  return getName() + (name ? std::string(" [") + name + "] " : " ") +
         getRectString();
}
#endif

void Window::pushLayer(bool hideParent)
{
  if (!layerCreated) {
    parentHidden = hideParent;
    layerCreated = true;
    if (parentHidden && Window::topWindow()) Window::topWindow()->hide();
    Layer::push(this);
  }
}

void Window::popLayer()
{
  if (layerCreated) {
    Layer::pop(this);
    if (parentHidden && Window::topWindow()) Window::topWindow()->show();
    layerCreated = false;
    parentHidden = false;
  }
}

void Window::assignLvGroup(lv_group_t* g, bool setDefault)
{
  if (setDefault)
    lv_group_set_default(g);

  // associate it with all input devices
  lv_indev_t* indev = lv_indev_get_next(NULL);
  while (indev) {
    lv_indev_set_group(indev, g);
    indev = lv_indev_get_next(indev);
  }
}

void Window::setWindowFlag(WindowFlags flag)
{
  windowFlags |= flag;

  // honor the no focus flag of libopenui
  if (windowFlags & NO_FOCUS)
    lv_obj_clear_flag(lvobj, LV_OBJ_FLAG_CLICK_FOCUSABLE);

  if (windowFlags & NO_SCROLL)
    lv_obj_clear_flag(lvobj, LV_OBJ_FLAG_SCROLLABLE);

  if (windowFlags & NO_CLICK)
    lv_obj_clear_flag(lvobj, LV_OBJ_FLAG_CLICKABLE);
}

void Window::clearWindowFlag(WindowFlags flag) { windowFlags &= ~flag; }

void Window::setTextFlag(LcdFlags flag) { textFlags |= flag; }
void Window::clearTextFlag(LcdFlags flag) { textFlags &= ~flag; }

void Window::attach(Window *newParent)
{
  detach();
  if (newParent) {
    parent = newParent;
    newParent->addChild(this);
  }
}

void Window::detach()
{
  if (parent) {
    parent->children.remove(this);
    parent = nullptr;
  }
}

void Window::onClosing(CloseHandler h)
{
  closeHandlers.push_back(std::move(h));
}

// Close the window, and all its children.
// Remove from LVGL and save the window pointer for later destruction.
void Window::closeWindow()
{
  TRACE_WINDOWS("Close %p %s %s", this, getWindowDebugString().c_str(), deleted() ? "DELETED" : "");
  if (!deleted()) {
    _deleted = true;

    // Recursively delete child objects
    deleteChildren();

    // Remove from parent
    detach();

    // Remove layer (if needed) before calling handlers, so any windows
    // created by a handler are added to the restored lv_group
    popLayer();

    // Call onClosing handler functions (reverse order of creation)
    for (auto it = closeHandlers.rbegin(); it != closeHandlers.rend(); ++it) {
      (*it)();
    }
    closeHandlers.clear();

    // Save for destructor call
    trash.push_back(this);

    // Remove from LVGL
    if (lvobj != nullptr) {
      auto obj = lvobj;
      lvobj = nullptr;
      lv_obj_set_user_data(obj, nullptr);
      lv_obj_del(obj);
    }
  }
}

void Window::clear()
{
  deleteChildren();
  if (lvobj != nullptr) {
    lv_obj_clean(lvobj);
  }
  invalidate();
}

void Window::deleteChildren()
{
  while (!children.empty())
    children.back()->closeWindow();
}

bool Window::hasFocus() const
{
  return lvobj && lv_obj_has_state(lvobj, LV_STATE_FOCUSED);
}

void Window::padLeft(coord_t pad) { lv_obj_set_style_pad_left(lvobj, pad, 0); }

void Window::padRight(coord_t pad)
{
  lv_obj_set_style_pad_right(lvobj, pad, 0);
}

void Window::padTop(coord_t pad) { lv_obj_set_style_pad_top(lvobj, pad, 0); }

void Window::padBottom(coord_t pad)
{
  lv_obj_set_style_pad_bottom(lvobj, pad, 0);
}

void Window::padAll(PaddingSize pad) { etx_padding(lvobj, pad, LV_PART_MAIN); }

void Window::checkEvents()
{
  auto copy = children;
  for (auto child : copy) {
    if (!child->deleted()) {
      child->checkEvents();
    }
  }
}

void Window::onEvent(event_t event)
{
  TRACE_WINDOWS("%s received event 0x%X",
                Window::getWindowDebugString("Window").c_str(), event);
  if (parent)
    parent->onEvent(event);
}

void Window::onClicked()
{
  if (parent && !(windowFlags & OPAQUE))
    parent->onClicked();
}

void Window::onCancel()
{
  if (parent)
    parent->onCancel();
}

bool Window::onLongPress()
{
  return true;
}

void Window::addChild(Window *window)
{
  auto lv_parent = lv_obj_get_parent(window->lvobj);
  if (lv_parent && (lv_parent != lvobj)) {
    lv_obj_set_parent(window->lvobj, lvobj);
  }

  children.push_back(window);
}

void Window::invalidate()
{
  if (lvobj) lv_obj_invalidate(lvobj);
}

void Window::setFlexLayout(lv_flex_flow_t flow, lv_coord_t padding,
                           coord_t width, coord_t height)
{
  lv_obj_set_flex_flow(lvobj, flow);
  if ((_LV_FLEX_COLUMN & flow) || (_LV_FLEX_WRAP & flow)) {
    lv_obj_set_style_pad_row(lvobj, padding, LV_PART_MAIN);
  }
  if (!(_LV_FLEX_COLUMN & flow) || (_LV_FLEX_WRAP & flow)) {
    lv_obj_set_style_pad_column(lvobj, padding, LV_PART_MAIN);
  }
  lv_obj_set_width(lvobj, width);
  lv_obj_set_height(lvobj, height);
}

FormLine *Window::newLine(FlexGridLayout &layout)
{
  return new FormLine(this, layout);
}

void Window::show(bool visible)
{
  if (!deleted() && lvobj) {
    if (lv_obj_has_flag(lvobj, LV_OBJ_FLAG_HIDDEN) == visible) {
      if (visible)
        lv_obj_clear_flag(lvobj, LV_OBJ_FLAG_HIDDEN);
      else
        lv_obj_add_flag(lvobj, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

bool Window::isVisible()
{
  return !deleted() && lvobj && !lv_obj_has_flag(lvobj, LV_OBJ_FLAG_HIDDEN);
}

bool Window::isOnScreen()
{
  // Check window is at least partially visible
  if (!isVisible()) return false;
  lv_area_t a;
  lv_obj_get_coords(lvobj, &a);
  return a.x2 >= 0 && a.x1 < LCD_W && a.y2 >= 0 && a.y1 < LCD_H;
}

void Window::enable(bool enabled)
{
  if (!deleted() && lvobj) {
    if (lv_obj_has_state(lvobj, LV_STATE_DISABLED) == enabled) {
      if (enabled)
        lv_obj_clear_state(lvobj, LV_STATE_DISABLED);
      else
        lv_obj_add_state(lvobj, LV_STATE_DISABLED);
    }
  }
}

#if defined(HARDWARE_TOUCH)
void Window::addBackButton()
{
  new ButtonBase(
      this, {0, 0, EdgeTxStyles::MENU_HEADER_HEIGHT, EdgeTxStyles::MENU_HEADER_HEIGHT},
      [=]() -> uint8_t {
        onCancel();
        return 0;
      },
      window_create);
}

void Window::addCustomButton(coord_t x, coord_t y, std::function<void()> action)
{
  new ButtonBase(
    this, {x, y, EdgeTxStyles::MENU_HEADER_HEIGHT, EdgeTxStyles::MENU_HEADER_HEIGHT},
    [=]() -> uint8_t {
      action();
      return 0;
    },
    window_create);
}
#endif

//-----------------------------------------------------------------------------

void NavWindow::onEvent(event_t event)
{
  switch (event) {
#if defined(HARDWARE_KEYS)
    case EVT_KEY_BREAK(KEY_MODEL):
      onPressMDL();
      break;

    case EVT_KEY_LONG(KEY_MODEL):
      onLongPressMDL();
      break;

    case EVT_KEY_BREAK(KEY_SYS):
      onPressSYS();
      break;

    case EVT_KEY_LONG(KEY_SYS):
      onLongPressSYS();
      break;

    case EVT_KEY_BREAK(KEY_TELE):
      onPressTELE();
      break;

    case EVT_KEY_LONG(KEY_TELE):
      onLongPressTELE();
      break;

    case EVT_KEY_BREAK(KEY_PAGEDN):
      onPressPGDN();
      break;

    case EVT_KEY_BREAK(KEY_PAGEUP):
      onPressPGUP();
      break;

    case EVT_KEY_LONG(KEY_PAGEDN):
      onLongPressPGDN();
      break;

    case EVT_KEY_LONG(KEY_PAGEUP):
      onLongPressPGUP();
      break;

    case EVT_KEY_LONG(KEY_EXIT):
      onLongPressRTN();
      break;
#endif

    default:
      if (bubbleEvents()) Window::onEvent(event);
  }
}

NavWindow::NavWindow(Window *parent, const rect_t &rect,
                     LvglCreate objConstruct) :
    Window(parent, rect, objConstruct)
{
  setWindowFlag(OPAQUE | IS_NAV_WINDOW);
}

#if defined(HARDWARE_KEYS)
void NavWindow::onPressSYS() { doKeyShortcut(EVT_KEY_BREAK(KEY_SYS)); }
void NavWindow::onLongPressSYS() { doKeyShortcut(EVT_KEY_LONG(KEY_SYS)); }
void NavWindow::onPressMDL() { doKeyShortcut(EVT_KEY_BREAK(KEY_MODEL)); }
void NavWindow::onLongPressMDL() { doKeyShortcut(EVT_KEY_LONG(KEY_MODEL)); }
void NavWindow::onPressTELE() { doKeyShortcut(EVT_KEY_BREAK(KEY_TELE)); }
void NavWindow::onLongPressTELE() { doKeyShortcut(EVT_KEY_LONG(KEY_TELE)); }
#endif

//-----------------------------------------------------------------------------

#if defined(PCBPA01)
// ---------------------------------------------------------------------------
// Uniform setup-card glyphs
//
// PA01 uses the 320x240 bitmap set, whose builtin masks are all 24x24 with the
// artwork centred in the canvas. Only the oversized info/error glyph (77x77)
// is out of scale, so it is downsampled to the same 24px optical size.
//
// lv_img_set_zoom() is not an option: LVGL's software transform only handles
// true-colour images and our masks are LV_IMG_CF_ALPHA_8BIT. Instead resample
// the oversized glyph to the common native size.
// ---------------------------------------------------------------------------
static constexpr uint16_t PA01_ICON_INK_SIZE = 24;

static MaskBitmap* resampleMask(const MaskBitmap* src, uint16_t bx, uint16_t by,
                                uint16_t bw, uint16_t bh, uint16_t ow, uint16_t oh)
{
  auto* dst = reinterpret_cast<MaskBitmap*>(
      new uint8_t[sizeof(MaskBitmap) + (size_t)ow * oh]);
  dst->width = ow;
  dst->height = oh;

  for (uint16_t dy = 0; dy < oh; dy += 1) {
    uint16_t sy0 = by + (uint32_t)dy * bh / oh;
    uint16_t sy1 = by + (uint32_t)(dy + 1) * bh / oh;
    if (sy1 <= sy0) sy1 = sy0 + 1;
    for (uint16_t dx = 0; dx < ow; dx += 1) {
      uint16_t sx0 = bx + (uint32_t)dx * bw / ow;
      uint16_t sx1 = bx + (uint32_t)(dx + 1) * bw / ow;
      if (sx1 <= sx0) sx1 = sx0 + 1;
      uint32_t sum = 0, n = 0;
      for (uint16_t sy = sy0; sy < sy1; sy += 1) {
        const uint8_t* row = src->data + (size_t)sy * src->width;
        for (uint16_t sx = sx0; sx < sx1; sx += 1) {
          sum += row[sx];
          n += 1;
        }
      }
      dst->data[(size_t)dy * ow + dx] = n ? (uint8_t)(sum / n) : 0;
    }
  }
  return dst;
}

// Cards are rebuilt on every page entry, so the resampled buffers are cached:
// they are handed to StaticIcon, which does not own them.
static constexpr uint8_t PA01_ICON_CACHE_SLOTS = 16;
static const MaskBitmap* pa01IconCache[PA01_ICON_CACHE_SLOTS] = {};
static EdgeTxIcon pa01IconCacheId[PA01_ICON_CACHE_SLOTS] = {};
static uint8_t pa01IconCacheNext = 0;

static const MaskBitmap* pa01CardIcon(EdgeTxIcon icon)
{
  for (uint8_t i = 0; i < PA01_ICON_CACHE_SLOTS; i += 1)
    if (pa01IconCache[i] && pa01IconCacheId[i] == icon) return pa01IconCache[i];

  const MaskBitmap* src = getBuiltinIcon(icon);
  const MaskBitmap* result = src;

  if (src) {
    uint16_t minX = src->width, minY = src->height, maxX = 0, maxY = 0;
    for (uint16_t y = 0; y < src->height; y += 1) {
      for (uint16_t x = 0; x < src->width; x += 1) {
        if (src->data[(size_t)y * src->width + x] > 16) {
          if (x < minX) minX = x;
          if (x > maxX) maxX = x;
          if (y < minY) minY = y;
          if (y > maxY) maxY = y;
        }
      }
    }
    if (maxX >= minX && maxY >= minY) {
      uint16_t bw = maxX - minX + 1;
      uint16_t bh = maxY - minY + 1;
      uint16_t maxDim = bw > bh ? bw : bh;
      // Only shrink the outliers; upscaling would just blur the smaller glyphs.
      if (maxDim > PA01_ICON_INK_SIZE) {
        uint16_t ow = bw >= bh ? PA01_ICON_INK_SIZE
                               : (uint16_t)((uint32_t)bw * PA01_ICON_INK_SIZE / maxDim);
        uint16_t oh = bh >= bw ? PA01_ICON_INK_SIZE
                               : (uint16_t)((uint32_t)bh * PA01_ICON_INK_SIZE / maxDim);
        if (!ow) ow = 1;
        if (!oh) oh = 1;
        result = resampleMask(src, minX, minY, bw, bh, ow, oh);
      }
    }
  }

  pa01IconCache[pa01IconCacheNext] = result;
  pa01IconCacheId[pa01IconCacheNext] = icon;
  pa01IconCacheNext = (pa01IconCacheNext + 1) % PA01_ICON_CACHE_SLOTS;
  return result;
}

// Quick Menu tiles invert their content while focused (light fill + dark
// glyphs). The children carry the inverted styles on LV_STATE_USER_1, which we
// toggle here: LVGL has no "child of a focused parent" pseudo-state.
static void setup_tile_focus_cb(lv_event_t* e)
{
  lv_obj_t* obj = lv_event_get_target(e);
  bool focused = lv_event_get_code(e) == LV_EVENT_FOCUSED;
  uint32_t cnt = lv_obj_get_child_cnt(obj);
  for (uint32_t i = 0; i < cnt; i += 1) {
    lv_obj_t* child = lv_obj_get_child(obj, i);
    if (focused) {
      lv_obj_add_state(child, LV_STATE_USER_1);
    } else {
      lv_obj_clear_state(child, LV_STATE_USER_1);
    }
  }
}
#endif

class SetupTextButton : public TextButton
{
 public:
  SetupTextButton(Window* parent, const rect_t& rect, const PageButtonDef& entry) :
      TextButton(parent, rect, STR_VAL(entry.title))
  {
    setPressHandler([=] {
      entry.createPage();
      return 0;
    });
    setCheckHandler([=]() {
      if (entry.isActive) check(entry.isActive());
      if (entry.enabled) show(entry.enabled());
    });
    setWrap();

#if defined(PCBPA01)
    // Adopt the Quick Menu tile language so the settings grids look like the
    // rest of the UI: a lifted, borderless surface, a small caption and a
    // full inversion while focused (no coloured outline).
    etx_solid_bg(lvobj, COLOR_THEME_SECONDARY1_INDEX, LV_PART_MAIN);
    // Focus uses the accent colour, matching the Quick Menu tiles and the LED.
    etx_solid_bg(lvobj, COLOR_THEME_FOCUS_INDEX,
                 LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(lvobj, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(lvobj, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(lvobj, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(lvobj, 0, LV_PART_MAIN | LV_STATE_FOCUSED);
    padAll(PAD_SMALL);

    lv_obj_add_event_cb(lvobj, setup_tile_focus_cb, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(lvobj, setup_tile_focus_cb, LV_EVENT_DEFOCUSED, nullptr);

    if (label) {
      // Small caption type on every setup card, like the Quick Menu tiles.
      etx_font(label, FONT_XS_INDEX);
      etx_txt_color(label, COLOR_THEME_PRIMARY2_INDEX,
                    LV_PART_MAIN | LV_STATE_USER_1);
    }

    if (entry.icon != EDGETX_ICONS_COUNT) {
      // PRIMARY1 keeps the glyph legible on the dark card body. The mask is
      // normalised first so every card shows the same optical icon size.
      const MaskBitmap* glyph = pa01CardIcon(entry.icon);
      icon = glyph ? new StaticIcon(this, 0, 0, glyph, COLOR_THEME_PRIMARY1_INDEX)
                   : new StaticIcon(this, 0, 0, entry.icon, COLOR_THEME_PRIMARY1_INDEX);
      etx_img_color(icon->getLvObj(), COLOR_THEME_PRIMARY2_INDEX,
                    LV_PART_MAIN | LV_STATE_USER_1);

      // Icon on top, caption underneath, the pair centred in the card.
      // Positions must be relative to the card's *content* area: lv_obj_set_pos()
      // already starts inside the padding, so centring against width()/height()
      // (which include the padding) pushed the block down and to the right.
      if (label) {
        lv_obj_set_width(label, lv_pct(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
      }

      lv_obj_update_layout(lvobj);
      const coord_t contentW = lv_obj_get_content_width(lvobj);
      const coord_t contentH = lv_obj_get_content_height(lvobj);
      const coord_t labelH = label ? lv_obj_get_height(label) : 0;
      const coord_t blockH =
          icon->height() + (labelH ? labelH + PAD_TINY : 0);
      coord_t top = (contentH - blockH) / 2;
      if (top < 0) top = 0;

      icon->setPos((contentW - icon->width()) / 2, top);

      if (label) {
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0,
                     top + icon->height() + PAD_TINY);
      }
    }
#endif
  }

 protected:
#if defined(PCBPA01)
  StaticIcon* icon = nullptr;
#endif
};

SetupButtonGroup::SetupButtonGroup(Window* parent, const rect_t& rect, int cols,
                                   const PageButtonDef* pages, coord_t btnHeight) :
    Window(parent, rect)
{
  padAll(PAD_OUTLINE);
  setFlexLayout(LV_FLEX_FLOW_ROW_WRAP, PAD_SMALL);
  lv_obj_set_flex_align(lvobj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_SPACE_EVENLY);

  coord_t buttonWidth = (width() - PAD_SMALL * (cols + 1) - PAD_OUTLINE * 2) / cols;

  for (int p = 0; pages[p].title; p += 1) {
    new SetupTextButton(this, {0, 0, buttonWidth, btnHeight}, pages[p]);
  }
}

SetupLine::SetupLine(Window* parent, coord_t y, coord_t col2, PaddingSize padding, const char* title,
                    std::function<void(SetupLine*, coord_t, coord_t)> createEdit, coord_t lblYOffset) :
    Window(parent, {0, y, LCD_W - padding * 2, 0})
{
  padAll(PAD_ZERO);
  coord_t titleY = PAD_LARGE + lblYOffset;
  coord_t titleH = EdgeTxStyles::STD_FONT_HEIGHT;
  coord_t h = EdgeTxStyles::UI_ELEMENT_HEIGHT + PAD_TINY * 2 + lblYOffset * 2;
  if (createEdit) {
    coord_t editY = PAD_TINY;
    coord_t lblWidth = col2 - PAD_SMALL - PAD_TINY;
    if (title) {
      if (getTextWidth(title) >= lblWidth) {
        h += PAD_MEDIUM;
        titleY = 0;
        titleH = EdgeTxStyles::UI_ELEMENT_HEIGHT + PAD_TINY + PAD_LARGE;
        editY = PAD_SMALL + 1;
      }
      new StaticText(this, {PAD_TINY, titleY, lblWidth, titleH}, title);
    }
    setHeight(h);
    createEdit(this, col2, editY);
  } else {
    setHeight(h);
    new StaticText(this, {0, titleY, 0, titleH}, title, COLOR_THEME_PRIMARY1_INDEX, FONT(BOLD));
  }
}

coord_t SetupLine::showLines(Window* parent, coord_t y, coord_t col2, PaddingSize padding, const SetupLineDef* setupLines)
{
  Window* w;

  for (int i = 0; setupLines[i].title || setupLines[i].createEdit; i += 1) {
#if !defined(ALL_LANGS)
    w = new SetupLine(parent, y, col2, padding, setupLines[i].title, setupLines[i].createEdit);
#else
    w = new SetupLine(parent, y, col2, padding, setupLines[i].title ? setupLines[i].title() : nullptr, setupLines[i].createEdit);
#endif
    y += w->height() + padding;
  }

  return y;
}

//-----------------------------------------------------------------------------
