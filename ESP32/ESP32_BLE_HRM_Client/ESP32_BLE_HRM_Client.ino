#include "config.h"
#include "BLEDevice.h"
#include <EEPROM.h>

#if USE_RGB_LEDS
#include <FastLED.h>
#endif

boolean foundSavedDevice = false;
boolean wasConnected = false;

esp_bd_addr_t advertisedAddress;
esp_bd_addr_t savedAddress;

static uint8_t heartRate = 0;

#define LUA_PORT  Serial1
#define DBG_PORT  Serial
TaskHandle_t outTaskHandle = NULL;
TaskHandle_t bleTaskHandle = NULL;
TaskHandle_t ledTaskHandle = NULL;
TaskHandle_t keyTaskHandle = NULL;

enum state_t {
  ST_IDLE = 0, 
  ST_AVAILABLE = 1, 
  ST_CONNECTED = 2, 
  ST_ERROR = 3
};

state_t state = ST_IDLE;


void Print_Address(uint8_t *p) {
  for (int i = 0; i < 5; i++) {
    DBG_PORT.printf("%02x:", p[i]);
  }
  DBG_PORT.printf("%02x\n", p[5]);
}

void EEPROM_Load(void) {
  EEPROM.begin(6);
  for (int i = 0; i < 6; i++) {
    savedAddress[i] = EEPROM.read(i);
  }
  DBG_PORT.print("Loaded address from memory: ");
  Print_Address(savedAddress);
}

void EEPROM_Save(void) {
  for (int i = 0; i < 6; i++) {
    EEPROM.write(i, advertisedAddress[i]);
  }
  EEPROM.commit();
  DBG_PORT.print("Saved address to memory: ");
  Print_Address(advertisedAddress);
}


void OUT_setup() {
  LUA_PORT.begin(9600, SERIAL_8N1, 20, 21);
  vTaskDelay(100 / portTICK_PERIOD_MS);
  LUA_PORT.println();
  vTaskDelay(100 / portTICK_PERIOD_MS);
  LUA_PORT.println();
}

void OUT_loop(void) {
  uint8_t data;
  const char stateLabels[4] = {'D', 'A', 'C', 'E'};
  while (1) {
    LUA_PORT.printf(">%c:%d\n", stateLabels[state], heartRate);
    if (LUA_PORT.available()) {
      uint8_t b = LUA_PORT.read();
      if (b == 'R') {
        LUA_PORT.println(">Reboot");
        vTaskDelay(100 / portTICK_PERIOD_MS);
        ESP.restart();
      }
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
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
      DBG_PORT.println("Bind key pressed");
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
    if (((millis() > (SLEEP_TIMEOUT)*1000) && (lastConnectedTime == 0)) && (!wasConnected)) {
      DBG_PORT.println("Enter sleep mode");
      LUA_PORT.println(">Sleep");
      vTaskDelay(100 / portTICK_PERIOD_MS);
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
  heartRate = pData[1];
  DBG_PORT.printf("Notification %s: ", pBLERemoteCharacteristic->getUUID().toString().c_str());
  for (uint8_t i = 0; i < length; i++) {
    DBG_PORT.printf("%3d ", pData[i]);
  }
  DBG_PORT.println();
}


class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {
      DBG_PORT.println("'onConnect' callback");
    }
    void onDisconnect(BLEClient* pclient) {
      DBG_PORT.println("'onDisconnect' callback");
      LUA_PORT.println(">Disconnected");
      vTaskDelay(100 / portTICK_PERIOD_MS);
      state = ST_IDLE;
      ESP.restart();
    }
};


bool connectToServer() {
  DBG_PORT.print("Forming a connection to ");
  DBG_PORT.println(myDevice->getAddress().toString().c_str());

  BLEClient* pClient = BLEDevice::createClient();
  DBG_PORT.println("Created client");

  pClient->setClientCallbacks(new MyClientCallback());
  pClient->connect(myDevice);
  DBG_PORT.println("Connected to server");

  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    DBG_PORT.println("Failed to find service");
    DBG_PORT.println(serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  DBG_PORT.println("Service found");

  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr) {
    DBG_PORT.println("Failed to find characteristic");
    pClient->disconnect();
    return false;
  }
  DBG_PORT.println("Characteristic found");

  if (pRemoteCharacteristic->canRead()) {
    DBG_PORT.print("The characteristic value: ");
    DBG_PORT.println(pRemoteCharacteristic->readValue());
  }

  if (pRemoteCharacteristic->canNotify())
    pRemoteCharacteristic->registerForNotify(notifyCallback);

  foundSavedDevice = 0;
  state = ST_CONNECTED;
  return true;
}


class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      DBG_PORT.print("BLE Advertised Device found: ");
      DBG_PORT.println(advertisedDevice.toString().c_str());

      if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {    
        DBG_PORT.print("Heart rate monitor available: ");
        DBG_PORT.println(advertisedDevice.getAddress().toString());
#if USE_FIXED_MAX_ADDRESS
        if (advertisedDevice.getAddress().toString() != FIXED_MAX_ADDRESS)) {
          return;
        }
#endif
        memcpy(advertisedAddress, advertisedDevice.getAddress().getNative(), 6);
        if (memcmp(advertisedDevice.getAddress().getNative(), savedAddress, 6) == 0) { 
          LUA_PORT.println(">Available");
          myDevice = new BLEAdvertisedDevice(advertisedDevice);
          BLEDevice::getScan()->stop();
          foundSavedDevice = true;
        }
        state = ST_AVAILABLE;
      }
    }
};


void BLE_setup(void) {
  DBG_PORT.begin(115200);
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  EEPROM_Load();
  DBG_PORT.println("Starting Arduino BLE Client application...");
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
        DBG_PORT.println("Connected to the BLE Server");
        LUA_PORT.println(">Connected");
        wasConnected = true;
      } else {
        LUA_PORT.println(">Error");
        DBG_PORT.println("Failed to connect to the server");
        state = ST_ERROR;
      }
      foundSavedDevice = 0;
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
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
  xTaskCreate( &BLE_task, "Bluetooth", 100000, NULL, 1, &bleTaskHandle );
  xTaskCreate( &OUT_task, "Pulses",    10000,  NULL, 2, &outTaskHandle );
  xTaskCreate( &LED_task, "Blink",     10000,  NULL, 3, &ledTaskHandle );
  xTaskCreate( &KEY_task, "Key",       10000,   NULL, 4, &keyTaskHandle );
}

void loop() {
}
