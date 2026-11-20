/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * PA01 RGB LED scene engine.
 *
 * Scenes are configured from a Lua tool (/SCRIPTS/TOOLS/ledfx.lua) which
 * writes /RADIO/ledfx.cfg. The firmware reads that file once at boot and
 * renders the effect itself, so the LEDs keep working without any script
 * running.
 */

#pragma once

#include <stdbool.h>

// Called periodically from ledLoop(): renders the active scene and returns
// true when the scene engine owns the strip (caller must skip its own logic).
bool ledSceneTick();
// 主界面循环首次运行：这里才开始播放开机场景（此时配置已可读）
void ledSceneUiTick();

// True when the scene engine is enabled in the config file.
bool ledSceneEnabled();

// 报警音事件
void ledSceneEventAlarm();

// Called from the Lua API when a script writes LED colors: the scene engine
// backs off for a while (and reloads the config afterwards).
void ledSceneOnLuaApply();
