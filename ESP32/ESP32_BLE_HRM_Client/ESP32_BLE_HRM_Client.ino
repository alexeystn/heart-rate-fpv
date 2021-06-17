#include "BLEDevice.h"
#include "driver/rmt.h"

// Enter MAC address of your Heart Rate Monitor here:
//#define BLE_HRM_MAC_ADDRESS "f6:c7:39:de:70:fd"
//#define BLE_HRM_MAC_ADDRESS "ee:ac:dd:9d:79:bb"
// Enable this line to connect to a specified HRM.
// Disable if you want to connect to any available HRM.

static uint8_t heartRate = 0;
static uint32_t lastGoodMeasurementTime = 0;
static boolean connected = false;

#define CONTACT_LOST_TIMEOUT_MS   5000

TaskHandle_t rmtTaskHandle = NULL;
TaskHandle_t bleTaskHandle = NULL;
TaskHandle_t ledTaskHandle = NULL;
portMUX_TYPE mux;

#define RMT_TX_CHANNEL   RMT_CHANNEL_0
#define RMT_TX_GPIO      13

#define PPM_PULSE    300
#define PPM_DEFAULT  1500
#define PPM_MIN      1000
#define PPM_MAX      2000


static rmt_item32_t ppmArray[] = {
  {{{ PPM_PULSE, 1, PPM_DEFAULT, 0 }}},
  {{{ PPM_PULSE, 1, PPM_DEFAULT, 0 }}},
  {{{ PPM_PULSE, 1, PPM_DEFAULT, 0 }}},
  {{{ PPM_PULSE, 1, PPM_DEFAULT, 0 }}},
  {{{ PPM_PULSE, 1, PPM_DEFAULT, 0 }}},
  {{{ PPM_PULSE, 1, PPM_DEFAULT, 0 }}},
  {{{ PPM_PULSE, 1, PPM_DEFAULT, 0 }}},
  {{{ PPM_PULSE, 1, PPM_DEFAULT, 0 }}},
  {{{ PPM_PULSE, 1, PPM_DEFAULT, 0 }}}, // 9th pulse to terminate 8 ch
};

#define PPM_CHANNEL_COUNT  (sizeof(ppmArray)/sizeof(ppmArray[0]))

void RMT_setup() {
  rmt_config_t config;

  config.rmt_mode = RMT_MODE_TX;
  config.channel = RMT_TX_CHANNEL;
  config.gpio_num = (gpio_num_t)RMT_TX_GPIO;
  config.clk_div = 80;
  config.mem_block_num = 1;
  config.tx_config.carrier_freq_hz = 0;
  config.tx_config.carrier_level = RMT_CARRIER_LEVEL_HIGH;
  config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
  config.tx_config.carrier_duty_percent = 50;
  config.tx_config.carrier_en = false;
  config.tx_config.loop_en = false;
  config.tx_config.idle_output_en = true;

  rmt_config(&config);
  rmt_driver_install(config.channel, 0, 0);

}

#define LED_PIN  2

void RMT_loop(void) {
  uint8_t hr;
  while (1) {
    portENTER_CRITICAL(&mux);
    hr = heartRate;
    portEXIT_CRITICAL(&mux);    

    //map(value, fromLow, fromHigh, toLow, toHigh)
    int pulse_size = map(hr, 0, 200, PPM_MIN, PPM_MAX);
    pulse_size = constrain(pulse_size, PPM_MIN, PPM_MAX) - PPM_PULSE;
    
    for (int i = 0; i < PPM_CHANNEL_COUNT; i++) {
      ppmArray[i].duration1 = pulse_size;
    }
    
    rmt_write_items(RMT_TX_CHANNEL, ppmArray, PPM_CHANNEL_COUNT, true);
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}


void LED_setup(void) {
  pinMode(LED_PIN, OUTPUT);
}

void blink(int onTimeMs, int offTimeMs) {
  digitalWrite(LED_PIN, 1);
  vTaskDelay(onTimeMs / portTICK_PERIOD_MS);
  digitalWrite(LED_PIN, 0);
  vTaskDelay(offTimeMs / portTICK_PERIOD_MS);
}


enum status_t {Disconnected = 0, ContactLost = 1, ContactOk = 2}; 

void LED_loop(void) {
  enum status_t status;
  while (1) {
    portENTER_CRITICAL(&mux);
    if (connected) {
      if (heartRate > 0) {
        status = ContactOk;
      } else {
        status = ContactLost;
      }      
    } else {
      status = Disconnected;
    }
    portEXIT_CRITICAL(&mux);

    switch (status) {
      case Disconnected:
        blink(50, 150);
        break;
      case ContactOk:
        blink(50, 1500);
        break;
      case ContactLost:
        blink(500, 500);
        break;
    }
  }
}



/**
 * This part is based on ESP32 BLE example from Arduino library
 */

// The remote HRM service we wish to connect to.
static  BLEUUID serviceUUID(BLEUUID((uint16_t)0x180D));
// The HRM characteristic of the remote service we are interested in.
static  BLEUUID    charUUID(BLEUUID((uint16_t)0x2A37));

static boolean doConnect = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;

static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    // refer to specification:
    // https://www.bluetooth.com/wp-content/uploads/Sitecore-Media-Library/Gatt/Xml/Characteristics/org.bluetooth.characteristic.heart_rate_measurement.xml
    if (pData[0] & 0x02) {
      lastGoodMeasurementTime = millis();
    }
    portENTER_CRITICAL(&mux);
    if ((millis() - lastGoodMeasurementTime) > CONTACT_LOST_TIMEOUT_MS) {
      heartRate = 0;
    } else {
      heartRate = pData[1];
    }
    portEXIT_CRITICAL(&mux);
    Serial.print("Notify callback for characteristic ");
    Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
    Serial.print(" of data length ");
    Serial.println(length);
    Serial.printf("%6d  ", millis());
    Serial.print("data: ");
    for (uint8_t i = 0; i < length; i++) {
      Serial.printf("%5d ", pData[i]);
    }
    Serial.println();
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("onDisconnect");
    portENTER_CRITICAL(&mux);
    heartRate = 0;
    portEXIT_CRITICAL(&mux);
  }
};

bool connectToServer() {
    Serial.print("Forming a connection to ");
    Serial.println(myDevice->getAddress().toString().c_str());
    
    BLEClient*  pClient  = BLEDevice::createClient();
    Serial.println(" - Created client");

    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the remove BLE Server.
    pClient->connect(myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
    Serial.println(" - Connected to server");

    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
      Serial.print("Failed to find our service UUID: ");
      Serial.println(serviceUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our service");


    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) {
      Serial.print("Failed to find our characteristic UUID: ");
      Serial.println(charUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our characteristic");

    // Read the value of the characteristic.
    if(pRemoteCharacteristic->canRead()) {
      std::string value = pRemoteCharacteristic->readValue();
      Serial.print("The characteristic value was: ");
      Serial.println(value.c_str());
    }

    if(pRemoteCharacteristic->canNotify())
      pRemoteCharacteristic->registerForNotify(notifyCallback);

    connected = true;
    return true;
}
/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
 /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {

#ifdef BLE_HRM_MAC_ADDRESS
      if (!(advertisedDevice.getAddress().toString() == BLE_HRM_MAC_ADDRESS)) {
        Serial.println("Incorrect MAC address");
        return;
      }
#endif

      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = true;

    } // Found our server
  } // onResult
}; // MyAdvertisedDeviceCallbacks


void BLE_setup(void) {
  Serial.begin(115200);
  Serial.println("Starting Arduino BLE Client application...");
  BLEDevice::init("");

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(3600, false); // scan for 1 hour
} // End of setup.


// This is the Arduino main loop function.
void BLE_loop(void) {
  while(1) {
  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are 
  // connected we set the connected flag to be true.
  if (doConnect == true) {
    if (connectToServer()) {
      Serial.println("We are now connected to the BLE Server.");
    } else {
      Serial.println("We have failed to connect to the server; there is nothin more we will do.");
    }
    doConnect = false;
  }

  // If we are connected to a peer BLE Server, update the characteristic each time we are reached
  // with the current time since boot.
  if (connected) {
    String newValue = "Time since boot: " + String(millis()/1000);
    Serial.println("Setting new characteristic value to \"" + newValue + "\"");
    
    // Set the characteristic's value to be the array of bytes that is actually a string.
    pRemoteCharacteristic->writeValue(newValue.c_str(), newValue.length());
  }else if(doScan){
    BLEDevice::getScan()->start(0);  // this is just example to start scan after disconnect, most likely there is better way to do it in arduino
  }
  
  vTaskDelay(3000 / portTICK_PERIOD_MS);; // Delay a second between loops.
  }
} // End of loop


void BLE_task(void * p){
  BLE_setup();
  BLE_loop();
}

void RMT_task(void * p){
  RMT_setup();
  RMT_loop();
}

void LED_task(void * p){
  LED_setup();
  LED_loop();
}

void setup() {
  vPortCPUInitializeMutex(&mux);
  xTaskCreate( &BLE_task, "Bluetooth", 100000, NULL, 1, &bleTaskHandle );
  xTaskCreate( &RMT_task, "Pulses",    10000,  NULL, 2, &rmtTaskHandle );
  xTaskCreate( &LED_task, "Blink",     1000,   NULL, 3, &ledTaskHandle );
} 

void loop() {
} 
