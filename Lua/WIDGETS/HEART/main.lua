local heartRate = 0
local currentTime = 0

local ST_NO_ESP32 = -1
local ST_IDLE = 0
local ST_AVAILABLE = 1
local ST_CONNECTED = 2
local ST_ERROR = 3

local status = ST_NO_ESP32
local lastSerialRxTime = 0

local maxHeartRate = 0

local heartBitmap

local function parseMessage(str)
  local s
  if string.sub(str, 3, 3) == ":" then
    s = string.sub(str, 2, 2)
    if s == "D" then
      status = ST_IDLE
      heartRate = 0
    elseif s == "A" then
      status = ST_AVAILABLE
      heartRate = 0
    elseif s == "C" then
      status = ST_CONNECTED
      heartRate = tonumber(string.sub(str, 4, -1))
      if not heartRate then
        heartRate = 0
      end
    end
  end
end

local rxLine = ""
local ch = 0

local function processRxData(str)
  local ret = false
  for i = 1,string.len(str) do
    ch = string.sub(str, i, i)
    rxLine = rxLine .. ch
    if ch == '\n' then
      if string.sub(rxLine, 1, 1) == ">"  then
        --putToLog(rxLine)
        parseMessage(rxLine)
        ret = true
      end
      rxLine = ""
    end
    if string.len(rxLine) > 50 then
      rxLine = ""
    end
  end
  return ret
end


local function init_func()
  setSerialBaudrate(9600)
  heartBitmap = Bitmap.open("/WIDGETS/HEART/heart.png")
end



local blinkCounter = 0

local function drawDisplay()
  lcd.clear()

  if status == ST_NO_ESP32 then 
    lcd.drawText(10, 1, "ESP32 not connected")
  elseif status == ST_IDLE then
    lcd.drawText(19, 1, "Sensor not found")
  elseif status == ST_AVAILABLE then
    lcd.drawText(22, 1, "Sensor available")
  else
    lcd.drawText(34, 1, "Heart rate")
  end
    
  if heartRate > 0 then
    lcd.drawText(75, 20, tostring(heartRate), DBLSIZE)
    blinkCounter = blinkCounter + 1
  else
    lcd.drawText(75, 20, "--", DBLSIZE)
    blinkCounter = 0
  end
  if blinkCounter <= 15 then
    lcd.drawBitmap(heartBitmap, 42, 26)
  end
  if blinkCounter >= 20 then 
    blinkCounter = 0
  end
  lcd.drawText(65, 60, "Max "..tostring(maxHeartRate), SMALLSIZE)

end


local function bg_func()
  currentTime = getTime()
  data = serialRead()
  if data then
    value = processRxData(data)
    if value then
      lastSerialRxTime = getTime()
    end
  end
  if (getTime() - lastSerialRxTime) > 200 then
    status = ST_NO_ESP32
    heartRate = 0
  end
  model.setGlobalVariable(0, 0, heartRate-100)
  if heartRate > maxHeartRate then
    maxHeartRate = heartRate
  end
end

name = "Heart Rate"

local options = {
}

local function create(zone, options) -- only once
  init_func()
end

local function update(widget, options) -- if options are changed
end

local function background(widget)  -- periodically, when not visible
  bg_func()
end

local function refresh(widget, event, touchState)  -- periodically, when visible
  print("Event", event, touchState)
  if event == EVT_TOUCH_FIRST then
    maxHeartRate = 0
  end
  drawDisplay()
  bg_func()
end

return {
  name = name,
  options = options,
  create = create,
  update = update,
  refresh = refresh,
  background = background
}
