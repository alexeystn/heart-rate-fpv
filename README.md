# Heart Rate Monitor for FPV drones

Watch your heart rate on Betaflight OSD 💓

<p align="center">
<img src="Images/block_diagram.png" width="500"/>
</p>

See how excited you are while:<br>
* Diving the highest skyscraper 🏙 <br>
* Flying long-range over the mountains 🏔 <br>
* Racing in the championship finals 🏁

Get a realtime bio-feedback to improve your flight stability.<br> 
Practice to control your stress level 🧘‍♂

Demo flights video:

<p align="center">
  
[![Watch the video](https://img.youtube.com/vi/yA9xkAsMA1U/mqdefault.jpg)](https://youtu.be/yA9xkAsMA1U)  [![Watch the video](https://img.youtube.com/vi/_b7GmlouoaM/mqdefault.jpg)](https://youtu.be/_b7GmlouoaM)  

</p>

### 1. HR Monitor
Get any available chest strap heart rate monitor with Bluetooth (for example, Kalenji ZT26D, Magene H64).<br>
Chest strap monitors are recommended.<br>
They are widely used by athletes, because they provide higher precision, not affected by body movements. Although, you may use wrist-strap monitor.

### 2. ESP32 Firmware

1. Upload [firmware](ESP32/ESP32_BLE_HRM_Client) to ESP32 board. ([How to install ESP32 in Arduino IDE](https://randomnerdtutorials.com/installing-the-esp32-board-in-arduino-ide-windows-instructions/))

2. Find out MAC address of your HR-monitor:

Put on the HR-monitor.<br>
Open "Tools -> Serial Monitor" in Arduino IDE.<br>
Now you can see continuously refreshing list of all available Bluetooth devices around you:

```
...
BLE Advertised Device found: Name: MI_SCALE, Address: 13:a6:c8:0f:1b:d7, manufacturer data: 580f10ac61b701d7
BLE Advertised Device found: Name: , Address: 50:fe:76:ce:35:ea, manufacturer data: 4c0010054a1c7f72c3
BLE Advertised Device found: Name: Mi Smart Band 4, Address: c2:0d:36:02:8c:31, manufacturer data: 578c110102f3602
BLE Advertised Device found: Name: , Address: 72:89:7b:87:fe:dd, manufacturer data: 4c0010054a1c7f72c3
BLE Advertised Device found: Name: Decathlon Dual HR, Address: f6:de:70:c7:39:fd, appearance: 833, serviceUUID: 0000180d-0000-1000-8000-00805f9b34fb
...
```

Find the line corresponding to your device.<br>
For example, there is my "Decathlon Dual HR" monitor with `f6:de:70:c7:39:fd` address in the list above.<br>

3. Replace the address in .ino-file with the address of real device:

` #define BLE_HRM_MAC_ADDRESS "01:23:45:67:89:ab"`

4. Flash ESP32 again.<br>
Open "Serial Monitor".<br>
When ESP board establishes connection with HR-monitor you can see:

```
Forming a connection to f6:de:70:c7:39:fd
 - Created client
 - Connected to server
 - Found our service
 - Found our characteristic
We are now connected to the BLE Server.
 ...
```

Now ESP32 board is bound to HR-monitor.


### 3. Hardware
Wire up ESP32 module to your Radio transmitter as depicted below:
<p align="center">
<img src="Images/connection_diagram.png" width="400" />
</p>
You can use external 5V source (such as power-bank) or connect it to available 5V transmitter outputs (e.g. pins in JR module bay, Radiomaster TX16S bottom connector, etc.).

Trainer port uses 3.5mm 3-pin jack connector.

**For advanced enthusiasts:**<br>
You can wire it up directly to the main board of your radio. Pads are not labeled usually, so be very careful!! <br>
Here is an example, how I did it for my Taranis Q X7 board:
<p align="center">
<img src="Images/taranis_qx7.jpg" width="250" />
</p>

### 4. OpenTX
1. Configure Trainer port as `Master/Jack`. Go to `Trainer` page to make sure if some variable signal is coming (bottom line).

<p align="center">
<img src="Images/opentx_master.png"/> <img src="Images/opentx_trainer.png"/>
</p>

2. Go to `Mixer` page. Set one of your channels source to `TR1`. Now you are redirecting heart rate to RX.

<p align="center">
<img src="Images/opentx_mixer.png"/>
</p>

3. (Optional) Copy [`heart.lua`](/Lua/) and [`heart.bmp`](/Lua/) to `SCRIPTS/TELEMETRY` folder on SD card.<br>
Enable it on `Display` page.

<p align="center">
<img src="Images/opentx_telemetry.png"/>
</p>

Now you are able to watch current heart rate and the latest statistics plot on the radio display. 

<p align="center">
<img src="Images/opentx_lua_heart_rate.png"/> <img src="Images/opentx_lua_graph.png"/>
</p>

Switch between them with a scroll wheel.<br>
Only 128x64 displays are supported so far.

### 5. Betaflight

1) Flash your FC with Betaflight 4.4.

2) Upload you favourite [font](/Fonts/mcm) with additional heart symbol in `Font Manager` on OSD tab.

3) Enable `Aux value` element on OSD tab in configurator.

4) Go to CLI and enter the following:
```
set osd_aux_symbol = 121
set osd_aux_channel = 8
save
```

Where `8` is the channel you configured in OpenTX mixer.

Done! 
<br>
<br>

### Future plans
* Tests with many different HRM models.
* Lua scripts for bigger screens (212x64, 480x272).


Feel free to contact me in Telegram: [@AlexeyStn](https://t.me/AlexeyStn)



