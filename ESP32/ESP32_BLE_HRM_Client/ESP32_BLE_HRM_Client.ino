#include "BLEDevice.h"
//#include "driver/rmt.h"
#include <FastLED.h>

#define LED_PIN     10
#define NUM_LEDS    2
#define LED_TYPE    WS2812

CRGB leds[NUM_LEDS];

boolean wasConnected = false;
boolean keyPressedFlag = 0;
uint8_t connectCommandFlag = 0;
boolean foundSavedDevice = false;

esp_bd_addr_t advertisedAddress;
esp_bd_addr_t savedAddress = {0xf6, 0xc7, 0x39, 0xde, 0x70, 0xfd};



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


enum state_t {ST_IDLE = 0, ST_AVAILABLE = 1, ST_CONNECTED = 2}; 


state_t state = ST_IDLE;


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


void KEY_setup(void) {
  pinMode(9, INPUT_PULLUP);
  vTaskDelay(1000 / portTICK_PERIOD_MS);
}


void KEY_loop(void) {
  uint8_t k;
  uint8_t previousState = 1;
  uint8_t currentState = 0;
  while (1) {

    previousState = currentState;
    currentState = digitalRead(9);
    if ((previousState == 1) && (currentState == 0)) {
      keyPressedFlag = 1;
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);


    if (keyPressedFlag && (state == ST_AVAILABLE)) {
      BLEDevice::getScan()->stop();
      Serial.println("Bind key pressed");
      connectCommandFlag = 1;
    }
  }
}


void LED_setup(void) {
  FastLED.addLeds<LED_TYPE, LED_PIN>(leds, NUM_LEDS);
  FastLED.setBrightness(40);
}


void LED_loop(void) {
  while (1) {
    switch (state) {
      case ST_IDLE:
        leds[0] = CRGB::Red;
        FastLED.show();
        vTaskDelay(100 / portTICK_PERIOD_MS);
        leds[0] = CRGB::Black;
        FastLED.show();
        vTaskDelay(200 / portTICK_PERIOD_MS); 
        break;
      case ST_AVAILABLE:
        leds[0] = CRGB::Yellow;
        FastLED.show();
        vTaskDelay(75 / portTICK_PERIOD_MS);
        leds[0] = CRGB::Black;
        FastLED.show();
        vTaskDelay(75 / portTICK_PERIOD_MS); 
        break;
      case ST_CONNECTED:
        leds[0] = CRGB::Blue;
        FastLED.show();
        vTaskDelay(100 / portTICK_PERIOD_MS);
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
static boolean deviceFound = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;

static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic,
                           uint8_t* pData,
                           size_t length,
                           bool isNotify) {
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
    Serial.printf("%7d  ", millis());
    Serial.printf("Notification %s: ", pBLERemoteCharacteristic->getUUID().toString().c_str());
    for (uint8_t i = 0; i < length; i++) {
      Serial.printf("%3d ", pData[i]);
    }
    Serial.println();
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    Serial.println("'onConnect' callback");
  }

  void onDisconnect(BLEClient* pclient) {
    Serial.println("'onDisconnect' callback");
    state = ST_IDLE;
  }
};

bool connectToServer() {
    Serial.print("Forming a connection to ");
    Serial.println(myDevice->getAddress().toString().c_str());
    
    BLEClient* pClient = BLEDevice::createClient();
    Serial.println("Created client");

    pClient->setClientCallbacks(new MyClientCallback());
    pClient->connect(myDevice);
    Serial.println("Connected to server");

    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
      Serial.println("Failed to find service");
      Serial.println(serviceUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println("Service found");

    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) {
      Serial.println("Failed to find characteristic");
      pClient->disconnect();
      return false;
    }
    Serial.println("Characteristic found");

    if(pRemoteCharacteristic->canRead()) {
      Serial.print("The characteristic value: ");
      Serial.println(pRemoteCharacteristic->readValue());
    }

    if(pRemoteCharacteristic->canNotify())
      pRemoteCharacteristic->registerForNotify(notifyCallback);

    //connected = true;
    state = ST_CONNECTED;
    return true;
}


class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
 // Called for each advertising BLE server
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {

      //Serial.print("Heart rate monitor available");
      //Serial.println(advertisedDevice.getAddress().toString());
      
      //advertisedAddress = &();

      boolean isMACok = false;

      if (memcmp(advertisedDevice.getAddress().getNative(), savedAddress, 6) == 0) {
        isMACok = true;
      }

      if (isMACok) {
        foundSavedDevice = true;
        BLEDevice::getScan()->stop();
      }

      if (myDevice != NULL) {
        delete myDevice;
      }
      myDevice = new BLEAdvertisedDevice(advertisedDevice);      
      state = ST_AVAILABLE;



      

    }
  }
};


void BLE_setup(void) {
  Serial.begin(115200);
  Serial.println("Starting Arduino BLE Client application...");
  // TODO: load address from EEPROM
  BLEDevice::init("");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(0, false);
}



void BLE_loop(void) {

  Serial.println("Enter BLE loop");
  while(1) {
    Serial.println("Loop");
  
    if ((keyPressedFlag) || (foundSavedDevice)) {
      keyPressedFlag = 0;
      if (state == ST_AVAILABLE) {
        if (connectToServer()) {
          Serial.println("Connected to the BLE Server");
          wasConnected = true;
          // TODO: save address to EEPROM
        } else {
          Serial.println("Failed to connect to the server");
          state = ST_IDLE;
        }
      }      
    }

    if (state == ST_IDLE) {
      if (wasConnected) {
        BLEDevice::getScan()->start(0);
      }
    }  
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// TODO: no connection in 1 minute: Sleep mode


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
  KEY_setup();
  KEY_loop();
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
