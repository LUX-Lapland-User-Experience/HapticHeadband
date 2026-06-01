/**
 * BUTTON PERIPHERAL – BLE Peripheral + Arcade Button with LED
 * Hardware: Seeed Studio XIAO nRF52840 Sense (mbed-enabled board package)
 *
 * Wiring:
 *   Button Pin 1 (LED+ / SW common)  →  XIAO 3.3V
 *   Button Pin 2 (SW signal)         →  XIAO D0
 *   Button Pin 3 (LED-)              →  XIAO GND
 */

#include <ArduinoBLE.h>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

static const char* BUTTON_SERVICE_UUID  = "12345678-1234-1234-1234-123456789abc";
static const char* BUTTON_CHAR_UUID     = "12345678-1234-1234-1234-123456789abd";
static const char* DEVICE_NAME          = "HapticButton";

static const int          BUTTON_PIN    = D0;
static const unsigned long DEBOUNCE_MS  = 15;

// ---------------------------------------------------------------------------
// BLE
// ---------------------------------------------------------------------------

BLEService            buttonService(BUTTON_SERVICE_UUID);
BLEByteCharacteristic buttonCharacteristic(BUTTON_CHAR_UUID, BLENotify);

// ---------------------------------------------------------------------------
// Button debounce
// ---------------------------------------------------------------------------

bool          lastReading    = LOW;
bool          buttonState    = LOW;
unsigned long lastDebounceMs = 0;

bool buttonPressed() {
  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastReading) lastDebounceMs = millis();
  lastReading = reading;

  if (millis() - lastDebounceMs >= DEBOUNCE_MS) {
    if (reading == HIGH && buttonState == LOW) {
      buttonState = HIGH;
      return true;
    }
    if (reading == LOW) buttonState = LOW;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("=== BUTTON PERIPHERAL starting ===");

  pinMode(BUTTON_PIN, INPUT_PULLDOWN);  // Pull LOW when not pressed

  if (!BLE.begin()) {
    Serial.println("[ERROR] BLE failed to start!");
    while (true) {}
  }

  BLE.setLocalName(DEVICE_NAME);
  BLE.setAdvertisedService(buttonService);
  buttonService.addCharacteristic(buttonCharacteristic);
  BLE.addService(buttonService);
  buttonCharacteristic.writeValue(0);
  BLE.advertise();

  Serial.println("[BLE] Advertising as \"HapticButton\"");
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------

void loop() {
  BLEDevice central = BLE.central();

  if (central) {
    Serial.print("[BLE] Connected: ");
    Serial.println(central.address());

    while (central.connected()) {
      if (buttonPressed()) {
        buttonCharacteristic.writeValue(1);
        Serial.println("[BUTTON] Press sent.");
      }
    }

    Serial.println("[BLE] Disconnected — advertising again.");
    BLE.advertise();
  }
}