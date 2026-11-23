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
#include <stdint.h>

// 开机早期调用（rgbPowerOn）：用上次保存的 boot 配置渲染一帧，
// 真正替换固化在代码里的默认 boot 灯效。无有效配置时返回 false。
bool ledBootEarlyFrame(uint32_t t);

// Called periodically from ledLoop(): renders the active scene and returns
// true when the scene engine owns the strip (caller must skip its own logic).
bool ledSceneTick();
// 主界面循环首次运行：加载 ledfx.cfg 并按配置渲染
void ledSceneUiTick();

// True when the scene engine is enabled in the config file.
bool ledSceneEnabled();

// 报警音事件
void ledSceneEventAlarm();

// Called from the Lua API when a script writes LED colors: the scene engine
// backs off for a while (and reloads the config afterwards).
void ledSceneOnLuaApply();
