#include "BLEDevice.h"
//#include "driver/rmt.h"
#include <FastLED.h>

#define LED_PIN     10
#define NUM_LEDS    2
#define LED_TYPE    WS2812

CRGB leds[NUM_LEDS];

uint8_t keyPressed = 0;

#define BLE_HRM_MAC_ADDRESS "f6:c7:39:de:70:fd"
// Enter MAC address of your Heart Rate Monitor here.
// You can find MAC address in the fitness app in your phone, 
// or in Serial Monitor in Arduino IDE after uploading this sketch.
// Comment out this line if you want to connect to any available 
// Heart Rate Monitor around you (not recommended)

static uint8_t heartRate = 0;
static uint32_t lastGoodMeasurementTime = 0;
static boolean connected = false;

#define CONTACT_LOST_TIMEOUT_MS   5000

TaskHandle_t outTaskHandle = NULL;
TaskHandle_t bleTaskHandle = NULL;
TaskHandle_t ledTaskHandle = NULL;
TaskHandle_t keyTaskHandle = NULL;
portMUX_TYPE mux;


void OUT_setup() {
  Serial1.begin(115200, SERIAL_8N1, 20, 21);
}

//#define LED_PIN  2

void OUT_loop(void) {
  uint8_t hr;
  while (1) {
    portENTER_CRITICAL(&mux);
    hr = heartRate;
    portEXIT_CRITICAL(&mux);    
    Serial1.write(hr);
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}


void LED_setup(void) {
  FastLED.addLeds<LED_TYPE, LED_PIN>(leds, NUM_LEDS);
  FastLED.setBrightness(40);
}


void blink(int enabled) {
  if (enabled) {
    leds[0] = CRGB::Blue;
    FastLED.show();
    vTaskDelay(100 / portTICK_PERIOD_MS);
  } else {
    if (digitalRead(9)) {
      leds[0] = CRGB::Red;
    } else {
      leds[0] = CRGB::Yellow;
    }
    
    FastLED.show();
    vTaskDelay(100 / portTICK_PERIOD_MS);
    leds[0] = CRGB::Black;
    FastLED.show();
    vTaskDelay(200 / portTICK_PERIOD_MS); 
  }
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
        blink(0);
        break;
      case ContactOk:
        blink(1);
        break;
      case ContactLost:
        blink(0);
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
      //std::string value = ;
      Serial.print("The characteristic value was: ");
      Serial.println(pRemoteCharacteristic->readValue());
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
      Serial.print("Heart rate monitor available!");
      Serial.println(advertisedDevice.getAddress().toString());
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      BLEDevice::getScan()->stop();
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
  if (doConnect == true) { // (keyPressed) {// 
    Serial.println("doConnect = true");
    doScan = true;

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
    //String newValue = "Time since boot: " + String(millis()/1000);
    //Serial.println("Setting new characteristic value to \"" + newValue + "\"");
    
    // Set the characteristic's value to be the array of bytes that is actually a string.
    //pRemoteCharacteristic->writeValue(newValue.c_str(), newValue.length());
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

void OUT_task(void * p){
  OUT_setup();
  OUT_loop();
}

void LED_task(void * p){
  LED_setup();
  LED_loop();
}

void KEY_task(void * p){
  pinMode(9, INPUT_PULLUP);
  while (1) {
    if (digitalRead(9) == 0) {
      keyPressed = 1;
    } else {
      keyPressed = 0;
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}





void setup() {
  portMUX_INITIALIZE(&mux);
  xTaskCreate( &BLE_task, "Bluetooth", 100000, NULL, 1, &bleTaskHandle );
  xTaskCreate( &OUT_task, "Pulses",    10000,  NULL, 2, &outTaskHandle );
  xTaskCreate( &LED_task, "Blink",     10000,   NULL, 3, &ledTaskHandle );
  xTaskCreate( &KEY_task, "Key",     1000,   NULL, 4, &keyTaskHandle );
} 

void loop() {
} 
