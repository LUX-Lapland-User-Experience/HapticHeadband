/**
 * HEADBAND – BLE Central (button) + Dual Haptic + SD logging + NeoPixel status
 * Hardware: Seeed Studio XIAO nRF52840 Sense (mbed-enabled board package)
 * Haptic:   2x Adafruit DRV2605L
 *             - Motor #1: Wire  (D4=SDA, D5=SCL) at 0x5A
 *             - Motor #2: Wire1 (D1=SDA, D2=SCL) at 0x5A
 * SD Card:  Adafruit 254 via SPI
 * NeoPixel: Single pixel on D6
 *
 * Wiring:
 *   XIAO 3.3V  →  both DRV2605L VIN
 *   XIAO GND   →  both DRV2605L GND
 *   XIAO D4    →  DRV2605L #1 SDA
 *   XIAO D5    →  DRV2605L #1 SCL
 *   XIAO D1    →  DRV2605L #2 SDA
 *   XIAO D2    →  DRV2605L #2 SCL
 *   XIAO 3.3V  →  SD card 5V
 *   XIAO GND   →  SD card GND
 *   XIAO D8    →  SD card CLK
 *   XIAO D9    →  SD card DO
 *   XIAO D10   →  SD card DI
 *   XIAO D7    →  SD card CS
 *   XIAO D3    →  Session button (other leg to GND)
 *   XIAO 3.3V  →  NeoPixel 5V
 *   XIAO GND   →  NeoPixel GND
 *   XIAO D6    →  NeoPixel Din
 *
 * Session button (D3):
 *   Press while IDLE    →  start session (blue → green)
 *   Press while RUNNING →  end session, write to SD (green → blue)
 *
 * NeoPixel states:
 *   Blue   →  idle, waiting for session
 *   Green  →  session running
 *   Red    →  error
 *   Purple →  writing to SD
 */

#include <ArduinoBLE.h>
#include <Wire.h>
#include <Adafruit_DRV2605.h>
#include <SD.h>
#include <SPI.h>
#include <Adafruit_NeoPixel.h>

// Second I2C bus on D1/D2 for DRV2605L #2
MbedI2C Wire2(D1, D2);

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

static const char* BUTTON_SERVICE_UUID   = "12345678-1234-1234-1234-123456789abc";
static const char* BUTTON_CHAR_UUID      = "12345678-1234-1234-1234-123456789abd";
static const char* BUTTON_DEVICE_NAME    = "HapticButton";

static const uint8_t  HAPTIC_EFFECT      = 14;

static const unsigned long VIB_MIN_MS    = 5000;    //Random event min. time
static const unsigned long VIB_MAX_MS    = 15000;   //Randon event max. time

static const unsigned long DETECTION_MS  = 5000;
static const unsigned long BLE_SCAN_MS   = 5000;

static const int SD_CS_PIN               = D7;
static const char* CSV_FILENAME          = "data.csv";

static const int SESSION_BUTTON_PIN      = D3;
static const unsigned long DEBOUNCE_MS   = 20;

bool motor1Enabled = true;
bool motor2Enabled = true;

// ---------------------------------------------------------------------------
// NeoPixel
// ---------------------------------------------------------------------------

#define NEOPIXEL_PIN        D6
#define NEOPIXEL_COUNT      1
#define NEOPIXEL_BRIGHTNESS 80

Adafruit_NeoPixel pixel(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

void setPixel(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}

void pixelIdle()    { setPixel(0, 0, 255);    }  // Blue   — waiting
void pixelRunning() { setPixel(0, 255, 0);    }  // Green  — session active
void pixelError()   { setPixel(255, 0, 0);    }  // Red    — error
void pixelWriting() { setPixel(128, 0, 128);  }  // Purple — writing to SD

// ---------------------------------------------------------------------------
// DRV2605L instances
// ---------------------------------------------------------------------------

Adafruit_DRV2605 drv1;
Adafruit_DRV2605 drv2;

bool drv1Ok = false;
bool drv2Ok = false;

bool initDriver(Adafruit_DRV2605 &drv, TwoWire &wire, const char* label) {
  if (!drv.begin(&wire)) {
    Serial.print("[ERROR] DRV2605L ");
    Serial.print(label);
    Serial.println(" not found — check wiring.");
    return false;
  }
  drv.selectLibrary(1);
  drv.setMode(DRV2605_MODE_INTTRIG);
  Serial.print("[DRV2605L] ");
  Serial.print(label);
  Serial.println(" OK");
  return true;
}

// ---------------------------------------------------------------------------
// Session state
// ---------------------------------------------------------------------------

enum State { IDLE, RUNNING };
State   state         = IDLE;
uint8_t participantNr = 0;
uint8_t vibrationNr   = 0;

unsigned long nextVibrationMs  = 0;
unsigned long lastVibrationMs  = 0;
bool          vibrationPending = false;

// ---------------------------------------------------------------------------
// In-memory event log
// ---------------------------------------------------------------------------

struct VibrationEvent {
  uint8_t vibrationNr;
  char    result[16];  // "yes", "no", "false_positive"
};

static const int MAX_EVENTS = 200;
VibrationEvent   sessionEvents[MAX_EVENTS];
int              sessionEventCount = 0;

void clearSessionEvents() {
  sessionEventCount = 0;
}

void logEvent(uint8_t vibNr, const char* result) {
  if (sessionEventCount >= MAX_EVENTS) return;
  sessionEvents[sessionEventCount].vibrationNr = vibNr;
  strncpy(sessionEvents[sessionEventCount].result, result, 15);
  sessionEventCount++;

  Serial.print("[LOG] P");
  Serial.print(participantNr);
  Serial.print(" V");
  Serial.print(vibNr);
  Serial.print(" ");
  Serial.println(result);
}

// ---------------------------------------------------------------------------
// SD card
// ---------------------------------------------------------------------------

bool sdOk = false;

// Read last participant number from CSV to continue numbering correctly
uint8_t readLastParticipantNr() {
  if (!SD.exists(CSV_FILENAME)) return 0;

  File f = SD.open(CSV_FILENAME, O_READ);
  if (!f) return 0;

  uint8_t lastNr = 0;
  char line[32];
  int  lineLen = 0;
  bool firstLine = true;

  while (f.available()) {
    char c = f.read();
    if (c == '\n') {
      if (!firstLine && lineLen > 0) {
        line[lineLen] = '\0';
        uint8_t nr = atoi(line);
        if (nr > lastNr) lastNr = nr;
      }
      firstLine = false;
      lineLen   = 0;
    } else if (c != '\r' && lineLen < 31) {
      line[lineLen++] = c;
    }
  }
  f.close();
  return lastNr;
}

void initSD() {
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("[ERROR] SD card not found — check wiring.");
    sdOk = false;
    pixelError();
    return;
  }
  sdOk = true;
  Serial.println("[SD] OK");

  // Write header if file doesn't exist
  if (!SD.exists(CSV_FILENAME)) {
    File f = SD.open(CSV_FILENAME, O_CREAT | O_WRITE);
    if (f) {
      f.println("participant,vibration_nr,detected");
      f.close();
    }
  }

  // Continue participant numbering from last session
  participantNr = readLastParticipantNr();
  Serial.print("[SD] Last participant: ");
  Serial.println(participantNr);
}

void writeSessionToSD() {
  if (!sdOk) {
    Serial.println("[ERROR] SD not available.");
    pixelError();
    return;
  }

  pixelWriting();

  File f = SD.open(CSV_FILENAME, O_CREAT | O_APPEND | O_WRITE);
  if (!f) {
    Serial.println("[ERROR] Could not open data.csv.");
    pixelError();
    return;
  }

  for (int i = 0; i < sessionEventCount; i++) {
    f.print(participantNr);
    f.print(",");
    f.print(sessionEvents[i].vibrationNr);
    f.print(",");
    f.println(sessionEvents[i].result);
  }
  f.close();

  Serial.print("[SD] Written ");
  Serial.print(sessionEventCount);
  Serial.println(" events to SD.");
}

// ---------------------------------------------------------------------------
// Haptic
// ---------------------------------------------------------------------------

void fireMotor1() {
  if (!motor1Enabled || !drv1Ok) return;
  drv1.setWaveform(0, HAPTIC_EFFECT);
  drv1.setWaveform(1, 0);
  drv1.go();
}

void fireMotor2() {
  if (!motor2Enabled || !drv2Ok) return;
  drv2.setWaveform(0, HAPTIC_EFFECT);
  drv2.setWaveform(1, 0);
  drv2.go();
}

void triggerVibration() {
  fireMotor1();
  fireMotor2();

  lastVibrationMs  = millis();
  vibrationPending = true;
  vibrationNr++;

  Serial.print("[VIBRATION] P");
  Serial.print(participantNr);
  Serial.print(" V");
  Serial.println(vibrationNr);
}

unsigned long randomVibrationInterval() {
  return (unsigned long)random(VIB_MIN_MS, VIB_MAX_MS + 1);
}

void checkVibrationTimeout() {
  if (!vibrationPending) return;
  if (millis() - lastVibrationMs >= DETECTION_MS) {
    Serial.println("[TIMER] Not detected.");
    logEvent(vibrationNr, "no");
    vibrationPending = false;
  }
}

// ---------------------------------------------------------------------------
// Session control
// ---------------------------------------------------------------------------

void startSession() {
  participantNr++;
  vibrationNr      = 0;
  vibrationPending = false;
  state            = RUNNING;
  nextVibrationMs  = millis() + randomVibrationInterval();

  clearSessionEvents();
  pixelRunning();

  Serial.print("[SESSION] Participant ");
  Serial.print(participantNr);
  Serial.println(" started.");
}

void endSession() {
  if (vibrationPending) {
    logEvent(vibrationNr, "no");
    vibrationPending = false;
  }

  Serial.print("[SESSION] Participant ");
  Serial.print(participantNr);
  Serial.println(" ended. Writing to SD...");

  writeSessionToSD();

  state = IDLE;
  pixelIdle();
}

// ---------------------------------------------------------------------------
// Session button handling
// ---------------------------------------------------------------------------

bool          lastButtonReading = HIGH;
bool          buttonState       = HIGH;
unsigned long lastDebounceMs    = 0;

void handleSessionButton() {
  bool reading = digitalRead(SESSION_BUTTON_PIN);

  if (reading != lastButtonReading) lastDebounceMs = millis();
  lastButtonReading = reading;

  if (millis() - lastDebounceMs < DEBOUNCE_MS) return;

  if (reading == HIGH && buttonState == LOW) {
    buttonState = HIGH;

    if (state == IDLE) {
      Serial.println("[SESSION BUTTON] Start session.");
      startSession();
    } else if (state == RUNNING) {
      Serial.println("[SESSION BUTTON] End session.");
      endSession();
    }
  }

  if (reading == LOW) buttonState = LOW;
}

// ---------------------------------------------------------------------------
// Response button press handler
// ---------------------------------------------------------------------------

void onButtonPress() {
  if (state != RUNNING) return;

  if (vibrationPending) {
    unsigned long reactionMs = millis() - lastVibrationMs;
    if (reactionMs <= DETECTION_MS) {
      Serial.println("[BUTTON] Detected.");
      logEvent(vibrationNr, "yes");
      vibrationPending = false;
    } else {
      Serial.println("[BUTTON] Press too late — ignored.");
    }
  } else {
    Serial.println("[BUTTON] False positive.");
    logEvent(vibrationNr, "false_positive");
  }
}

// ---------------------------------------------------------------------------
// BLE — button scanning (non-blocking)
// ---------------------------------------------------------------------------

BLEDevice         buttonPeripheral;
BLECharacteristic buttonCharacteristic;
bool              bleButtonConnected = false;
unsigned long     nextScanMs         = 0;
bool              scanning           = false;

void startScan() {
  Serial.println("[BLE] Scanning for button device...");
  BLE.scanForUuid(BUTTON_SERVICE_UUID);
  scanning = true;
}

void stopScan() {
  BLE.stopScan();
  scanning = false;
}

void handleButtonScanning() {
  if (!scanning && millis() >= nextScanMs) {
    startScan();
    return;
  }
  if (!scanning) return;

  BLEDevice peripheral = BLE.available();
  if (!peripheral) return;
  if (peripheral.localName() != BUTTON_DEVICE_NAME) return;

  stopScan();
  Serial.println("[BLE] Button device found. Connecting...");

  if (!peripheral.connect()) {
    Serial.println("[BLE] Connection failed — will retry.");
    nextScanMs = millis() + BLE_SCAN_MS;
    return;
  }

  if (!peripheral.discoverAttributes()) {
    Serial.println("[BLE] Attribute discovery failed.");
    peripheral.disconnect();
    nextScanMs = millis() + BLE_SCAN_MS;
    return;
  }

  BLECharacteristic ch = peripheral.characteristic(BUTTON_CHAR_UUID);
  if (!ch || !ch.canSubscribe()) {
    Serial.println("[BLE] Characteristic not found.");
    peripheral.disconnect();
    nextScanMs = millis() + BLE_SCAN_MS;
    return;
  }

  ch.subscribe();
  buttonPeripheral     = peripheral;
  buttonCharacteristic = ch;
  bleButtonConnected   = true;
  Serial.println("[BLE] Button device connected.");
}

// ---------------------------------------------------------------------------
// BLE — button connection handler
// ---------------------------------------------------------------------------

void handleButtonConnection() {
  if (!buttonPeripheral.connected()) {
    Serial.println("[BLE] Button device disconnected.");
    bleButtonConnected = false;
    nextScanMs         = millis() + BLE_SCAN_MS;
    return;
  }

  if (buttonCharacteristic.valueUpdated()) {
    uint8_t val = buttonCharacteristic.value()[0];
    Serial.print("[BLE] Raw value received: ");
    Serial.println(val);
    if (val == 1) onButtonPress();
  }
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("=== HEADBAND starting ===");

  // NeoPixel
  pixel.begin();
  pixel.setBrightness(NEOPIXEL_BRIGHTNESS);
  pixelError();  // Red during init

  // Session button
  pinMode(SESSION_BUTTON_PIN, INPUT_PULLUP);

  randomSeed(analogRead(A0));

  // I2C buses
  Wire.begin();
  Wire2.begin();

  drv1Ok = initDriver(drv1, Wire,  "#1 (D4/D5)");
  drv2Ok = initDriver(drv2, Wire2, "#2 (D1/D2)");

  // SD card — also reads last participant number
  initSD();

  // BLE
  if (!BLE.begin()) {
    Serial.println("[ERROR] BLE failed to start!");
    pixelError();
    while (true) {}
  }
  Serial.println("[BLE] OK");

  nextScanMs = millis() + 1000;

  pixelIdle();
  Serial.println("[INIT] Ready. Press session button to start.");
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------

void loop() {
  handleSessionButton();

  if (bleButtonConnected) {
    handleButtonConnection();
  } else {
    handleButtonScanning();
  }

  if (state == RUNNING) {
    checkVibrationTimeout();
    if (!vibrationPending && millis() >= nextVibrationMs) {
      triggerVibration();
      nextVibrationMs = millis() + randomVibrationInterval();
    }
  }
}