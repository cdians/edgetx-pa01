local function init()
    colorChangeTime = getTime()
    sparkPhase1 = 0  -- 第一个流光点相位
    sparkPhase2 = 180  -- 第二个流光点相位（错开初始位置）
end

-- 基础白灰色设置 (R240,G240,B240)
local baseColor = {r=240, g=240, b=240} 

-- 动态流光颜色生成器
local function getSparkColor(phase)
    -- 青(0,255,255) 到 粉(255,105,180) 渐变
    local t = (math.sin(math.rad(phase)) + 1) / 2  -- 将相位转换为0-1范围
    
    return {
        r = 255 * t,                -- 红色通道：0 → 255
        g = 255 * (1 - t*0.6),      -- 绿色通道：255 → 102
        b = 255 - 75 * t            -- 蓝色通道：255 → 180
    }
end

-- 获取动态流光位置
local function getSparkPosition(phase, length)
    -- 使用正弦波实现往返流动效果
    return math.floor((math.sin(math.rad(phase)) + 1) * 0.5 * (length - 1) + 0.5)
end

local function run()
    if (getTime() - colorChangeTime) < 0.05 then return end  -- 约20fps
    colorChangeTime = getTime()

    -- 先设置全部为白灰色基底
    for i=0, LED_STRIP_LENGTH-1 do
        setRGBLedColor(i, baseColor.r, baseColor.g, baseColor.b)
    end

    -- 更新两个流光点
    sparkPhase1 = (sparkPhase1 + 3.6) % 360  -- 每周期100步
    sparkPhase2 = (sparkPhase2 + 2.4) % 360  -- 不同步的速度

    -- 第一个流光点（青色主导）
    local pos1 = getSparkPosition(sparkPhase1, LED_STRIP_LENGTH)
    local color1 = getSparkColor(sparkPhase1*1.2)
    setRGBLedColor(pos1, color1.r, color1.g, color1.b)

    -- 第二个流光点（粉色主导）
    local pos2 = getSparkPosition(sparkPhase2, LED_STRIP_LENGTH)
    local color2 = getSparkColor(sparkPhase2*0.8 + 90)
    setRGBLedColor(pos2, color2.r, color2.g, color2.b)

    applyRGBLedColors()
end

local function background()
    
end

return { run=run, background=background, init=init }