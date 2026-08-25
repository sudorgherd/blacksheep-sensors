# BLACKSHEEP Sensors

This repository contains the sensor-side firmware and development work for **PROJECT BLACKSHEEP**, a mobile passive RF-sensing system intended to observe the surrounding wireless environment and reduce it into compact signal features for later correlation and mapping.

## Current Status

**Early development — v0.1.0 C5 hardware bring-up in progress.**

Repository preparation is complete. Both ESP32-C5 development boards have been
received and mounted on breadboards; firmware/toolchain bring-up is starting.

Initial hardware development uses two ESP32-C5 boards:

* one dedicated to **2.4 GHz Wi-Fi observation**
* one dedicated to **5 GHz Wi-Fi observation**

Using separate radios allows both Wi-Fi bands to be observed during the same time window without requiring a single radio to alternate between bands.

The first development phase will focus on determining which sensor attributes are useful and reliable, including:

* MAC address / OUI information for manufacturer identification
* RSSI
* frequency and channel
* 802.11 frame type/subtype
* timing and activity patterns
* PHY and frame metadata where available
* local aggregation and feature extraction

The sensors are intended to process RF observations locally and eventually provide bounded, machine-readable summaries to a BLACKSHEEP control board.

## Relationship to ARGUS REDLINE

Sensor development will proceed alongside **ARGUS REDLINE v0.6.0 through v1.0 development**.

The ESP32-C5 sensor firmware can be developed and characterized independently while REDLINE finishes its core transport platform.

Following the **REDLINE v1.0 release**, BLACKSHEEP development will move into control-board integration using Heltec ESP32-S3 / SX1262 hardware, GNSS and BLE sensing, and REDLINE transport.

Planned high-level architecture:

```text
ESP32-C5 — 2.4 GHz sensor ─┐
                           ├── BLACKSHEEP control board ── ARGUS REDLINE
ESP32-C5 — 5 GHz sensor ───┘
```

Additional sensing domains may be added later without requiring the Wi-Fi sensor firmware to become application-specific.

## Development Stage

The repository/bootstrap preparation is complete, and initial ESP32-C5 board
bring-up is underway. Wi-Fi sensing and hardware validation results will be
documented only as they are completed.

Expect rapid changes while the initial ESP32-C5 hardware is characterized.

**PROJECT BLACKSHEEP is under active development.**
