local function init()
    colorChangeTime = getTime()
    for i=0, LED_STRIP_LENGTH-1 do
        setRGBLedColor(i, 0, 0, 0)
    end
end
local BREATH_INTERVAL = 45
local PI = 3.14159            -- 圆周率
local BREATH_STEP = (PI * BREATH_INTERVAL) / 3000  -- 角度步长计算
local GAMMA = 7
local COLOR_SPEED       = 0.5   -- 色相每帧移动速度（可调）

-- 呼吸灯状态
local breath_angle = 0.0
local hue_angle    = 0.0          -- 色相角 0-360°

-- HSV → RGB, 输入 h∈[0,360] s,v∈[0,1]  输出 r,g,b∈[0,255]
local function hsv2rgb(h, s, v)
    h = h % 360
    local c = v * s
    local x = c * (1 - math.abs((h / 60) % 2 - 1))
    local m = v - c

    local r, g, b
    if     h <  60 then r, g, b = c,  x,  0
    elseif h < 120 then r, g, b = x,  c,  0
    elseif h < 180 then r, g, b = 0,  c,  x
    elseif h < 240 then r, g, b = 0,  x,  c
    elseif h < 300 then r, g, b = x,  0,  c
    else                 r, g, b = c,  0,  x
    end

    r = math.floor((r + m) * 255 + 0.5)
    g = math.floor((g + m) * 255 + 0.5)
    b = math.floor((b + m) * 255 + 0.5)
    return r, g, b
end

-- LED呼吸亮度计算函数
local function ledBreathBright(angle)
    -- 将sin值映射到[0,1]范围
    local brightness = math.sin(angle) * 0.5 + 0.5
    
    -- Gamma校正
    brightness = brightness ^ GAMMA
    
    -- 转换为0-255整数（四舍五入）
    return math.floor(brightness * 255)  -- 确保返回整数
end

-- 更新呼吸灯状态并获取亮度值
function updateBreathLed()
    -- 计算当前亮度
    local bright = ledBreathBright(breath_angle)
    
    -- 更新呼吸角度
    breath_angle = breath_angle + BREATH_STEP
    
    -- 角度复位（完成一个周期后重置）
    if breath_angle > PI then
        breath_angle = 0
    end
    
    -- return bright

        -- 色相
    hue_angle = (hue_angle + COLOR_SPEED) % 360

    -- 计算当前颜色（饱和度固定 1，亮度由呼吸决定）
    local r, g, b = hsv2rgb(hue_angle, 1.0, bright / 255)

    return r, g, b
end

local function run()
    if (getTime() - colorChangeTime) < 10 / 100 then return end  -- 约50fps
    colorChangeTime = getTime()

    local r, g, b = updateBreathLed()
    for i = 0, LED_STRIP_LENGTH - 1 do
        setRGBLedColor(i, r, g, b)
    end

    applyRGBLedColors()
end

local function background()
    
end

return { run=run, background=background, init=init }