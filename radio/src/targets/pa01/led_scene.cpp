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

#if !defined(SIMU)
  #include "stm32_hal.h"
#endif

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

// 把 boot / pw 场景写入 RTC 备份寄存器，供下次开机早期的原生灯效读取
static void ledBootCfgPersist();

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
  uint8_t gamma;   // 呼吸曲线指数 ×10（22 = 2.2，1=线性 2.2=感知匀速 3.5=暗段更长）
};

static FxCfg cfg[SC_COUNT];
static bool cfgLoaded;
static uint32_t lastCfgTry;
static bool fxEnabled = true;
static uint8_t fxMask = 0x03;
static bool uiStarted;      // 主界面循环已开始
static uint32_t uiStartedAt;  // 主界面首次 tick 的时间
static uint16_t eventDur100ms = 10;

static uint32_t alarmUntil;
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
  cfg[sc].gamma = 22;   // 默认 2.2
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
  // 第 12 列：呼吸曲线指数 ×10；缺省保持默认 2.2
  if (n >= 12) cfg[sc].gamma = (uint8_t)v[11];
}

static void loadCfg()
{
  FIL f;
  if (f_open(&f, LEDFX_FILE, FA_READ) != FR_OK) {
    // No config (SD not mounted yet, card pulled, or file absent): stay
    // unloaded. ledSceneTick() then returns false and ledLoop() keeps the
    // stock power-on / charging / battery indicators alive. Because the tick
    // retries every 500 ms while unloaded, dropping in a card with a config
    // takes effect automatically - no need to give up after N tries.
    // Forcing cfgLoaded=true here was wrong: it made render() take over the
    // strip with an all-zero config and left every LED dark, permanently.
    return;
  }
  static char buf[1024];
  UINT br = 0;
  if (f_read(&f, buf, sizeof(buf) - 1, &br) != FR_OK) {
    f_close(&f);
    return;  // stay unloaded, keep stock LED behaviour, retry next tick
  }
  f_close(&f);
  buf[br] = 0;
  cfgLoaded = true;

  char* p = buf;
  while (*p) {
    char* nl = strchr(p, '\n');
    if (nl) *nl = 0;
    if (*p && *p != '#') parseLine(p);
    if (!nl) break;
    p = nl + 1;
  }

  // 下次开机时，早期 boot 灯效直接从备份寄存器读取这里的配置
  ledBootCfgPersist();
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
      float u = (p < half) ? ((float)p / half) : ((float)(period - p) / half);
      float gamma = c.gamma ? (c.gamma / 10.0f) : 2.2f;
      uint32_t k = (uint32_t)(255.0f * powf(u, gamma));
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
      float u = ((float)thr + 1024.0f) / 2048.0f;
      if (u < 0.0f) u = 0.0f;
      if (u > 1.0f) u = 1.0f;
      float gamma = c.gamma ? (c.gamma / 10.0f) : 2.2f;
      uint32_t v = (uint32_t)(255.0f * powf(u, gamma));
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
      float u = (p < half) ? ((float)p / half) : ((float)(period - p) / half);
      float gamma = c.gamma ? (c.gamma / 10.0f) : 2.2f;
      uint32_t k = (uint32_t)(255.0f * powf(u, gamma));
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
    uint8_t idx = 0, count = 0;

    if (isPowerLed(hw)) {
      if (!(fxMask & 0x02) || !pw.enabled) {
        c = &noneCfg;
      } else {
        c = &pw;
        idx = hw - LED_POWER_FIRST;
        count = pwCount;
      }
    } else {
      if (!(fxMask & 0x01)) {
        c = &noneCfg;
      } else {
        c = &act;
        idx = seq++;
        count = mainCount;
      }
    }

    uint8_t r, g, b;
    ledColorOf(*c, hw, idx, count, t, &r, &g, &b);
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

// 当前应显示的场景。开机灯效由固件原生逻辑播放，不在这里重播 boot。
static uint8_t pickScene()
{
  if (cfg[SC_CRIT].enabled && batteryCritical()) return SC_CRIT;
  if (cfg[SC_LOW].enabled && IS_TXBATT_WARNING()) return SC_LOW;
  if (cfg[SC_ALARM].enabled && timersGetMsTick() < alarmUntil) return SC_ALARM;

  uint16_t cs = get_battery_charge_state();
  if (cfg[SC_CHARGE].enabled && cs == CHARGE_STARTED) return SC_CHARGE;
  if (cfg[SC_FULL].enabled && cs == CHARGE_FINISHED) return SC_FULL;

  if (cfg[SC_THR].enabled && cfg[SC_THR].trig > 0 &&
      throttlePercent() >= (int32_t)cfg[SC_THR].trig)
    return SC_THR;

  return cfg[SC_IDLE].enabled ? SC_IDLE : SC_NONE;
}

// ---------------------------------------------------------------- RTC-backed boot config

// boot / pw 各占 4 个备份寄存器：magic+crc / 12 字节场景参数。
// 配置变化时由 loadCfg() 写入；下次开机早期由 rgbPowerOn() 读取，
// 从而真正替换固化在代码里的默认 boot 灯效（不是开机后再补播）。
#define LED_BOOT_MAGIC   0x4C44u   // "LD"
#define LED_BOOT_BASE    RTC_BKP_DR4
#define LED_PW_BASE      RTC_BKP_DR8

static uint16_t bootCrc16(const uint8_t* p, int n)
{
  uint16_t c = 0xFFFF;
  while (n--) {
    c ^= *p++;
    for (int i = 0; i < 8; i++)
      c = (c & 1) ? (uint16_t)((c >> 1) ^ 0xA001) : (uint16_t)(c >> 1);
  }
  return c;
}

static void ledBootSceneStore(uint32_t base, const FxCfg& c)
{
#if !defined(SIMU)
  uint8_t b[12] = {
    c.fx, c.speed, c.bright, c.r, c.g, c.b, c.enabled,
    c.r2, c.g2, c.b2, c.gamma, 0
  };
  uint32_t w[4];
  w[0] = LED_BOOT_MAGIC | ((uint32_t)bootCrc16(b, sizeof(b)) << 16);
  memcpy(&w[1], b, sizeof(b));

  RTC_HandleTypeDef h = {};
  h.Instance = RTC;
  HAL_PWR_EnableBkUpAccess();
  HAL_PWREx_EnableBkUpReg();
  for (int i = 0; i < 4; i++)
    HAL_RTCEx_BKUPWrite(&h, base + i, w[i]);
#else
  (void)base; (void)c;
#endif
}

static bool ledBootSceneLoad(uint32_t base, FxCfg* c)
{
#if !defined(SIMU)
  RTC_HandleTypeDef h = {};
  h.Instance = RTC;
  HAL_PWR_EnableBkUpAccess();
  HAL_PWREx_EnableBkUpReg();

  uint32_t w[4];
  for (int i = 0; i < 4; i++)
    w[i] = HAL_RTCEx_BKUPRead(&h, base + i);
  if ((w[0] & 0xFFFFu) != LED_BOOT_MAGIC)
    return false;

  uint8_t b[12];
  memcpy(b, &w[1], sizeof(b));
  if (((w[0] >> 16) & 0xFFFFu) != bootCrc16(b, sizeof(b)))
    return false;

  memset(c, 0, sizeof(*c));
  c->fx = b[0]; c->speed = b[1]; c->bright = b[2];
  c->r = b[3]; c->g = b[4]; c->b = b[5];
  c->enabled = b[6];
  c->r2 = b[7]; c->g2 = b[8]; c->b2 = b[9];
  c->gamma = b[10] ? b[10] : 22;   // 旧备份（gamma=0）回落 2.2
  return true;
#else
  (void)base; (void)c;
  return false;
#endif
}

static void ledBootCfgPersist()
{
  ledBootSceneStore(LED_BOOT_BASE, cfg[SC_BOOT]);
  ledBootSceneStore(LED_PW_BASE, cfg[SC_POWER]);
}

// 开机早期由 rgbPowerOn() 调用：用备份寄存器里的 boot 配置直接渲染一帧。
// 返回 false 表示没有有效配置，调用方继续使用原来的固化灯效。
bool ledBootEarlyFrame(uint32_t t)
{
  FxCfg boot;
  if (!ledBootSceneLoad(LED_BOOT_BASE, &boot) || !boot.enabled)
    return false;

  FxCfg pw;
  if (!ledBootSceneLoad(LED_PW_BASE, &pw))
    pw = boot;

  uint8_t seq = 0;
  const uint8_t pwCount = LED_POWER_LAST - LED_POWER_FIRST + 1;
  const uint8_t mainCount = LEDFX_LEDS - pwCount;

  for (uint8_t hw = 0; hw < LEDFX_LEDS; hw++) {
    const FxCfg* c;
    uint8_t idx, count;
    if (isPowerLed(hw)) {
      c = &pw;
      idx = hw - LED_POWER_FIRST;
      count = pwCount;
    } else {
      c = &boot;
      idx = seq++;
      count = mainCount;
    }

    uint8_t r, g, b;
    ledColorOf(*c, hw, idx, count, t, &r, &g, &b);
    rgbSetLedColor(hw, r, g, b);
  }
  rgbLedColorApply();
  return true;
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
    uiStartedAt = timersGetMsTick();
    if (!cfgLoaded) loadCfg();
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
    // 脚本刚让出灯带时留一小段缓冲，防止脚本偶发卡帧时固件插一帧进来
    luaResumeUntil = now + 150;
  }
  if (now < luaResumeUntil) {
    lastMs = now;
    return true;
  }

  if (!cfgLoaded && (now - lastCfgTry) > 500) {
    lastCfgTry = now;
    loadCfg();
  }
  // 配置没读到时不要用默认值接管灯带，交给原来的上电/充电灯效
  if (!cfgLoaded) {
    lastMs = now;
    return false;
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

  render(pickScene());
  return true;
}

#define ALARM_BOOT_GRACE_MS  8000

void ledSceneEventAlarm()
{
  uint32_t now = timersGetMsTick();

  // 开机阶段屏蔽告警灯效：刚进主界面时开关若不在初始位置会立即弹出
  // 开关告警，其提示音会触发这里，导致 boot 之后插播一段告急灯效。
  // uiStarted 未置位（splash 阶段）或进入主界面后的宽限期内都忽略，
  // 让开机灯效序列（boot -> idle）完整播放。宽限期后正常响应告警。
  if (!uiStarted || (now - uiStartedAt) < ALARM_BOOT_GRACE_MS)
    return;

  alarmUntil = now + 2000;
}

void ledSceneOnLuaApply()
{
  luaOwnUntil = timersGetMsTick() + 500;
}
