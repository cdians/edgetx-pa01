-- TNS|LED 灯效|TNE
-- PA01 LED 场景灯效工具
-- 写入 /RADIO/ledfx.cfg，固件开机时读取并自行渲染

local W, H = LCD_W, LCD_H
local COL1, COL2 = 8, W - 8
local CFG_PATH = "/RADIO/ledfx.cfg"
local NLED = 14

local C = {
  bg    = lcd.RGB(18, 21, 27),
  card  = lcd.RGB(38, 44, 56),
  hi    = lcd.RGB(52, 60, 76),
  txt   = lcd.RGB(232, 236, 242),
  muted = lcd.RGB(122, 130, 150),
  acc   = lcd.RGB(255, 95, 162),
  bar   = lcd.RGB(28, 33, 43),
}

-- 效果：0 关闭 1 静态 2 脉动 3 光谱 4 流动 5 循环 6 警灯 7 油门条(旧) 8 星点 9 交替 10 光谱脉动
local FX_NAME = { "关闭", "静态", "脉动", "光谱", "流动", "循环", "警灯", "油门条",
                  "星点", "交替", "光谱脉动" }
-- 网格里可选的效果：油门条已移到「油门」事件，不再列出
local FX_LIST = { 1, 2, 3, 4, 5, 6, 8, 9, 10 }
-- 电源键（灯 8-9）只支持 静态 / 脉动
local PW_FX = { 1, 2 }
-- 各效果用到的颜色组数量（索引 = fx + 1）
local FX_SLOTS = { 0, 1, 1, 0, 0, 2, 2, 1, 2, 2, 0 }
local FX_ICON = {
  {110, 116, 128}, {255, 70, 70}, {255, 120, 80}, nil,
  {180, 110, 255}, {80, 220, 140}, {90, 120, 255}, {250, 200, 70},
  {235, 238, 245}, {40, 180, 140}, nil,
}

local PRESETS = {
  {255, 60, 90}, {255, 140, 60}, {255, 95, 162},
  {180, 110, 255}, {90, 120, 255}, {80, 220, 150},
}

local COLOR_PRESETS = {
  {255, 60, 60}, {255, 140, 40}, {255, 200, 60}, {255, 95, 162},
  {180, 110, 255}, {90, 120, 255}, {80, 220, 150}, {235, 238, 245},
}

-- 场景顺序与默认值（和固件一致）；trig = 触发绑定，0 = 任意
local SCENES = {
  { key = "charge", name = "充电中", fx = 2, speed = 3, bright = 60, r = 90, g = 120, b = 255, on = true, trig = 0 },
  { key = "full",   name = "已充满", fx = 1, speed = 0, bright = 80, r = 78, g = 200, b = 130, on = true, trig = 0 },
  { key = "low",    name = "低电",   fx = 6, speed = 2, bright = 70, r = 230, g = 70, b = 70, on = true, trig = 0 },
  { key = "crit",   name = "告急",   fx = 1, speed = 0, bright = 90, r = 230, g = 70, b = 70, on = true, trig = 0 },
  { key = "boot",   name = "开机",   fx = 5, speed = 5, bright = 80, r = 80, g = 220, b = 140, on = true, trig = 0 },
  { key = "idle",   name = "待机",   fx = 2, speed = 3, bright = 80, r = 255, g = 95, b = 162, on = true, trig = 0 },
  { key = "thr",    name = "油门",   fx = 1, speed = 0, bright = 90, r = 255, g = 170, b = 60, on = false, trig = 30 },
  { key = "alarm",  name = "报警",   fx = 6, speed = 2, bright = 90, r = 230, g = 70, b = 70, on = true, trig = 0 },
  { key = "pw",     name = "电源键", fx = 2, speed = 3, bright = 40, r = 255, g = 95, b = 162, on = true, trig = 0 },
}
-- 警灯 / 循环的第二组颜色；其余场景与第一组相同
SCENES[3].r2, SCENES[3].g2, SCENES[3].b2 = 60, 90, 255
SCENES[5].r2, SCENES[5].g2, SCENES[5].b2 = 40, 70, 210
SCENES[8].r2, SCENES[8].g2, SCENES[8].b2 = 60, 90, 255
for _, s in ipairs(SCENES) do
  if not s.r2 then s.r2, s.g2, s.b2 = s.r, s.g, s.b end
end

local function hasTrig(s)
  return s.key == "thr"
end

local function trigName(s)
  return (s.trig or 0) .. "%"
end

local function colorSlots(s)
  return FX_SLOTS[(s.fx or 0) + 1] or 0
end

local function fxListFor(s)
  return (s.key == "pw") and PW_FX or FX_LIST
end

local function getColor(s, slot)
  if slot == 2 then return s.r2 or s.r, s.g2 or s.g, s.b2 or s.b end
  return s.r, s.g, s.b
end

local function setColor(s, slot, r, g, b)
  if slot == 2 then s.r2, s.g2, s.b2 = r, g, b
  else s.r, s.g, s.b = r, g, b end
end

local gEventDur = 10   -- x100ms
local gMask = 3        -- bit0 功能开关灯 bit1 电源键灯
local gEnabled = true

-- 出厂默认值快照（用于「恢复默认」）
local DEFAULTS = {}
local function snapshotDefaults()
  for i, s in ipairs(SCENES) do
    DEFAULTS[i] = { fx = s.fx, speed = s.speed, bright = s.bright,
                    r = s.r, g = s.g, b = s.b,
                    r2 = s.r2, g2 = s.g2, b2 = s.b2,
                    on = s.on, trig = s.trig }
  end
end
local function restoreDefault(i)
  local d, s = DEFAULTS[i], SCENES[i]
  if not d or not s then return end
  s.fx, s.speed, s.bright, s.r, s.g, s.b, s.r2, s.g2, s.b2, s.on, s.trig =
    d.fx, d.speed, d.bright, d.r, d.g, d.b, d.r2, d.g2, d.b2, d.on, d.trig
end

local screen = 1       -- 1 列表 / 2 编辑 / 3 颜色
local sceneIdx = 6     -- 默认待机
local listCursor = 6   -- 1..10 场景，11 全部应用，12 保存并退出
local cursor = 1
local editMode = false
local colorCursor = 1     -- 1..8 预设，9..11 R/G/B，12 取消，13 应用
local colorEdit = false
local colorSlot = 1       -- 当前编辑的颜色组 1/2
local colorBackup = { 255, 255, 255 }
local savedTip = 0

-- ---------------------------------------------------------------- 配置

local function findScene(key)
  for i, s in ipairs(SCENES) do
    if s.key == key then return s end
  end
end

local function parseLine(line)
  local key, vals = string.match(line, "^([%a]+)=(.+)$")
  if not key then return end
  local v = {}
  for n in string.gmatch(vals, "%-?%d+") do v[#v + 1] = tonumber(n) end
  if key == "global" then
    gEventDur = v[1] or gEventDur
    gMask = v[2] or gMask
    gEnabled = (v[3] == nil) or (v[3] ~= 0)
    return
  end
  local s = findScene(key)
  if s and #v >= 7 then
    s.fx, s.speed, s.bright, s.r, s.g, s.b =
      v[1], v[2], v[3], v[4], v[5], v[6]
    s.on = v[7] ~= 0
    s.trig = v[8] or 0
    if #v >= 11 then
      s.r2, s.g2, s.b2 = v[9], v[10], v[11]
    else
      s.r2, s.g2, s.b2 = s.r, s.g, s.b
    end
    -- 油门条已并入「油门」事件，旧配置回落到静态
    if s.fx == 7 then s.fx = 1 end
    -- 「关闭」效果已移除，统一用「启用」开关控制
    if s.fx == 0 then s.fx = 1 end
  end
end

local function loadCfg()
  local f = io.open(CFG_PATH, "r")
  if not f then return end
  local txt = io.read(f, 1024)
  io.close(f)
  if type(txt) ~= "string" then return end
  for line in string.gmatch(txt, "[^\r\n]+") do parseLine(line) end
end

local function saveCfg()
  local f = io.open(CFG_PATH, "w")
  if not f then return false end
  io.write(f, string.format("global=%d,%d,%d\n", gEventDur, gMask, gEnabled and 1 or 0))
  for _, s in ipairs(SCENES) do
    io.write(f, string.format("%s=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", s.key,
      s.fx, s.speed, s.bright, s.r, s.g, s.b, s.on and 1 or 0, s.trig or 0,
      s.r2 or s.r, s.g2 or s.g, s.b2 or s.b))
  end
  io.close(f)
  return true
end

-- ---------------------------------------------------------------- 效果

local function hsv2rgb(h, s, v)
  h = h % 360
  local region = math.floor(h / 60)
  local rem = (h % 60) * 255 / 60
  local p = v * (255 - s) / 255
  local q = v * (255 - s * rem / 255) / 255
  local t = v * (255 - s * (255 - rem) / 255) / 255
  local r, g, b
  if region == 0 then r, g, b = v, t, p
  elseif region == 1 then r, g, b = q, v, p
  elseif region == 2 then r, g, b = p, v, t
  elseif region == 3 then r, g, b = p, q, v
  elseif region == 4 then r, g, b = t, p, v
  else r, g, b = v, p, q end
  return r, g, b
end

-- 左组：功能键 3/4（0..3）+ 环绕左（12..13）
local function isLeftLed(i)
  return i <= 3 or i >= 12
end

-- 速度档位 → 倍率（低档更慢）
local SPD = { 0.4, 0.55, 0.75, 1.0, 1.35, 1.8, 2.4, 3.2, 4.2, 5.5, 7.0 }

local function fxColor(s, i, t)
  local sp = SPD[(s.speed or 0) + 1] or 1
  local power = (i == 8 or i == 9)
  local idx = power and (i - 8) or ((i < 8) and i or (i - 2))
  local count = power and 2 or 12
  local r, g, b = 0, 0, 0
  local a1, a2, a3 = s.r, s.g, s.b
  local b1, b2, b3 = s.r2 or s.r, s.g2 or s.g, s.b2 or s.b

  if s.fx == 1 then
    r, g, b = a1, a2, a3
  elseif s.fx == 2 then
    local period = 3600 / sp
    local half = period / 2
    local p = t % period
    local k = (p < half) and (p * 255 / half) or ((period - p) * 255 / half)
    r, g, b = a1 * k / 255, a2 * k / 255, a3 * k / 255
  elseif s.fx == 3 then
    r, g, b = hsv2rgb(math.floor(t * sp / 12) % 360, 255, 255)
  elseif s.fx == 4 then
    r, g, b = hsv2rgb((math.floor(t * sp / 12) + i * 24) % 360, 255, 255)
  elseif s.fx == 5 then   -- 循环：色1 亮点，色2 底色
    local pos = math.floor(t / (120 / sp)) % count
    if idx == pos then r, g, b = a1, a2, a3
    else r, g, b = b1 / 5, b2 / 5, b3 / 5 end
  elseif s.fx == 6 then   -- 警灯：奇偶灯交替 色1/色2
    local ph = math.floor(t / (500 / sp)) % 2
    local odd = idx % 2
    if (ph == 1) ~= (odd == 1) then r, g, b = a1, a2, a3
    else r, g, b = b1, b2, b3 end
  elseif s.fx == 8 then   -- 星点：色1 底色，色2 星点
    r, g, b = a1, a2, a3
    local d1 = (t * 0.072 * sp) % 360
    local d2 = (t * 0.048 * sp + 180) % 360
    local function bounce(deg)
      local v = (math.sin(math.rad(deg)) + 1) * 0.5
      return math.floor(v * (count - 1) + 0.5)
    end
    if idx == bounce(d1) then r, g, b = b1, b2, b3
    elseif idx == bounce(d2) then r, g, b = b1 * 0.6, b2 * 0.6, b3 * 0.6 end
  elseif s.fx == 9 then   -- 交替：左组 / 右组 定时互换
    local ph = math.floor(t / (1000 / sp)) % 2
    if isLeftLed(i) == (ph == 0) then r, g, b = a1, a2, a3
    else r, g, b = b1, b2, b3 end
  elseif s.fx == 10 then  -- 彩虹呼吸：色相旋转 + 亮度起伏
    local period = 3600 / sp
    local half = period / 2
    local p = t % period
    local k2 = (p < half) and (p * 255 / half) or ((period - p) * 255 / half)
    local rr, gg, bb = hsv2rgb(math.floor(t * sp / 12) % 360, 255, 255)
    r, g, b = rr * k2 / 255, gg * k2 / 255, bb * k2 / 255
  end
  local br = s.bright / 100
  return r * br, g * br, b * br
end

local function previewLeds(s)
  if not setRGBLedColor then return end
  local t = getTime() * 10
  local pw = findScene("pw")
  local base = (s.key == "pw") and findScene("idle") or s
  for i = 0, NLED - 1 do
    local sc, on
    if i == 8 or i == 9 then
      sc = (s.key == "pw") and s or pw
      on = gMask >= 2
    else
      sc = base
      on = (gMask % 2) == 1
    end
    if on and sc then
      local r, g, b = fxColor(sc, i, t)
      setRGBLedColor(i, math.floor(r), math.floor(g), math.floor(b))
    end
  end
  applyRGBLedColors()
end

-- ---------------------------------------------------------------- 绘制

local function headerText(left, right)
  lcd.drawFilledRectangle(0, 0, W, 20, C.bar)
  lcd.drawText(COL1, 2, left, C.txt + BOLD)
  if right then
    lcd.drawText(COL2, 4, right, C.muted + RIGHT + SMLSIZE)
  end
  lcd.drawFilledRectangle(0, 19, W, 1, C.acc)
end

local function swatch(r, g, b, x, y, w, h, radius)
  lcd.drawFilledRectangle(x, y, w, h,
    lcd.RGB(math.floor(r), math.floor(g), math.floor(b)))
end

local function drawPreview(s, y)
  lcd.drawFilledRectangle(COL1, y, W - 16, 26, C.card)
  lcd.drawText(COL1 + 6, y + 7, FX_NAME[s.fx + 1], C.muted + SMLSIZE)
  local size, gap = 10, 5
  local total = NLED * size + (NLED - 1) * gap
  local x = W / 2 - total / 2 + 6
  local t = getTime() * 10
  for i = 0, NLED - 1 do
    local r, g, b = fxColor(s, i, t)
    if s.fx == 0 then r, g, b = 38, 44, 56 end
    swatch(r, g, b, x, y + 8, size, size)
    x = x + size + gap
  end
end

-- 场景列表
local function drawList()
  local cur = (listCursor <= #SCENES) and listCursor or sceneIdx
  local s = SCENES[cur]
  headerText("灯效设置", s.name .. "  " .. FX_NAME[s.fx + 1])
  drawPreview(s, 24)

  for i, sc in ipairs(SCENES) do
    local cx = COL1 + ((i - 1) % 2) * 156
    local cy = 54 + math.floor((i - 1) / 2) * 24
    local active = (i == cur) and (listCursor <= #SCENES)
    lcd.drawFilledRectangle(cx, cy, 148, 22, active and C.hi or C.card)
    if active then lcd.drawFilledRectangle(cx, cy + 3, 2, 16, C.acc) end
    swatch(sc.r, sc.g, sc.b, cx + 9, cy + 7, 8, 8)
    lcd.drawText(cx + 21, cy + 4, sc.name, (active and C.txt + BOLD) or C.txt)
    lcd.drawText(cx + 126, cy + 7, FX_NAME[sc.fx + 1],
      (sc.on and C.muted or C.hi) + RIGHT + SMLSIZE)
    if not sc.on then
      lcd.drawText(cx + 142, cy + 7, "关", C.muted + RIGHT + SMLSIZE)
    else
      lcd.drawText(cx + 142, cy + 4, ">", C.muted + RIGHT + BOLD)
    end
  end

  lcd.drawText(12, 200, "▲▼ 选择 · ENTER 编辑灯效", C.muted + SMLSIZE)
  lcd.drawFilledRectangle(COL1, 212, 148, 22, C.card)
  lcd.drawText(44, 214, "全部应用", C.txt + BOLD)
  lcd.drawFilledRectangle(164, 212, 148, 22, C.acc)
  lcd.drawText(208, 214, "保存并退出", C.bg + BOLD)
  if listCursor == #SCENES + 1 then lcd.drawRectangle(COL1 - 1, 211, 150, 24, C.txt) end
  if listCursor == #SCENES + 2 then lcd.drawRectangle(163, 211, 150, 24, C.txt) end
  if savedTip > 0 then
    lcd.drawText(W / 2, 200, "已保存", C.acc + CENTER + SMLSIZE)
  end
end

-- 编辑页可导航项（光标 1 起）
local function editRows(s)
  local fxList = fxListFor(s)
  local slots = colorSlots(s)
  local items = {}
  local R = { fxList = fxList, slots = slots }
  for k = 1, #fxList do items[#items + 1] = { t = "fx", id = fxList[k] } end
  if hasTrig(s) then items[#items + 1] = { t = "trig" } end
  items[#items + 1] = { t = "speed" }
  items[#items + 1] = { t = "bright" }
  if slots == 2 then items[#items + 1] = { t = "slot" } end
  if slots >= 1 then
    for k = 1, 6 do items[#items + 1] = { t = "preset", v = k } end
    items[#items + 1] = { t = "custom" }
  end
  items[#items + 1] = { t = "on" }
  items[#items + 1] = { t = "default" }
  items[#items + 1] = { t = "save" }
  for i, it in ipairs(items) do
    if it.t == "trig" then R.trig = i
    elseif it.t == "speed" then R.speed = i
    elseif it.t == "bright" then R.bright = i
    elseif it.t == "slot" then R.slot = i
    elseif it.t == "preset" and not R.preset then R.preset = i
    elseif it.t == "custom" then R.custom = i
    elseif it.t == "on" then R.on = i
    elseif it.t == "default" then R.default = i
    elseif it.t == "save" then R.save = i end
  end
  R.items = items
  R.count = #items
  return R
end

-- 场景编辑
local function drawEdit()
  local s = SCENES[sceneIdx]
  local R = editRows(s)
  if cursor > R.count then cursor = R.count end
  headerText("< 设置", s.name)

  -- 效果网格（电源键只列出 3 项）
  for k = 1, #R.fxList do
    local id = R.fxList[k]
    local cx = COL1 + ((k - 1) % 3) * 103
    local cy = 26 + math.floor((k - 1) / 3) * 28
    local sel = (cursor == k)
    local cur = (s.fx == id)
    lcd.drawFilledRectangle(cx, cy, 98, 25, cur and C.hi or C.card)
    if sel then lcd.drawRectangle(cx, cy, 98, 25, C.acc) end
    if cur then lcd.drawFilledRectangle(cx, cy + 3, 2, 19, C.acc) end
    local ic = FX_ICON[id + 1]
    if ic then
      swatch(ic[1], ic[2], ic[3], cx + 6, cy + 7, 11, 11)
    else
      local cols = { {255,60,90}, {255,180,60}, {90,210,140}, {90,140,240} }
      for n = 1, 4 do
        local c2 = cols[n]
        swatch(c2[1], c2[2], c2[3], cx + 6 + (n - 1) * 3, cy + 7, 3, 11)
      end
    end
    lcd.drawText(cx + 21, cy + 6, FX_NAME[id + 1], cur and (C.txt + BOLD) or C.txt)
  end

  -- 油门触发阈值
  if R.trig then
    local pos = #R.fxList
    local cx = COL1 + (pos % 3) * 103
    local cy = 26 + math.floor(pos / 3) * 28
    lcd.drawFilledRectangle(cx, cy, 98, 25, C.card)
    if cursor == R.trig then lcd.drawRectangle(cx, cy, 98, 25, C.acc) end
    lcd.drawText(cx + 6, cy + 1, "触发", C.muted + SMLSIZE)
    lcd.drawText(cx + 6, cy + 12, trigName(s) .. " >", C.txt + SMLSIZE)
  end

  -- 参数
  local y = 142
  local function paramRow(idx, name, val, maxv, yy)
    lcd.drawText(16, yy + 2, name, C.muted + SMLSIZE)
    lcd.drawFilledRectangle(52, yy + 5, 96, 6, C.card)
    lcd.drawFilledRectangle(52, yy + 5, 96 * val / maxv, 6, C.acc)
    lcd.drawText(154, yy + 1, tostring(val), C.txt + SMLSIZE)
    if cursor == idx then lcd.drawRectangle(14, yy, 148, 14, C.acc) end
  end
  paramRow(R.speed, "速度", s.speed, 10, y)
  paramRow(R.bright, "亮度", s.bright, 100, y + 16)

  -- 颜色
  local cy2 = 176
  if R.slots == 0 then
    -- 自动配色效果：用动态色带示意
    lcd.drawText(16, cy2 + 2, "自动配色", C.muted + SMLSIZE)
    local t = getTime() * 10
    for k = 0, 9 do
      local r, g, b = hsv2rgb(math.floor(t / 12 + k * 24) % 360, 255, 255)
      swatch(r, g, b, 56 + k * 24, cy2, 20, 14)
    end
  else
    local px0 = 48
    if R.slots == 2 then
      lcd.drawFilledRectangle(16, cy2, 14, 14, colorSlot == 1 and C.acc or C.card)
      lcd.drawText(20, cy2 + 2, "1", colorSlot == 1 and C.bg or C.txt)
      lcd.drawFilledRectangle(33, cy2, 14, 14, colorSlot == 2 and C.acc or C.card)
      lcd.drawText(37, cy2 + 2, "2", colorSlot == 2 and C.bg or C.txt)
      if cursor == R.slot then lcd.drawRectangle(14, cy2 - 2, 36, 18, C.txt) end
      px0 = 58
    else
      lcd.drawText(16, cy2 + 2, "颜色", C.muted + SMLSIZE)
    end
    local cr, cg, cb = getColor(s, colorSlot)
    for k = 1, 6 do
      local px = px0 + (k - 1) * 22
      local p = PRESETS[k]
      swatch(p[1], p[2], p[3], px, cy2, 20, 14)
      if cr == p[1] and cg == p[2] and cb == p[3] then
        lcd.drawRectangle(px - 1, cy2 - 1, 22, 16, C.acc)
      end
      if cursor == R.preset + k - 1 then
        lcd.drawRectangle(px - 2, cy2 - 2, 24, 18, C.acc)
      end
    end
    lcd.drawText(196, cy2 + 2, "自定义 >", C.muted + SMLSIZE)
    if cursor == R.custom then lcd.drawRectangle(192, cy2 - 2, 46, 18, C.acc) end
  end

  -- 按钮
  lcd.drawFilledRectangle(COL1, 212, 96, 22, s.on and C.hi or C.card)
  lcd.drawText(16, 214, "启用: " .. (s.on and "开" or "关"),
    s.on and (C.txt + BOLD) or (C.muted + BOLD))
  lcd.drawFilledRectangle(112, 212, 96, 22, C.card)
  lcd.drawText(138, 214, "恢复默认", C.muted + BOLD)
  lcd.drawFilledRectangle(216, 212, 96, 22, C.acc)
  lcd.drawText(244, 214, "保存", C.bg + BOLD)
  if cursor == R.on then lcd.drawRectangle(COL1 - 1, 211, 98, 24, C.txt) end
  if cursor == R.default then lcd.drawRectangle(111, 211, 98, 24, C.txt) end
  if cursor == R.save then lcd.drawRectangle(215, 211, 98, 24, C.txt) end

  if savedTip > 0 then
    lcd.drawText(W / 2, 196, "已保存", C.acc + CENTER + SMLSIZE)
  elseif editMode then
    lcd.drawText(W / 2, 196, "滚轮调整 · ENTER 完成", C.muted + CENTER + SMLSIZE)
  end
end

-- 自定义颜色
local function drawColor()
  local s = SCENES[sceneIdx]
  local cr, cg, cb = getColor(s, colorSlot)
  headerText("< " .. s.name, (colorSlots(s) == 2) and ("颜色 " .. colorSlot) or "颜色")

  -- 渐变条
  for x = 0, W - 41 do
    local r = 255 * (1 - x / (W - 40))
    local b = 255 * (x / (W - 40))
    lcd.drawFilledRectangle(20 + x, 28, 1, 22, lcd.RGB(r, 70, b))
  end

  for k = 1, 8 do
    local p = COLOR_PRESETS[k]
    local px = 20 + (k - 1) * 36
    swatch(p[1], p[2], p[3], px, 58, 28, 24)
    if cr == p[1] and cg == p[2] and cb == p[3] then
      lcd.drawRectangle(px - 1, 57, 30, 26, C.acc)
    end
    if colorCursor == k then lcd.drawRectangle(px - 2, 56, 32, 28, C.txt) end
  end

  local ch = { { "R", cr }, { "G", cg }, { "B", cb } }
  for i = 1, 3 do
    local y = 96 + (i - 1) * 24
    lcd.drawText(20, y, ch[i][1], C.txt + BOLD)
    lcd.drawFilledRectangle(40, y + 5, 200, 7, C.card)
    lcd.drawFilledRectangle(40, y + 5, 200 * ch[i][2] / 255, 7, C.acc)
    lcd.drawText(252, y, tostring(ch[i][2]), C.txt)
    if colorCursor == 8 + i then
      lcd.drawRectangle(18, y - 2, 240, 16, C.acc)
      if colorEdit then lcd.drawRectangle(16, y - 4, 244, 20, C.txt) end
    end
  end

  lcd.drawFilledRectangle(COL1, 196, 148, 22, C.card)
  lcd.drawText(56, 198, "取消", C.muted + BOLD)
  lcd.drawFilledRectangle(164, 196, 148, 22, C.acc)
  lcd.drawText(214, 198, "应用颜色", C.bg + BOLD)
  if colorCursor == 12 then lcd.drawRectangle(COL1 - 1, 195, 150, 24, C.txt) end
  if colorCursor == 13 then lcd.drawRectangle(163, 195, 150, 24, C.txt) end

  if colorEdit then
    lcd.drawText(W / 2, 176, "滚轮调整 · ENTER 完成", C.muted + CENTER + SMLSIZE)
  end
end

-- ---------------------------------------------------------------- 事件

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local function runEdit(event)
  local s = SCENES[sceneIdx]
  local R = editRows(s)
  local it = R.items[cursor]
  if not it then cursor = 1; it = R.items[1] end

  -- 调整模式：滚轮改数值
  if editMode then
    local d = 0
    if event == EVT_VIRTUAL_NEXT then d = 1
    elseif event == EVT_VIRTUAL_PREV then d = -1
    elseif event == EVT_VIRTUAL_ENTER or event == EVT_VIRTUAL_EXIT then
      editMode = false
      return
    end
    if d ~= 0 then
      if it.t == "trig" then
        s.trig = clamp((s.trig or 30) + 5 * d, 5, 100)
      elseif it.t == "speed" then
        s.speed = clamp(s.speed + d, 0, 10)
      else
        s.bright = clamp(s.bright + 5 * d, 5, 100)
      end
    end
    return
  end

  if event == EVT_VIRTUAL_NEXT then
    cursor = cursor % R.count + 1
  elseif event == EVT_VIRTUAL_PREV then
    cursor = (cursor + R.count - 2) % R.count + 1
  elseif event == EVT_VIRTUAL_ENTER then
    if it.t == "fx" then
      s.fx = it.id
      if colorSlots(s) < 2 then colorSlot = 1 end
    elseif it.t == "trig" or it.t == "speed" or it.t == "bright" then
      editMode = true
    elseif it.t == "slot" then
      colorSlot = (colorSlot == 1) and 2 or 1
    elseif it.t == "preset" then
      local p = PRESETS[it.v]
      setColor(s, colorSlot, p[1], p[2], p[3])
    elseif it.t == "custom" then
      screen = 3
      colorCursor = 1
      colorEdit = false
      local cr, cg, cb = getColor(s, colorSlot)
      colorBackup = { cr, cg, cb }
    elseif it.t == "on" then
      s.on = not s.on
    elseif it.t == "default" then
      restoreDefault(sceneIdx)
      if colorSlots(s) < 2 then colorSlot = 1 end
    elseif it.t == "save" then
      if saveCfg() then savedTip = 40 end
    end
  elseif event == EVT_VIRTUAL_EXIT then
    if editMode then
      editMode = false
    else
      screen = 1
      cursor = 1
    end
  end
end

local function runColor(event)
  local s = SCENES[sceneIdx]
  local cr, cg, cb = getColor(s, colorSlot)

  -- 调整模式：滚轮改当前通道
  if colorEdit then
    local d = 0
    if event == EVT_VIRTUAL_NEXT then d = 5
    elseif event == EVT_VIRTUAL_PREV then d = -5
    elseif event == EVT_VIRTUAL_ENTER or event == EVT_VIRTUAL_EXIT then
      colorEdit = false
      return
    end
    if d ~= 0 then
      local ch = colorCursor - 8
      if ch == 1 then cr = clamp(cr + d, 0, 255)
      elseif ch == 2 then cg = clamp(cg + d, 0, 255)
      else cb = clamp(cb + d, 0, 255) end
      setColor(s, colorSlot, cr, cg, cb)
    end
    return
  end

  if event == EVT_VIRTUAL_NEXT then
    colorCursor = colorCursor % 13 + 1
  elseif event == EVT_VIRTUAL_PREV then
    colorCursor = (colorCursor + 11) % 13 + 1
  elseif event == EVT_VIRTUAL_ENTER then
    if colorCursor <= 8 then
      local p = COLOR_PRESETS[colorCursor]
      setColor(s, colorSlot, p[1], p[2], p[3])
    elseif colorCursor <= 11 then
      colorEdit = true
    elseif colorCursor == 12 then
      setColor(s, colorSlot, colorBackup[1], colorBackup[2], colorBackup[3])
      screen = 2
    else
      screen = 2
    end
  elseif event == EVT_VIRTUAL_EXIT then
    setColor(s, colorSlot, colorBackup[1], colorBackup[2], colorBackup[3])
    screen = 2
  end
end

-- ---------------------------------------------------------------- 主循环

local function init()
  snapshotDefaults()
  loadCfg()
  -- 默认选中待机场景
  for i, s in ipairs(SCENES) do
    if s.key == "idle" then sceneIdx = i end
  end
end

local function run(event)
  if savedTip > 0 then savedTip = savedTip - 1 end

  if screen == 1 then
    if event == EVT_VIRTUAL_EXIT then return 2 end
    local n = #SCENES + 2
    if event == EVT_VIRTUAL_NEXT then
      listCursor = listCursor % n + 1
    elseif event == EVT_VIRTUAL_PREV then
      listCursor = (listCursor + n - 2) % n + 1
    elseif event == EVT_VIRTUAL_ENTER then
      if listCursor <= #SCENES then
        sceneIdx = listCursor
        screen = 2
        cursor = 1
        colorSlot = 1
      else
        if saveCfg() then savedTip = 40 end
        if listCursor == #SCENES + 2 then return 2 end
      end
    end
  elseif screen == 2 then
    runEdit(event)
  else
    runColor(event)
  end

  lcd.clear(C.bg)
  if screen == 1 then
    drawList()
  elseif screen == 2 then
    drawEdit()
  else
    drawColor()
  end

  -- 实时预览：列表页跟随高亮项，其余跟随当前场景
  local ps = sceneIdx
  if screen == 1 and listCursor <= #SCENES then ps = listCursor end
  local s = SCENES[ps]
  previewLeds(s)

  return 0
end

return { init = init, run = run }
