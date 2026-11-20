/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * PA01 RGB LED scene engine.
 *
 * Config file: /RADIO/ledfx.cfg, written by /SCRIPTS/TOOLS/ledfx.lua
 *
 *   global=eventDur(100ms),mask,enabled
 *   <scene>=fx,speed,bright,r,g,b,enabled[,trigger[,r2,g2,b2[,keyMode]]]]
 *
 * fx: 0 off, 1 solid, 2 breath, 3 rainbow, 4 wave, 5 scroll, 6 police, 7 throttle
 *     8 ink, 9 alternate(left/right swap), 10 rainbow breath
 * mask bit0 = function LEDs (0..7,10..13), bit1 = power key LEDs (8..9)
 * the pw scene drives LEDs 8..9 only
 * eventDur is in 100ms units
 *
 * trigger (optional, 0 = any):
 *   key scene: 1 page up, 2 page down, 3 exit, 4 model, 5 enter
 *   keyMode:   0 press (default), 1 short press, 2 long press
 *   sw scene:  1..4 function switch
 *   thr scene: throttle threshold in percent (0..100)
 */

#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "edgetx.h"
#include "board.h"
#include "ff.h"
#include "keys.h"

// 输入通道（mixer.cpp）
extern int16_t channelOutputs[];

#include "boards/generic_stm32/rgb_leds.h"
#include "battery_driver.h"
#include "led_scene.h"

#define LEDFX_FILE "/RADIO/ledfx.cfg"
#define LEDFX_LEDS 14
#define LED_POWER_FIRST 8
#define LED_POWER_LAST 9

enum : uint8_t {
  SC_IDLE, SC_CHARGE, SC_FULL, SC_LOW, SC_CRIT,
  SC_THR, SC_ALARM, SC_BOOT, SC_POWER, SC_COUNT
};

// 没有可显示的场景（对应灯位保持熄灭）
static const uint8_t SC_NONE = 0xFF;

enum : uint8_t {
  FX_OFF, FX_SOLID, FX_BREATH, FX_RAINBOW, FX_WAVE, FX_SCROLL, FX_POLICE, FX_THROTTLE,
  FX_INK, FX_TREE, FX_BREATH_HSV
};

struct FxCfg {
  uint8_t fx;
  uint8_t speed;
  uint8_t bright;
  uint8_t r, g, b;
  uint8_t r2, g2, b2;
  uint8_t enabled;
  uint8_t trig;
};

static FxCfg cfg[SC_COUNT];
static bool cfgLoaded;
static uint8_t cfgTries;
static uint32_t lastCfgTry;
static bool fxEnabled = true;
static uint8_t fxMask = 0x03;
static bool uiStarted;      // 主界面循环已开始
static bool bootPending = true;
static uint8_t lastScene = 0xFF;
static uint8_t fadeScene = 0xFF;
static uint32_t fadeStart;
static uint8_t fadeK;       // 0..255，当前场景的不透明度
static const uint32_t fadeMs = 600;
static uint16_t eventDur100ms = 10;

static uint32_t alarmUntil;
static uint32_t bootUntil;
static uint32_t phaseMs;
static uint32_t lastMs;
static uint32_t luaOwnUntil;
static uint32_t luaResumeUntil;
static bool luaWasActive;

// ---------------------------------------------------------------- defaults

static void setCfg(uint8_t sc, uint8_t fx, uint8_t speed, uint8_t bright,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t on, uint8_t trig = 0)
{
  cfg[sc].fx = fx;
  cfg[sc].speed = speed;
  cfg[sc].bright = bright;
  cfg[sc].r = r;
  cfg[sc].g = g;
  cfg[sc].b = b;
  cfg[sc].r2 = r;
  cfg[sc].g2 = g;
  cfg[sc].b2 = b;
  cfg[sc].enabled = on;
  cfg[sc].trig = trig;
}

// ---------------------------------------------------------------- parsing

static int parseInts(const char* p, int* v, int maxv)
{
  int n = 0;
  while (*p && n < maxv) {
    while (*p == ',' || *p == ' ' || *p == '\t' || *p == '\r') p++;
    if (*p < '0' || *p > '9') break;
    v[n++] = (int)strtol(p, (char**)&p, 10);
  }
  return n;
}

static int8_t sceneByName(const char* k)
{
  if (!strcmp(k, "idle"))   return SC_IDLE;
  if (!strcmp(k, "charge")) return SC_CHARGE;
  if (!strcmp(k, "full"))   return SC_FULL;
  if (!strcmp(k, "low"))    return SC_LOW;
  if (!strcmp(k, "crit"))   return SC_CRIT;
  if (!strcmp(k, "thr"))    return SC_THR;
  if (!strcmp(k, "alarm"))  return SC_ALARM;
  if (!strcmp(k, "boot"))   return SC_BOOT;
  if (!strcmp(k, "pw"))     return SC_POWER;
  return -1;
}

static void parseLine(char* line)
{
  char* eq = strchr(line, '=');
  if (!eq) return;
  *eq = 0;

  int v[14];
  int n = parseInts(eq + 1, v, 14);
  if (n == 0) return;

  if (!strcmp(line, "global")) {
    // global=eventDur(100ms),mask,enabled
    if (n >= 1) eventDur100ms = (uint16_t)v[0];
    if (n >= 2) fxMask = (uint8_t)v[1];
    if (n >= 3) fxEnabled = v[2] != 0;
    return;
  }

  int8_t sc = sceneByName(line);
  if (sc < 0 || n < 7) return;
  setCfg(sc, v[0], v[1], v[2], v[3], v[4], v[5], v[6],
         n >= 8 ? (uint8_t)v[7] : 0);
  if (n >= 11) {
    cfg[sc].r2 = (uint8_t)v[8];
    cfg[sc].g2 = (uint8_t)v[9];
    cfg[sc].b2 = (uint8_t)v[10];
  }
}

static void loadCfg()
{
  FIL f;
  if (f_open(&f, LEDFX_FILE, FA_READ) != FR_OK) {
    // SD 还没挂载好时继续重试，约 10 秒后放弃（灯带由配置文件完全决定，不做内置兜底）
    if (++cfgTries > 20) cfgLoaded = true;
    return;
  }
  cfgLoaded = true;
  static char buf[1024];
  UINT br = 0;
  if (f_read(&f, buf, sizeof(buf) - 1, &br) != FR_OK) {
    f_close(&f);
    return;
  }
  f_close(&f);
  buf[br] = 0;

  char* p = buf;
  while (*p) {
    char* nl = strchr(p, '\n');
    if (nl) *nl = 0;
    if (*p && *p != '#') parseLine(p);
    if (!nl) break;
    p = nl + 1;
  }
}

// ---------------------------------------------------------------- effects

static void hsv2rgb(uint16_t h, uint8_t s, uint8_t v,
                    uint8_t* r, uint8_t* g, uint8_t* b)
{
  if (h >= 360) h %= 360;
  uint8_t region = h / 60;
  uint16_t rem = (h % 60) * 255 / 60;

  uint8_t p = (uint8_t)((v * (255 - s)) / 255);
  uint8_t q = (uint8_t)((v * (255 - (s * rem) / 255)) / 255);
  uint8_t t = (uint8_t)((v * (255 - (s * (255 - rem)) / 255)) / 255);

  switch (region) {
    case 0: *r = v; *g = t; *b = p; break;
    case 1: *r = q; *g = v; *b = p; break;
    case 2: *r = p; *g = v; *b = t; break;
    case 3: *r = p; *g = q; *b = v; break;
    case 4: *r = t; *g = p; *b = v; break;
    default: *r = v; *g = p; *b = q; break;
  }
}

// 往返位置（-1..1 的正弦映射到 0..n-1）
static int bouncePos(float deg, int n)
{
  float s = (sinf(deg * (float)M_PI / 180.0f) + 1.0f) * 0.5f;
  int p = (int)(s * (n - 1) + 0.5f);
  if (p < 0) p = 0;
  if (p >= n) p = n - 1;
  return p;
}

static bool isPowerLed(uint8_t i)
{
  return i >= LED_POWER_FIRST && i <= LED_POWER_LAST;
}

// 左组：功能键 3/4（0..3）+ 环绕左（12..13）
static bool isLeftLed(uint8_t i)
{
  return i <= 3 || i >= 12;
}

// 速度档位 → 倍率（低档更慢）
static const float SPD[11] = {
  0.40f, 0.55f, 0.75f, 1.00f, 1.35f, 1.80f, 2.40f, 3.20f, 4.20f, 5.50f, 7.00f
};

// idx 为组内序号，count 为组内灯数
static void colorFor(const FxCfg& c, uint8_t hw, uint8_t idx, uint8_t count,
                     uint32_t t, uint8_t* pr, uint8_t* pg, uint8_t* pb)
{
  float sp = SPD[c.speed > 10 ? 10 : c.speed];
  uint8_t r = 0, g = 0, b = 0;

  switch (c.fx) {
    case FX_SOLID:
      r = c.r; g = c.g; b = c.b;
      break;

    case FX_BREATH: {
      uint32_t period = (uint32_t)(3600.0f / sp);
      uint32_t half = period / 2;
      uint32_t p = t % period;
      uint32_t k = (p < half) ? (p * 255 / half) : ((period - p) * 255 / half);
      r = c.r * k / 255; g = c.g * k / 255; b = c.b * k / 255;
      break;
    }

    case FX_RAINBOW: {
      uint16_t h = (uint16_t)((uint32_t)((float)t * sp / 12.0f) % 360);
      hsv2rgb(h, 255, 255, &r, &g, &b);
      break;
    }

    case FX_WAVE: {
      uint16_t h = (uint16_t)(((uint32_t)((float)t * sp / 12.0f) + idx * 24) % 360);
      hsv2rgb(h, 255, 255, &r, &g, &b);
      break;
    }

    case FX_SCROLL: {
      // 色1 亮点，色2 底色
      uint32_t step = (uint32_t)(120.0f / sp);
      uint8_t pos = count ? (uint8_t)((t / step) % count) : 0;
      if (idx == pos) { r = c.r; g = c.g; b = c.b; }
      else { r = c.r2 / 5; g = c.g2 / 5; b = c.b2 / 5; }
      break;
    }

    case FX_POLICE: {
      // 奇偶灯交替 色1 / 色2
      uint32_t flip = (uint32_t)(500.0f / sp);
      bool ph = ((t / flip) & 1) != 0;
      bool odd = (idx & 1) != 0;
      if (ph ^ odd) { r = c.r; g = c.g; b = c.b; }
      else { r = c.r2; g = c.g2; b = c.b2; }
      break;
    }

    case FX_THROTTLE: {
      int16_t thr = channelOutputs[2];
      int32_t v = ((int32_t)thr + 1024) * 200 / 2048;
      if (v < 0) v = 0;
      if (v > 255) v = 255;
      r = (uint8_t)(c.r * v / 255);
      g = (uint8_t)(c.g * v / 255);
      b = (uint8_t)(c.b * v / 255);
      break;
    }

    case FX_INK: {
      // 色1 底色 + 两个往返的色2 光点
      r = c.r; g = c.g; b = c.b;
      float d1 = fmodf((float)t * 0.072f * sp, 360.0f);
      float d2 = fmodf((float)t * 0.048f * sp + 180.0f, 360.0f);
      if (idx == bouncePos(d1, count)) {
        r = c.r2; g = c.g2; b = c.b2;
      } else if (idx == bouncePos(d2, count)) {
        r = c.r2 * 3 / 5; g = c.g2 * 3 / 5; b = c.b2 * 3 / 5;
      }
      break;
    }

    case FX_TREE: {
      // 交替：左组 / 右组 定时互换
      uint32_t flip = (uint32_t)(1000.0f / sp);
      bool ph = ((t / flip) & 1) != 0;
      if (isLeftLed(hw) ^ ph) { r = c.r; g = c.g; b = c.b; }
      else { r = c.r2; g = c.g2; b = c.b2; }
      break;
    }

    case FX_BREATH_HSV: {
      uint32_t period = (uint32_t)(3600.0f / sp);
      uint32_t half = period / 2;
      uint32_t p = t % period;
      uint32_t k = (p < half) ? (p * 255 / half) : ((period - p) * 255 / half);
      uint8_t rr, gg, bb;
      hsv2rgb((uint16_t)((uint32_t)((float)t * sp / 12.0f) % 360), 255, 255, &rr, &gg, &bb);
      r = (uint8_t)(rr * k / 255);
      g = (uint8_t)(gg * k / 255);
      b = (uint8_t)(bb * k / 255);
      break;
    }

    default:
      break;
  }

  *pr = r; *pg = g; *pb = b;
}

static void ledColorOf(const FxCfg& c, uint8_t hw, uint8_t idx, uint8_t count,
                       uint32_t t, uint8_t* r, uint8_t* g, uint8_t* b)
{
  if (c.fx == FX_OFF) {
    *r = *g = *b = 0;
    return;
  }
  colorFor(c, hw, idx, count, t, r, g, b);
  uint16_t br = (uint16_t)c.bright * 255 / 100;
  *r = (uint8_t)(*r * br / 255);
  *g = (uint8_t)(*g * br / 255);
  *b = (uint8_t)(*b * br / 255);
}

static void render(uint8_t activeScene)
{
  static const FxCfg noneCfg = {0};
  const FxCfg& act = (activeScene == SC_NONE) ? noneCfg : cfg[activeScene];
  const FxCfg& pw = cfg[SC_POWER];
  uint32_t t = phaseMs;
  uint8_t seq = 0;
  const uint8_t pwCount = LED_POWER_LAST - LED_POWER_FIRST + 1;
  const uint8_t mainCount = LEDFX_LEDS - pwCount;

  for (uint8_t hw = 0; hw < LEDFX_LEDS; hw++) {
    const FxCfg* c;
    uint8_t idx, count;

    if (isPowerLed(hw)) {
      if (!(fxMask & 0x02) || !pw.enabled) {
        rgbSetLedColor(hw, 0, 0, 0);
        continue;
      }
      c = &pw;
      idx = hw - LED_POWER_FIRST;
      count = pwCount;
    } else {
      if (!(fxMask & 0x01)) {
        rgbSetLedColor(hw, 0, 0, 0);
        continue;
      }
      c = &act;
      idx = seq++;
      count = mainCount;
    }

    uint8_t r, g, b;
    ledColorOf(*c, hw, idx, count, t, &r, &g, &b);

    // 开机场景淡出到后续场景
    if (fadeScene != 0xFF && !isPowerLed(hw)) {
      uint8_t fr, fg, fb;
      ledColorOf(cfg[fadeScene], hw, idx, count, t, &fr, &fg, &fb);
      r = (uint8_t)(((uint16_t)r * fadeK + (uint16_t)fr * (255 - fadeK)) / 255);
      g = (uint8_t)(((uint16_t)g * fadeK + (uint16_t)fg * (255 - fadeK)) / 255);
      b = (uint8_t)(((uint16_t)b * fadeK + (uint16_t)fb * (255 - fadeK)) / 255);
    }

    rgbSetLedColor(hw, r, g, b);
  }

  rgbLedColorApply();
}

// ---------------------------------------------------------------- scenes

static bool batteryCritical()
{
  return g_vbat100mV <= BATTERY_MIN + (BATTERY_MAX - BATTERY_MIN) * 8 / 100;
}

// 油门通道位置 0..100%
static int32_t throttlePercent()
{
  return ((int32_t)channelOutputs[2] + 1024) * 100 / 2048;
}

// 物理按键 → 配置里的绑定编号（0 = 不在列表里）
static uint8_t pickScene(uint32_t now)
{
  if (now < bootUntil) {
    // 开机场景启用就播放；关闭时这段时间保持熄灭，不直接跳到待机
    return cfg[SC_BOOT].enabled ? SC_BOOT : SC_NONE;
  }
  if (cfg[SC_CRIT].enabled && batteryCritical()) return SC_CRIT;
  if (cfg[SC_LOW].enabled && IS_TXBATT_WARNING()) return SC_LOW;
  if (cfg[SC_ALARM].enabled && now < alarmUntil) return SC_ALARM;

  uint16_t cs = get_battery_charge_state();
  if (cfg[SC_CHARGE].enabled && cs == CHARGE_STARTED) return SC_CHARGE;
  if (cfg[SC_FULL].enabled && cs == CHARGE_FINISHED) return SC_FULL;

  if (cfg[SC_THR].enabled && cfg[SC_THR].trig > 0 &&
      throttlePercent() >= (int32_t)cfg[SC_THR].trig)
    return SC_THR;

  return cfg[SC_IDLE].enabled ? SC_IDLE : SC_NONE;
}

// ---------------------------------------------------------------- public

bool ledSceneEnabled()
{
  return cfgLoaded && fxEnabled;
}

void ledSceneUiTick()
{
  if (!uiStarted) {
    uiStarted = true;
    if (!cfgLoaded) loadCfg();

    // 保证进入主界面后还能看到开机场景，再做淡出过渡
    uint32_t now = timersGetMsTick();
    if (cfgLoaded) {
      if (bootPending) {
        bootPending = false;
        bootUntil = now + 3000;
      } else if (bootUntil > now && bootUntil < now + 1500) {
        bootUntil = now + 1500;
      }
    }
  }

  ledSceneTick();
}

bool ledSceneTick()
{
  uint32_t now = timersGetMsTick();

  // 脚本正在预览：让出灯带；脚本停止后重新读取它保存的配置
  if (now < luaOwnUntil) {
    luaWasActive = true;
    lastMs = now;
    return true;
  }
  if (luaWasActive) {
    luaWasActive = false;
    cfgLoaded = false;
    cfgTries = 0;
    // 脚本刚让出灯带时留一小段缓冲，防止脚本偶发卡帧时固件插一帧进来
    luaResumeUntil = now + 150;
  }
  if (now < luaResumeUntil) {
    lastMs = now;
    return true;
  }

  // 主界面启动前就接管：配置一读到就开始播开机场景

  if (!cfgLoaded && (now - lastCfgTry) > 500) {
    lastCfgTry = now;
    loadCfg();
  }
  // 配置没读到时不要用默认值接管灯带，交给原来的上电/充电灯效
  if (!cfgLoaded) {
    lastMs = now;
    return false;
  }

  // 第一次读到配置即开始开机场景
  if (bootPending) {
    bootPending = false;
    bootUntil = now + 3000;
  }

  if (!fxEnabled) {
    lastMs = now;
    return false;
  }
  if (lastMs == 0) lastMs = now;
  uint32_t dt = now - lastMs;
  if (dt > 200) dt = 200;
  lastMs = now;
  phaseMs += dt;

  uint8_t sc = pickScene(now);

  // 开机场景切换到后续场景时做 600ms 交叉淡出
  if (lastScene == SC_BOOT && sc != SC_BOOT) {
    fadeScene = SC_BOOT;
    fadeStart = now;
    fadeK = 0;
  }
  lastScene = sc;
  if (fadeScene != 0xFF) {
    uint32_t e = now - fadeStart;
    if (e >= fadeMs) fadeScene = 0xFF;
    else fadeK = (uint8_t)(e * 255 / fadeMs);
  }

  render(sc);
  return true;
}

void ledSceneEventAlarm()
{
  alarmUntil = timersGetMsTick() + 2000;
}

void ledSceneOnLuaApply()
{
  luaOwnUntil = timersGetMsTick() + 500;
}
