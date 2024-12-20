// ESP32-C3-Zero:        USE_RGB_LEDS 1,  LED_PIN  10
// ESP32-C3-SuperMini:   USE_RGB_LEDS 0,  LED_PIN  8
#define  USE_RGB_LEDS   0
#define  LED_PIN        8

// How soon module turns off, if heart monitor not found (seconds)
#define SLEEP_TIMEOUT 60

// Set to 0 to bind with a key to available device (normal)
// Set to 1 to hard-code heart monitor MAC address
#define  USE_FIXED_MAC_ADDRESS   0   
// Use lowercase letters in address
#define FIXED_MAC_ADDRESS "01:23:45:67:89:ab"
