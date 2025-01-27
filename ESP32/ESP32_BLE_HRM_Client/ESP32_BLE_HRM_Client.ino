#include "config.h"
#include "BLEDevice.h"
#include <EEPROM.h>

#if USE_RGB_LEDS
#include <FastLED.h>
#endif

boolean wasConnected = false;
boolean foundSavedDevice = false;

esp_bd_addr_t advertisedAddress;
esp_bd_addr_t savedAddress;

static uint8_t heartRate = 0;
static uint32_t lastGoodMeasurementTime = 0;
static boolean connected = false;

#define CONTACT_LOST_TIMEOUT_MS   5000

TaskHandle_t outTaskHandle = NULL;
TaskHandle_t bleTaskHandle = NULL;
TaskHandle_t ledTaskHandle = NULL;
TaskHandle_t keyTaskHandle = NULL;
portMUX_TYPE mux;

enum state_t {ST_IDLE = 1, ST_AVAILABLE = 2, ST_CONNECTED = 3};  // 0 is reserved

state_t state = ST_IDLE;


void Print_Address(uint8_t *p) {
  for (int i = 0; i < 5; i++) {
    Serial.printf("%02x:", p[i]);
  }
  Serial.printf("%02x\n", p[5]);
}


void EEPROM_Load(void) {
  EEPROM.begin(6);
  for (int i = 0; i < 6; i++) {
    savedAddress[i] = EEPROM.read(i);
  }
  Serial.print("Loaded address from memory: ");
  Print_Address(savedAddress);
}


void EEPROM_Save(void) {
  for (int i = 0; i < 6; i++) {
    EEPROM.write(i, advertisedAddress[i]);
  }
  EEPROM.commit();
  Serial.print("Saved address to memory: ");
  Print_Address(advertisedAddress);
}


void OUT_setup() {
  Serial1.begin(115200, SERIAL_8N1, 20, 21);
}


void OUT_loop(void) {
  uint8_t data;
  Serial1.println("S:ESP32 Heart Rate");
  while (1) {
    switch (state) {
      case ST_IDLE:
        data = ST_IDLE;
        break;
      case ST_AVAILABLE:
        data = ST_AVAILABLE;
        break;
      case ST_CONNECTED:
        data = heartRate;
        break;
    }
    Serial1.print("H:");
    Serial1.println(data);
    if (Serial1.available()) {
      uint8_t b = Serial1.read();
      if (b == 'R') {
        ESP.restart();
      }
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}


void KEY_setup(void) {
  pinMode(9, INPUT_PULLUP);
  vTaskDelay(1000 / portTICK_PERIOD_MS);
}


void KEY_loop(void) {
  uint32_t lastUnpressedTime = 0;
  uint32_t holdTime = 0;
  while (1) {
    if (digitalRead(9) == 1) {
      lastUnpressedTime = millis();
      holdTime = 0;
    } else {
      holdTime = millis() - lastUnpressedTime;
    }
#if USE_FIXED_MAX_ADDRESS == 0
    if ((holdTime > 1000) && (state == ST_AVAILABLE)) {
      Serial.println("Bind key pressed");
      EEPROM_Save();
      vTaskDelay(500 / portTICK_PERIOD_MS);
      ESP.restart();
    }
#endif
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}



#if USE_RGB_LEDS
CRGB leds[2];
#define LED_OFF    { leds[0] = CRGB::Black; FastLED.show(); }
#define LED_ON(x)  { leds[0] = CRGB::x; FastLED.show(); }
#else
#define LED_OFF     digitalWrite(LED_PIN, 1)
#define LED_ON(x)   digitalWrite(LED_PIN, 0)
#endif

void LED_setup(void) {
#if USE_RGB_LEDS
  FastLED.addLeds<WS2812, LED_PIN>(leds, 2);
  FastLED.setBrightness(40);
#else
  pinMode(LED_PIN, OUTPUT);
#endif
}


void LED_loop(void) {
  uint32_t lastConnectedTime = 0;
  while (1) {
    switch (state) {
      case ST_IDLE:
        LED_ON(Red);
        vTaskDelay(200 / portTICK_PERIOD_MS);
        LED_OFF;
        vTaskDelay(200 / portTICK_PERIOD_MS);
        break;
      case ST_AVAILABLE:
        LED_ON(Orange);
        vTaskDelay(50 / portTICK_PERIOD_MS);
        LED_OFF;
        vTaskDelay(50 / portTICK_PERIOD_MS);
        break;
      case ST_CONNECTED:
        lastConnectedTime = millis();
        LED_ON(Blue);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        break;
    }
    
    if ((millis() > (SLEEP_TIMEOUT)*1000) && (lastConnectedTime == 0)) {
      Serial.println("Enter sleep mode");
      LED_OFF;
      esp_deep_sleep_start();
    }
  }
}


static  BLEUUID serviceUUID(BLEUUID((uint16_t)0x180D));
static  BLEUUID    charUUID(BLEUUID((uint16_t)0x2A37));

static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;

static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic,
                           uint8_t* pData,
                           size_t length,
                           bool isNotify) {
  if (pData[0] & 0x02) {
    lastGoodMeasurementTime = millis();
  }
  //portENTER_CRITICAL(&mux);
  if ((millis() - lastGoodMeasurementTime) > CONTACT_LOST_TIMEOUT_MS) {
    heartRate = 0;
  } else {
    heartRate = pData[1];
  }
  //portEXIT_CRITICAL(&mux);
  Serial.printf("Notification %s: ", pBLERemoteCharacteristic->getUUID().toString().c_str());
  for (uint8_t i = 0; i < length; i++) {
    Serial.printf("%3d ", pData[i]);
  }
  Serial.println();
}

class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {
      Serial.println("'onConnect' callback");
      Serial1.println("S:Connected");
    }
    void onDisconnect(BLEClient* pclient) {
      Serial.println("'onDisconnect' callback");
      Serial1.println("S:Disconnected");
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

  if (pRemoteCharacteristic->canRead()) {
    Serial.print("The characteristic value: ");
    Serial.println(pRemoteCharacteristic->readValue());
  }

  if (pRemoteCharacteristic->canNotify())
    pRemoteCharacteristic->registerForNotify(notifyCallback);

  state = ST_CONNECTED;
  return true;
}


class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      Serial.print("BLE Advertised Device found: ");
      Serial.println(advertisedDevice.toString().c_str());

      if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {
        
        Serial.print("Heart rate monitor available: ");
        Serial.println(advertisedDevice.getAddress().toString());

#if USE_FIXED_MAX_ADDRESS
        if (advertisedDevice.getAddress().toString() != FIXED_MAX_ADDRESS)) {
          return;
        }
#endif
        
        memcpy(advertisedAddress, advertisedDevice.getAddress().getNative(), 6);
        
        if (memcmp(advertisedDevice.getAddress().getNative(), savedAddress, 6) == 0) { 
          foundSavedDevice = true;
          myDevice = new BLEAdvertisedDevice(advertisedDevice);
          BLEDevice::getScan()->stop();
        }
        state = ST_AVAILABLE;
      }
    }
};


void BLE_setup(void) {
  Serial.begin(115200);
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  EEPROM_Load();
  Serial.println("Starting Arduino BLE Client application...");
  BLEDevice::init("");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(0, false);
}


void BLE_loop(void) {
  while (1) {
    if (foundSavedDevice) {
      if (connectToServer()) {
        Serial.println("Connected to the BLE Server");
        wasConnected = true;
      } else {
        Serial.println("Failed to connect to the server");
        state = ST_IDLE;
      }
      foundSavedDevice = 0;
    }
    if (state == ST_IDLE) {
      if (wasConnected) { // reconnect
        BLEDevice::getScan()->start(0);
      }
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}


void BLE_task(void * p) {
  BLE_setup();
  BLE_loop();
}

void OUT_task(void * p) {
  OUT_setup();
  OUT_loop();
}

void LED_task(void * p) {
  LED_setup();
  LED_loop();
}

void KEY_task(void * p) {
  KEY_setup();
  KEY_loop();
}


void setup() {
  portMUX_INITIALIZE(&mux);
  xTaskCreate( &BLE_task, "Bluetooth", 100000, NULL, 1, &bleTaskHandle );
  xTaskCreate( &OUT_task, "Pulses",    10000,  NULL, 2, &outTaskHandle );
  xTaskCreate( &LED_task, "Blink",     10000,  NULL, 3, &ledTaskHandle );
  xTaskCreate( &KEY_task, "Key",       10000,   NULL, 4, &keyTaskHandle );
}

void loop() {
}
