local heartRate = 0
local nextHRUpdateTime = 0
local nextStatsUpdateTime = 0
local currentTime = 0

local stats = {}
local statsPointer = 1
statsSize = 128 - 17 - 2

local function putToStats(bpm)
  stats[statsPointer] = bpm
  if statsPointer < #stats then
    statsPointer = statsPointer + 1
  else
    statsPointer = 1
  end
end

local avgSum = 0
local avgCount = 0
local avgInterval = 2.15 --seconds

local function putToAvgBuffer(bpm)
  if bpm > 0 then
    avgSum = avgSum + bpm
    avgCount = avgCount + 1
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

local function init_func()
  for i = 1,statsSize do
    stats[i] = 0
  end
end

local function bpmToY(bpm)
  return 64 - 7 - (bpm - 60) * 4 / 5
end

local function cntToPnt(i)
  local res = statsPointer + i - 1
  if res > statsSize then
    return (res - statsSize)
  else
    return res
  end
end

local showGraph = true
local blinkCounter = 0

local function drawDisplay()
  lcd.clear()
  if showGraph then -- Single number
    lcd.drawFilledRectangle(0, 0, LCD_W, 10)
    lcd.drawText(34, 1, "Heart rate", INVERS)
    if heartRate > 30 then
      lcd.drawText(62, 28, tostring(heartRate), DBLSIZE)
      blinkCounter = blinkCounter + 1
    else
      lcd.drawText(62, 28, "--", DBLSIZE)
      blinkCounter = 0
    end
    if blinkCounter <= 15 then
      lcd.drawPixmap(42, 28, "/SCRIPTS/TELEMETRY/heart.bmp")
    end
    if blinkCounter >= 20 then 
      blinkCounter = 0
    end    
  else -- Graph
    for bpm = 60,120,10 do
      lcd.drawLine(17, bpmToY(bpm), 17+statsSize, bpmToY(bpm), DOTTED, FORCE)
    end
    for bpm=60,120,20 do
      if (bpm < 100) then x = 6 else x = 1 end
      lcd.drawText(x, bpmToY(bpm) - 3, tostring(bpm), SMALLSIZE)
    end
    for i = 1,(statsSize-1) do
      x1 = 17 + i
      x2 = 17 + i + 1
      y1 = bpmToY(stats[cntToPnt(i)])
      y2 = bpmToY(stats[cntToPnt(i+1)])
      lcd.drawLine(x1, y1, x2, y2, SOLID, FORCE)
    end
    for i = 2,(statsSize*avgInterval/60+1) do
      x = 17 + statsSize - (i-1)*(60/avgInterval) + 1
      lcd.drawLine(x, 2, x, 63, DOTTED, FORCE)
      for bpm = 60,120,20 do
        y = bpmToY(bpm)
        lcd.drawLine(x-1, y, x+1, y, SOLID, FORCE)
      end
    end
  end
end

local function run_func(event) 
  local newHeartRate = 0
  if (event == EVT_ROT_RIGHT) or (event == EVT_ROT_LEFT) then
    showGraph = not showGraph
  end
  currentTime = getTime()
  newHeartRate = math.floor( ((getValue('trn1') + 1000 ) * 200 ) / 2000)
  if currentTime > nextHRUpdateTime then
    heartRate = newHeartRate
    nextHRUpdateTime = currentTime + 100
  end
  putToAvgBuffer(heartRate)
  drawDisplay()
  return 0
end

return { init=init_func, run=run_func }
