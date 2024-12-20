# Heart Rate Monitor for FPV drones

Watch your heart rate on Betaflight OSD 💓

<p align="center">
  
[![Watch the video](https://img.youtube.com/vi/yA9xkAsMA1U/mqdefault.jpg)](https://youtu.be/yA9xkAsMA1U)  [![Watch the video](https://img.youtube.com/vi/_b7GmlouoaM/mqdefault.jpg)](https://youtu.be/_b7GmlouoaM)  

</p>

<p align="left">  
<img src="Images/block_diagram.png" width="500"/>
</p>

### 1. HR Monitor
Get any available chest strap heart rate monitor with Bluetooth (for example, Kalenji ZT26D, Magene H64, Coospo H6).<br>
Chest strap monitors are recommended. They are much more precise than wrist monitors.

### 2. ESP32 Firmware

1. Download source code.
2. Set LED paremeters (USE_RGB_LEDS and LED_PIN)
3. Select ESP32-C3 board, and 80 MHz CPU frequency.
4. Upload to ESP32.

[How to install ESP32 in Arduino IDE](https://randomnerdtutorials.com/installing-the-esp32-board-in-arduino-ide-windows-instructions/)

### 3. Bind procedure

| LED state  | Status |
| :--------: | :-----------: |
| Slow blink | No devices    |
| Fast blink | Device found     |
| Solid      | Device connected |

1. Power up ESP32 module.
2. Put on your heart rate monitor.
3. Wait until LED is blinking fast.
4. Hold BOOT key for 1 second. 
5. Wait until LED is solid.<br>
Now your HR monitor is bound to ESP32 module.

### 4. Connection
Wire up ESP32 module to your Radio transmitter as depicted below:
<p align="center">
<img src="Images/connection_diagram.png" height="300" />
<img src="Images/connector.jpg" height="250" />
</p>

### 5. EdgeTX
1. Configure AUX port for `LUA` in `Hardware` tab.

<p align="center">
<img src="Images/edgetx_aux_lua.png"/>
</p>

2. Set `Switch Mode` to `Wide` in ExpressLRS script.

<p align="center">
<img src="Images/edgetx_switch_mode.png"/>
</p>

3. Configure any aux channel (from CH6 to CH12) as following:<br>
`Source: MAX`<br>
`Weight: GV1`

<p align="center">
<img src="Images/edgetx_mixes.png"/>
</p>

4. Copy [`heart.lua`](/Lua/) and [`heart.bmp`](/Lua/) to `SCRIPTS/TELEMETRY` directory on SD card.<br>
5. Enable `heart` script on `Display` page.

<p align="center">
<img src="Images/edgetx_telemetry.png"/>
</p>

6. Exit to main screen and long press `TELE` key.
<p align="center">
<img src="Images/edgetx_lua_heart_rate.png"/> <img src="Images/edgetx_lua_graph.png"/>
</p>

7. Switch between 2 screens with a scroll wheel.<br>

### 5. Betaflight

1) Use FC with Betaflight ≥4.4.

2) Analog: Upload [font](/Fonts/mcm) with additional heart symbol in `Font Manager` on OSD tab. <br>
   HDZero: Save font file to your goggles' SD card .

3) Enable `Aux value` element on OSD tab in configurator.

4) Enter the following lines in CLI:
```
set osd_aux_symbol = 121
set osd_aux_channel = 11
save
```

Where `11` is the channel you configured in OpenTX mixer.

Done! 
<br>
<br>

### Future plans
* Lua scripts for TX16 color screen 480x272.

Feel free to contact me in Telegram: [@AlexeyStn](https://t.me/AlexeyStn)



