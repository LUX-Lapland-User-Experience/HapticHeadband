# Haptic Headband System
## Technical Documentation & User Guide

---

## 1. System Overview

The haptic headband system is designed to test whether users notice haptic vibration signals in different real-world scenarios. The system consists of two wireless devices that communicate over Bluetooth Low Energy (BLE).

- Headband device — worn by the participant, produces random haptic vibrations
- Button device — held by the participant, used to register when a vibration is noticed

The researcher controls sessions using a physical button on the headband box and monitors status via a NeoPixel LED. All data is stored on an SD card and exported as a CSV file for analysis in Excel.

---

## 2. Hardware

### 2.1 Headband Device

Components:
- Seeed Studio XIAO nRF52840 Sense — main microcontroller
- 2x Adafruit DRV2605L haptic driver — drives the vibration motors
- 2x Coin vibration motors
- Adafruit 254 SD card breakout — stores session data
- Single NeoPixel LED — shows device status
- Momentary push button — controls session start/stop
- 1S LiPo battery (~250 mAh)

Wiring:

    XIAO 3.3V   →  DRV2605L #1 & #2  VIN
    XIAO GND    →  DRV2605L #1 & #2  GND
    XIAO D4     →  DRV2605L #1       SDA
    XIAO D5     →  DRV2605L #1       SCL
    XIAO D1     →  DRV2605L #2       SDA
    XIAO D2     →  DRV2605L #2       SCL
    XIAO 3.3V   →  SD Card           5V
    XIAO GND    →  SD Card           GND
    XIAO D8     →  SD Card           CLK
    XIAO D9     →  SD Card           DO (MISO)
    XIAO D10    →  SD Card           DI (MOSI)
    XIAO D7     →  SD Card           CS
    XIAO D3     →  Session Button    One leg
    XIAO GND    →  Session Button    Other leg
    XIAO 3.3V   →  NeoPixel          5V
    XIAO GND    →  NeoPixel          GND
    XIAO D6     →  NeoPixel          Din

---

### 2.2 Button Device

Components:
- Seeed Studio XIAO nRF52840 Sense — main microcontroller
- Arcade-style button with built-in LED (mechanical keyboard switch)
- 1S LiPo battery (~250 mAh)

Wiring:

    XIAO 3.3V   →  Button  Pin 1 (LED+)
    XIAO D0     →  Button  Pin 2 (SW signal)
    XIAO GND    →  Button  Pin 3 (GND / LED-)

---

### 2.3 Battery Life

- Headband device — approximately 16 hours
- Button device — approximately 20 hours

Both devices charge automatically when connected via USB. The orange CHG LED on the XIAO illuminates during charging and turns off when full.

---

## 3. Software

### 3.1 Arduino Setup

Board package:  Seeed nRF52 mbed-enabled Boards
Board:          Seeed XIAO nRF52840 Sense

Required libraries:
- ArduinoBLE
- Adafruit DRV2605
- Adafruit NeoPixel
- SD

### 3.2 BLE Architecture

The headband acts as a BLE central device — it scans for and connects to the button peripheral. The button device advertises as "HapticButton" and sends a notification whenever the button is pressed.

    Button service UUID:        12345678-1234-1234-1234-123456789abc
    Button characteristic UUID: 12345678-1234-1234-1234-123456789abd

### 3.3 Key Parameters

These values can be adjusted in the configuration section of headband.ino:

    VIB_MIN_MS      60000       Minimum interval between vibrations (1 min)
    VIB_MAX_MS      120000      Maximum interval between vibrations (2 min)
    DETECTION_MS    5000        Time window to register a press after vibration (5 sec)
    HAPTIC_EFFECT   14          DRV2605L effect number (1-123)
    MAX_EVENTS      200         Maximum events stored per session

---

## 4. Operation

### 4.1 NeoPixel Status LED

    Red     Error / Init    Device is starting up or an error occurred
    Blue    Idle            Ready and waiting — no session active
    Green   Running         Session is active, vibrations are firing
    Purple  Writing         Writing session data to SD card

### 4.2 Running a Test Session

1. Power on both devices — headband LED turns blue when ready
2. Fit the headband on the participant and hand them the button device
3. Press the session button on the headband box — LED turns green, session starts
4. The headband vibrates at random intervals (1-2 minutes)
5. Participant presses the button whenever they notice a vibration
6. When the session is complete, press the session button again — LED turns
   purple while writing to SD, then blue when done
7. Repeat from step 2 for the next participant

### 4.3 Error Handling

If the NeoPixel turns red during a session end or data write, the SD card write failed. Do not power off the device. Check that the SD card is properly inserted and try ending the session again. Data is held in memory until successfully written.

---

## 5. Data Collection

### 5.1 SD Card Setup

Format the SD card as FAT32 before first use. The device automatically creates data.csv with the correct header on first boot.

Participant numbers are read from the existing CSV on startup, so numbering continues correctly even after powering off between participants.

### 5.2 CSV Format

File name: data.csv

    participant     1, 2, 3...                  Participant number, auto-incremented
    vibration_nr    1, 2, 3...                  Vibration number within the session
    detected        yes / no / false_positive   Whether the vibration was noticed

A false_positive entry means the participant pressed the button when no vibration was active.

Example output:

    participant,vibration_nr,detected
    1,1,no
    1,2,yes
    1,3,yes
    1,4,false_positive
    1,5,no
    2,1,yes
    2,2,yes

### 5.3 Retrieving Data

1. Power off the headband device
2. Remove the SD card from the Adafruit 254 breakout
3. Insert into a computer using an SD card reader
4. Open data.csv directly in Excel or any spreadsheet application

---

## 6. Troubleshooting

    NeoPixel stays red            SD card not found           Check SD card is inserted and formatted as FAT32
    Button presses not registering BLE not connected          Check button device is powered on and LED is lit
    No vibrations firing          DRV2605L not found          Check I2C wiring on D4/D5 and D1/D2
    Session button not responding Wrong pin or debounce       Check button is wired to D3 and GND
    Devices not connecting        BLE UUID mismatch           Ensure both sketches use identical UUIDs
    Data not saved                SD write failed             Check SD card — LED turns red on write failure

---

## 7. Firmware Files

- headband.ino — uploaded to the headband XIAO nRF52840 Sense
- button_peripheral.ino — uploaded to the button device XIAO nRF52840 Sense

Both sketches require the Seeed nRF52 mbed-enabled board package. Select Seeed XIAO nRF52840 Sense as the target board before uploading.
