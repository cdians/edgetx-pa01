-- Game-Snake 2.0 for EdgeTX
-- Author: FAN

local snake = {}
local food = {x = 0, y = 0}
local dir = "right"
local nextDir = "right"
local gridSize = math.floor(LCD_W / 30)  -- 缩小网格尺寸
local score = 0
local gameActive = true
local lastUpdate = 0
local moveDelay = 50  -- 初始移动间隔（毫秒）
local generateFood

local function init()
    snake = {
        {x = 5, y = 5},
        {x = 4, y = 5},
        {x = 3, y = 5}
    }
    score = 0
    gameActive = true
    generateFood()
    lastUpdate = getTime()  -- 获取当前时间
end

function generateFood()
    local maxX = math.floor(LCD_W / gridSize) - 1
    local maxY = math.floor(LCD_H / gridSize) - 1
    
    repeat
        food.x = math.random(0, maxX)
        food.y = math.random(0, maxY)
        local valid = true
        
        for _, seg in ipairs(snake) do
            if seg.x == food.x and seg.y == food.y then
                valid = false
                break
            end
        end
    until valid
end

-- 改进的四通道控制
local function processInput()
    -- 通道映射：Aileron（副翼）、Elev（升降）、Thr（油门）、Rud（方向）
    local aileron = getValue("ail")  -- 左右控制
    local elev = getValue("ele")    -- 上下控制
    
    -- 带死区的方向判断（约±10%死区）
    if elev < -300 then        -- 上
        nextDir = (dir ~= "down") and "up" or dir
    elseif elev > 300 then     -- 下
        nextDir = (dir ~= "up") and "down" or dir
    elseif aileron < -300 then -- 左
        nextDir = (dir ~= "right") and "left" or dir
    elseif aileron > 300 then  -- 右
        nextDir = (dir ~= "left") and "right" or dir
    end
end

local function updateGame()
    if not gameActive then return end
    
    -- 控制游戏节奏
    local now = getTime()
    if now - lastUpdate < moveDelay then return end
    lastUpdate = now

    -- 更新方向
    dir = nextDir

    -- 计算新头部位置
    local newHead = {x = snake[1].x, y = snake[1].y}
    if dir == "up" then
        newHead.y = newHead.y + 1
    elseif dir == "down" then
        newHead.y = newHead.y - 1
    elseif dir == "left" then
        newHead.x = newHead.x - 1
    elseif dir == "right" then
        newHead.x = newHead.x + 1
    end

    -- 移除边界限制，添加循环边界处理
    local maxX = math.floor(LCD_W / gridSize)
    local maxY = math.floor(LCD_H / gridSize)
    newHead.x = newHead.x % maxX
    newHead.y = newHead.y % maxY

    -- 自碰撞检测
    if gameActive == true then
        for _, seg in ipairs(snake) do
            if newHead.x == seg.x and newHead.y == seg.y then
                gameActive = false
                return
            end
        end
    end

    -- 移动蛇身
    table.insert(snake, 1, newHead)
    
    -- 食物检测
    if newHead.x == food.x and newHead.y == food.y then
        score = score + 1
        moveDelay = moveDelay * 0.90  -- 每次加速10%
        generateFood()
    else
        table.remove(snake)
    end
end

local function drawGame()
    lcd.clear()
    
    -- 绘制带圆角的蛇身
    for i, seg in ipairs(snake) do
        local size = gridSize - 2
        lcd.drawFilledRectangle(
            seg.x * gridSize + 1,
            seg.y * gridSize + 1,
            size, size,
            (i == 1) and ROUND or 0  -- 蛇头显示圆角
            )
    end
    -- 绘制食物
    lcd.drawFilledRectangle(
    food.x * gridSize + 2,
    food.y * gridSize + 2,
    gridSize - 4,
    gridSize - 4,
    GREY
    )


    -- 绘制游戏状态
    lcd.drawText(5, 5, "Score:"..score, SMLSIZE)
    
    if not gameActive then
        lcd.drawFilledRectangle(30, LCD_H/2-10, LCD_W-40, 40, GREY)
        lcd.drawText(LCD_W/2, LCD_H/2-10, "GAME OVER", CENTER + BLINK)
        lcd.drawText(LCD_W/2, LCD_H/2+10, "Press [Enter] to restart", CENTER + SMLSIZE)
    end
    
    lcd.refresh()
end

local function handleEvent(event)
    if event == EVT_ENTER_BREAK then
        return "reset"
    end
end

local function run(event)
    processInput()
    updateGame()
    drawGame()
    
    -- 通过按键检测重启
    if not gameActive and handleEvent(event) == "reset" then
        init()
        moveDelay = 50  -- 重置速度
    end
    return 0
end

return {init=init, run=run}