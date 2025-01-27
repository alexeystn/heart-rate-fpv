local heartRate = 0
local nextHRUpdateTime = 0
local nextStatsUpdateTime = 0
local currentTime = 0

local ST_NO_ESP32 = -1
local ST_IDLE = 1
local ST_AVAILABLE = 2
local ST_CONNECTED = 3
-- 0 is reserved

local status = ST_NO_ESP32
local lastSerialRxTime = 0

local armSwitch = 'sd'

local arms = {}
local stats = {}
local statsPointer = 1
local statsSize = 128 - 17 - 2
local maxHeartRate = 0

local log = {}
local logPointer = 1
local logSize = 9

local function putToStats(bpm)
  stats[statsPointer] = bpm
  arms[statsPointer] = getValue(armSwitch) > 0
  if statsPointer < #stats then
    statsPointer = statsPointer + 1
  else
    statsPointer = 1
  end
end

local function putToLog(str)
  log[logPointer] = str
  logPointer = logPointer + 1
  if logPointer > logSize then
    logPointer = 1
  end
end

local avgSum = 0
local avgCount = 0
local avgInterval = 2.15 --seconds

local function putToAvgBuffer(bpm)
  if bpm > 0 then
    avgSum = avgSum + bpm
    avgCount = avgCount + 1
    if bpm > maxHeartRate then
      maxHeartRate = bpm
    end
  end
  if currentTime < nextStatsUpdateTime then
    return
  end
  nextStatsUpdateTime = currentTime + avgInterval * 100
  if avgCount > 0 then
    bpmAvg = avgSum / avgCount
  else
    bpmAvg = 0
  end
  putToStats(bpmAvg)
  avgSum = 0
  avgCount = 0
end

local function parseMessage(str)
  local ret = nil
  if string.sub(str, 1, 2) == "H:" then
    ret = tonumber(string.sub(str, 3, -1))
  end
  return ret
end

local rxLine = ""
local ch = 0

local function processRxData(str)
  for i = 1,string.len(str) do
    ch = string.sub(str, i, i)
    rxLine = rxLine .. ch
    if ch == '\n' then
      putToLog(rxLine)
      ret = parseMessage(rxLine)
      rxLine = ""
    end
    if string.len(rxLine) > 50 then
      rxLine = ""
    end
  end
  return ret
end


local function init_func()
  for i = 1,statsSize do
    stats[i] = 0
    arms[i] = false
  end
  for i = 1,logSize do
    log[i] = '***'
  end
  setSerialBaudrate(115200)
end

local function bpmToY(bpm)
  return 64 - (bpm - 60) * 3 / 5 - 4
end

local function cntToPnt(i)
  local res = statsPointer + i - 1
  if res > statsSize then
    return (res - statsSize)
  else
    return res
  end
end

local displayMode = 1
local blinkCounter = 0

local function drawDisplay()
  lcd.clear()
  if displayMode == 1 then -- Single number
    lcd.drawFilledRectangle(0, 0, LCD_W, 10)
    
    if status == ST_NO_ESP32 then 
      lcd.drawText(10, 1, "ESP32 not connected", INVERS)
    elseif status == ST_IDLE then
      lcd.drawText(19, 1, "Sensor not found", INVERS)
    elseif status == ST_AVAILABLE then
      lcd.drawText(22, 1, "Sensor available", INVERS)
    else
      lcd.drawText(34, 1, "Heart rate", INVERS)
    end
    
    if heartRate > 0 then
      lcd.drawText(62, 26, tostring(heartRate), DBLSIZE)
      blinkCounter = blinkCounter + 1
    else
      lcd.drawText(62, 26, "--", DBLSIZE)
      blinkCounter = 0
    end
    if blinkCounter <= 15 then
      lcd.drawPixmap(42, 26, "/SCRIPTS/TELEMETRY/heart.bmp")
    end
    if blinkCounter >= 20 then 
      blinkCounter = 0
    end
    lcd.drawText(47, 57, "Max "..tostring(maxHeartRate), SMALLSIZE)
  elseif displayMode == 2 then -- Graph
    for bpm = 50,150,10 do
      lcd.drawLine(17, bpmToY(bpm), 17+statsSize, bpmToY(bpm), DOTTED, FORCE)
    end
    for bpm=60,140,20 do
      if (bpm < 100) then x = 6 else x = 1 end
      lcd.drawText(x, bpmToY(bpm) - 3, tostring(bpm), SMALLSIZE)
    end
    for i = 1,(statsSize-1) do
      x1 = 17 + i
      x2 = 17 + i + 1
      y1 = bpmToY(stats[cntToPnt(i)])
      y2 = bpmToY(stats[cntToPnt(i+1)])
      lcd.drawLine(x1, y1, x2, y2, SOLID, FORCE)
      if arms[cntToPnt(i)] then
        lcd.drawLine(x2, 0, x2, 1, SOLID, FORCE)
      end
    end
    for i = 2,(statsSize*avgInterval/60+1) do
      x = 17 + statsSize - (i-1)*(60/avgInterval)
      lcd.drawLine(x, 1, x, 63, DOTTED, FORCE)
      for bpm = 60,140,20 do
        y = bpmToY(bpm)
        lcd.drawLine(x-1, y, x+1, y, SOLID, FORCE)
        lcd.drawLine(x, y-1, x, y+1, SOLID, FORCE)
      end
    end
  else
    lcd.drawLine(LCD_W-1, 0, LCD_W-1, LCD_H-1, SOLID, FORCE)
    lcd.drawLine(0, 0, 0, LCD_H-1, SOLID, FORCE)
    local pos = 0
    for i = logPointer, logSize do
      lcd.drawText(2, pos*7, log[i], SMLSIZE)
      pos = pos + 1
    end
    for i = 1, logPointer-1 do
      lcd.drawText(2, pos*7, log[i], SMLSIZE)
      pos = pos + 1
    end
  end
end

local function run_func(event)   
  if ((event == EVT_ROT_RIGHT) or (event == EVT_PLUS_BREAK)) and (displayMode < 3) then
    displayMode = displayMode + 1
  end
  if ((event == EVT_ROT_LEFT) or (event == EVT_MINUS_BREAK)) and (displayMode > 1) then
    displayMode = displayMode - 1
  end
  if (event == EVT_ENTER_BREAK) then 
    maxHeartRate = 0
    if displayMode == 3 then
      serialWrite('R')
    end
  end
  drawDisplay()
  return 0
end

local function bg_func()
  --local heartRate = 0
  currentTime = getTime()
  data = serialRead()
  
  if data then
    value = processRxData(data)
  end
  
  if value then
    lastSerialRxTime = getTime()
    if value < 30 then
      status = value
      heartRate = 0
    else
      status = ST_CONNECTED
      heartRate = value
    end
    lastSerialRxTime = getTime()
  end
  
  if (getTime() - lastSerialRxTime) > 200 then
    status = -1
    heartRate = 0
  end
  model.setGlobalVariable(0, 0, heartRate-100)

  putToAvgBuffer(heartRate)
  return 0
end

return { run=run_func, background=bg_func, init=init_func }
