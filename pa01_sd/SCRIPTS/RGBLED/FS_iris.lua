local function init()
    colorChangeTime = getTime()  -- 初始化时间
    phase = 0
end

local minBrightness = 0   -- 最小亮度
local maxBrightness = 255 -- 最大亮度

local function getColor(phase, length)
    local position = (phase % length) / length
    local r, g, b = maxBrightness, 0, 0 -- 固定红色通道为最大值

    -- 颜色过渡逻辑：红 → 紫 → 粉 → 红
    if position < 0.5 then
        -- 红到紫：蓝色从0升到255
        b = maxBrightness * (position * 2)
    else
        -- 紫到粉到红：蓝色降到0，绿色短暂出现
        local t = (position - 0.5) * 2
        b = maxBrightness * (1 - t)         -- 蓝色从255降到0
        g = maxBrightness * t * 0.6         -- 绿色短暂升到153（制造粉色效果）
    end

    return r, g, b
end

local colorChangeTime = 0 -- 上次颜色变化时间

local function run()
    if (getTime() - colorChangeTime) > 2 then -- 每2个时间单位更新一次
        colorChangeTime = getTime()
        phase = (phase + 2) % 255 -- 调整相位变化速度

        -- 为每个LED设置颜色
        for i = 0, LED_STRIP_LENGTH - 1 do
            -- 每个LED增加相位偏移，产生波浪效果
            local r, g, b = getColor(phase + i * 32, 255)
            setRGBLedColor(i, r, g, b)
        end
        applyRGBLedColors()
    end
end

local function background()
    -- 开关关闭时的后台处理（保持为空）
end

return { run=run, background=background, init=init }