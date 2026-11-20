local function init()
    colorChangeTime = getTime()  -- 初始化时间
    phase = 0
end

local minBrightness = 0   -- 最小亮度
local maxBrightness = 255 -- 最大亮度

local function getColor(phase, length)
    local position = (phase % length) / length
    local r, g, b = 0, 0, 0  -- 固定红色通道为0

    -- 四色渐变逻辑：蓝 → 深蓝 → 绿 → 墨绿 → 蓝
    if position < 0.25 then
        -- 蓝到深蓝：亮度从255降到128
        b = maxBrightness - (maxBrightness - 128) * (position / 0.25)
    elseif position < 0.5 then
        -- 深蓝到绿：蓝色降为0，绿色升到255
        local t = (position - 0.25) / 0.25
        b = 128 - 128 * t
        g = 0 + 255 * t
    elseif position < 0.75 then
        -- 绿到墨绿：亮度从255降到128
        local t = (position - 0.5) / 0.25
        g = maxBrightness - (maxBrightness - 128) * t
    else
        -- 墨绿到蓝：绿色降为0，蓝色升到255
        local t = (position - 0.75) / 0.25
        g = 128 - 128 * t
        b = 0 + 255 * t
    end

    return r, g, b
end

local colorChangeTime = 0 -- 上次颜色变化时间

local function run()
    if (getTime() - colorChangeTime) > 2 then -- 每2个时间单位更新一次
        colorChangeTime = getTime()
        phase = (phase + 3) % 255  -- 调整颜色变化速度

        -- 为每个LED设置颜色
        for i = 0, LED_STRIP_LENGTH - 1 do
            -- 每个LED增加相位偏移，产生波浪效果
            local r, g, b = getColor(phase + i * 40, 255)
            setRGBLedColor(i, r, g, b)
        end
        applyRGBLedColors()
    end
end

local function background()
    -- 开关关闭时的后台处理（保持为空）
end

return { run=run, background=background, init=init }