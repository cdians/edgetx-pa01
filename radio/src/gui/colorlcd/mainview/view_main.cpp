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

#include "view_main.h"

#include "edgetx.h"
#include "mainwindow.h"
#include "model_select.h"
#include "quick_menu.h"
#include "radio_tools.h"
#include "screen_setup.h"
#include "theme_manager.h"
#include "topbar.h"
#include "view_channels.h"
#include "widget.h"

#if defined(PCBPA01)
// PA01: the framed EdgeTX tile in the top-left corner is replaced by a brand
// line - an accent dot and the model name. The strip keeps the same touch area
// so tapping it still opens the Quick Menu.
static lv_obj_t* brandNameLabel = nullptr;
static char brandNameText[LEN_MODEL_NAME + 1] = "";

// ViewMain survives a model switch (only the custom screens are reloaded), so
// the brand line has to follow g_model instead of being set once at build time.
static void updateBrandName()
{
  if (!brandNameLabel) return;
  const char* name = g_model.header.name[0] ? g_model.header.name : "MODEL";
  if (strcmp(name, brandNameText) == 0) return;
  strAppend(brandNameText, name, LEN_MODEL_NAME + 1);
  lv_label_set_text(brandNameLabel, brandNameText);
}

class MainViewBrand : public Window
{
 public:
  MainViewBrand(Window* parent, std::function<void()> action) :
      Window(parent, {0, 0, LCD_W, EdgeTxStyles::MENU_HEADER_HEIGHT}),
      action(std::move(action))
  {
    setWindowFlag(NO_FOCUS | NO_CLICK);

    auto dot = lv_obj_create(lvobj);
    lv_obj_remove_style_all(dot);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    etx_solid_bg(dot, COLOR_THEME_FOCUS_INDEX);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    // 5px, per the design. At 8px it competed with the model name instead of
    // sitting quietly in front of it.
    lv_obj_set_size(dot, 5, 5);
    lv_obj_set_pos(dot, PAD_MEDIUM, (EdgeTxStyles::MENU_HEADER_HEIGHT - 5) / 2);

    // 14px: the model name is the one piece of identity in the bar, so it gets a
    // step above the 10px status text instead of matching it.
    nameLabel = etx_label_create(lvobj, FONT_XS_INDEX);
    lv_obj_set_width(nameLabel, LCD_W - 60);
    lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_DOT);
    etx_txt_color(nameLabel, COLOR_THEME_PRIMARY1_INDEX);
    lv_obj_clear_flag(nameLabel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(nameLabel, LV_ALIGN_LEFT_MID, PAD_MEDIUM + 5 + PAD_SMALL, 0);
    brandNameLabel = nameLabel;
    updateBrandName();

#if defined(HARDWARE_TOUCH)
    if (this->action) addCustomButton(0, 0, [=]() { this->action(); });
#endif
  }

  ~MainViewBrand() override
  {
    if (brandNameLabel == nameLabel) {
      brandNameLabel = nullptr;
      brandNameText[0] = '\0';
    }
  }

 protected:
  std::function<void()> action;
  lv_obj_t* nameLabel = nullptr;
};
#endif

static void saveViewId(unsigned view)
{
  if (view != g_model.view) {
    TRACE("save view #%d", view);
    g_model.view = view;
    storageDirty(EE_MODEL);
  }
}

static void tile_view_deleted_cb(lv_event_t* e)
{
  lv_obj_t* target = lv_event_get_target(e);
  lv_obj_t* obj = lv_event_get_current_target(e);

  // LV_EVENT_CHILD_DELETED is bubbled to all parents, so
  // we'd better make sure this is one of our own.
  if (obj == target) {
    TRACE("CHILD_DELETED tile[%d]", lv_event_get_user_data(e));
    lv_obj_del(obj);
  }
}

static void tile_view_scroll_begin(lv_event_t * e)
{
	lv_anim_t* a = (lv_anim_t*)lv_event_get_param(e);
	if (a) a->time = 0;
}

static void tile_view_scroll(lv_event_t* e)
{
  auto viewMain = ViewMain::instance();
  if (viewMain) {
    viewMain->updateTopbarVisibility();
  }
}

static void tile_view_scroll_end(lv_event_t* e)
{
  auto viewMain = ViewMain::instance();
  if (viewMain) {
    auto view = viewMain->getCurrentMainView();
    saveViewId(view);
  }
}

ViewMain* ViewMain::_instance = nullptr;

ViewMain* ViewMain::instance()
{
  if (!_instance)
    _instance = new ViewMain();
  return _instance;
}

ViewMain::ViewMain() :
    NavWindow(MainWindow::instance(), MainWindow::instance()->getRect())
{
  pushLayer();

  tile_view = lv_tileview_create(lvobj);
  lv_obj_set_pos(tile_view, rect.x, rect.y);
  lv_obj_set_size(tile_view, rect.w, rect.h);
  lv_obj_set_scrollbar_mode(tile_view, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(tile_view, LV_OBJ_FLAG_SCROLL_ELASTIC);

  lv_obj_add_flag(tile_view, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_user_data(tile_view, this);
  lv_obj_add_event_cb(tile_view, tile_view_scroll_begin, LV_EVENT_SCROLL_BEGIN, NULL);
  lv_obj_add_event_cb(tile_view, tile_view_scroll, LV_EVENT_SCROLL, nullptr);
  lv_obj_add_event_cb(tile_view, tile_view_scroll_end, LV_EVENT_SCROLL_END, nullptr);

  // create last to be on top
  topbar = new TopBar(this);
#if defined(PCBPA01)
  headerIcon = new MainViewBrand(this, [=]() { QuickMenu::openQuickMenu(); });
#else
  headerIcon = new HeaderIcon(this, ICON_EDGETX, [=]() { QuickMenu::openQuickMenu(); });
#endif

  onClosing([=]() {
    _instance = nullptr;
  });
}

void ViewMain::addMainView(WidgetsContainer* view, uint32_t viewId)
{
  TRACE("addMainView(0x%p, %d)", view, viewId);

  auto tile =
      lv_tileview_add_tile(tile_view, viewId, 0, LV_DIR_LEFT | LV_DIR_RIGHT);

  auto view_obj = view->getLvObj();
  lv_obj_set_parent(view_obj, tile);

  auto user_data = (void*)(intptr_t)viewId;
  lv_obj_add_event_cb(tile, tile_view_deleted_cb, LV_EVENT_CHILD_DELETED,
                      user_data);

  view->show();
}

void ViewMain::setTopbarVisible(float visible)
{
  topbar->setVisible(visible);
}

void ViewMain::setEdgeTxButtonVisible(float visible)
{
  headerIcon->show(isVisible);

  if (isVisible)
  {
    coord_t y = 0;
    if (visible == 0.0f) {
      y = -EdgeTxStyles::MENU_HEADER_HEIGHT;
    } else if (visible > 0.0f && visible < 1.0f){
      y = -(float)EdgeTxStyles::MENU_HEADER_HEIGHT * (1.0f - visible);
    }
    if (y != headerIcon->top()) headerIcon->setTop(y);
  }
}

unsigned ViewMain::getMainViewsCount() const
{
  return lv_obj_get_child_cnt(tile_view);
}

unsigned ViewMain::getCurrentMainView() const
{
  return lv_obj_get_scroll_x(tile_view) / width();
}

void ViewMain::setCurrentMainView(unsigned viewId)
{
  lv_obj_set_tile_id(tile_view, viewId, 0, LV_ANIM_OFF);
}

void setRequestedMainView(uint8_t view)
{
  auto viewMain = ViewMain::instance();
  if (view < viewMain->getMainViewsCount()) {
    viewMain->setCurrentMainView(view);
    saveViewId(view);
  }
}

void ViewMain::nextMainView()
{
  auto view = getCurrentMainView();
  if (++view >= getMainViewsCount()) view = 0;

  setCurrentMainView(view);
  saveViewId(view);
}

void ViewMain::previousMainView()
{
  auto view = getCurrentMainView();
  if (view > 0)
    view--;
  else
    view = getMainViewsCount() - 1;

  setCurrentMainView(view);
  saveViewId(view);
}

TopBar* ViewMain::getTopbar() { return topbar; }

void ViewMain::updateTopbarVisibility()
{
  if (!tile_view) return;

  coord_t scrollPos = lv_obj_get_scroll_x(tile_view);
  coord_t pageWidth = width();
  if (!pageWidth) return;

  int leftScroll = scrollPos % width();
  if (leftScroll == 0) {
    int view = scrollPos / pageWidth;
    setTopbarVisible(isVisible && hasTopbar(view) ? 1.0f : 0.0f);
    showTopBarEdgeTxButton();
  } else {
    int leftIdx = scrollPos / pageWidth;
    bool leftTopbar = hasTopbar(leftIdx);
    bool rightTopbar = hasTopbar(leftIdx + 1);

    float ratio;

    if (leftTopbar && rightTopbar) {
      ratio = 1.0f;
    } else if (leftTopbar) {
      // scrolling from a screen with Topbar
      ratio = 1.0f - (float)leftScroll / (float)pageWidth;
    } else if (rightTopbar) {
      // scrolling to a screen with Topbar
      ratio = (float)leftScroll / (float)pageWidth;
    } else {
      ratio = 0.0f;
    }

    setTopbarVisible(ratio);

    leftTopbar = hasTopbar(leftIdx) || isAppMode(leftIdx);
    rightTopbar = hasTopbar(leftIdx + 1) || isAppMode(leftIdx + 1);

    ratio = (float)leftScroll / (float)pageWidth;

    if (leftTopbar && rightTopbar) {
      ratio = 1.0f;
    } else if (leftTopbar) {
      // scrolling from a screen with Topbar
      ratio = 1.0f - (float)leftScroll / (float)pageWidth;
    } else if (rightTopbar) {
      // scrolling to a screen with Topbar
      ratio = (float)leftScroll / (float)pageWidth;
    } else {
      ratio = 0.0f;
    }

    setEdgeTxButtonVisible(ratio);
  }
}

#if defined(HARDWARE_KEYS)
void ViewMain::doKeyShortcut(event_t event)
{
  QMPage pg = g_eeGeneral.getKeyShortcut(event);
  if (pg == QM_APP) {
    runLuaTool(g_eeGeneral.getKeyToolName(event));
  } else if (pg == QM_OPEN_QUICK_MENU) {
    QuickMenu::openQuickMenu();
  } else {
    QuickMenu::openPage(pg);
  }
}
void ViewMain::onPressPGUP()
{
  if (!widget_select) {
    previousMainView();
  }
}
void ViewMain::onPressPGDN()
{
  if (!widget_select) {
    nextMainView();
  }
}
#endif

void ViewMain::onClicked() { QuickMenu::openQuickMenu(); }

void ViewMain::onCancel()
{
  if (widget_select) {
    enableWidgetSelect(false);
  }
}

void ViewMain::refreshWidgetSelectTimer()
{
  widgetSelectCancelTime = get_tmr10ms() + 1000; // 10 seconds
}

void ViewMain::enableWidgetSelect(bool enable)
{
  TRACE("enableWidgetSelect(%d)", enable);
  if (widget_select == enable) return;
  widget_select = enable;

  lv_obj_t* tile = lv_tileview_get_tile_act(tile_view);
  if (!tile) return;

  auto cont_obj = lv_obj_get_child(tile, 0);
  if (!cont_obj) return;

  auto cont = (WidgetsContainer*)lv_obj_get_user_data(cont_obj);

  for (uint32_t i = 0; i < cont->getZonesCount(); i++) {
    Widget* widget = cont->getWidget(i);
    if (widget)
      widget->enableFocus(enable);
  }

  if (enable) {
    lv_obj_clear_flag(tile_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(tile_view, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_clear_flag(tile_view, LV_OBJ_FLAG_SCROLL_CHAIN_VER);

    refreshWidgetSelectTimer();
  } else {
    lv_obj_add_flag(tile_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tile_view, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_add_flag(tile_view, LV_OBJ_FLAG_SCROLL_CHAIN_VER);

    widgetSelectCancelTime = 0;
  }
}

bool ViewMain::onLongPress()
{
  if (isAppMode()) {
    int view = getCurrentMainView();
    if (customScreens[view]->getWidget(0))
      customScreens[view]->getWidget(0)->setFullscreen(true);
  } else {
    enableWidgetSelect(true);
  }
  killEvents(KEY_ENTER);
  lv_indev_wait_release(lv_indev_get_act());
  return false;
}

void ViewMain::show(bool visible)
{
  if (deleted()) return;
  isVisible = visible;
  int view = getCurrentMainView();
  setTopbarVisible(isVisible && hasTopbar(view) ? 1.0f : 0.0f);
  showTopBarEdgeTxButton();
  for (int i = 0; i < MAX_CUSTOM_SCREENS; i += 1) {
    if (customScreens[i]) {
      customScreens[i]->show(visible);
      customScreens[i]->showWidgets(visible);
    }
  }
}

bool ViewMain::isAppMode()
{
  return isAppMode(getCurrentMainView());
}

bool ViewMain::isAppMode(unsigned view)
{
  if (view < MAX_CUSTOM_SCREENS && customScreens[view])
    return ((Layout*)customScreens[view])->isAppMode();
  return false;
}

bool ViewMain::hasTopbar()
{
  return hasTopbar(getCurrentMainView());
}

bool ViewMain::hasTopbar(unsigned view)
{
  if (view < MAX_CUSTOM_SCREENS)
    return g_model.getScreenLayoutData(view)->options[LAYOUT_OPTION_TOPBAR].value.boolValue;
  return false;
}

void ViewMain::showTopBarEdgeTxButton()
{
  setEdgeTxButtonVisible(isVisible && (hasTopbar() || isAppMode()) ? 1.0f : 0.0f);
}

void ViewMain::hideTopBarEdgeTxButton()
{
  setEdgeTxButtonVisible(0.0f);
}

void ViewMain::_refreshWidgets()
{
#if defined(PCBPA01)
  updateBrandName();
#endif
  if (!deleted()) {
    topbar->refreshWidgets(isVisible && hasTopbar());
    for (int i = 0; i < MAX_CUSTOM_SCREENS; i += 1) {
      if (customScreens[i])
        customScreens[i]->refreshWidgets(isVisible);
    }
    if (widget_select && widgetSelectCancelTime < get_tmr10ms())
      enableWidgetSelect(false);
  }
}

void ViewMain::refreshWidgets()
{
  if (_instance) _instance->_refreshWidgets();
}
