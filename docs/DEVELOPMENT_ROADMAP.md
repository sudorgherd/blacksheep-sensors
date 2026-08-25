# PROJECT BLACKSHEEP Sensors — Versioned Development Roadmap

**Status:** Early development; v0.1.0 in progress
**Repository:** `sudorgherd/blacksheep-sensors`
**Initial hardware:** 2× ESP32-C5 development boards
**Target integration point:** ARGUS REDLINE v1.0

## Development Model

BLACKSHEEP sensor development begins before REDLINE v1.0 and proceeds independently where possible.

The initial sensor platform uses two dedicated ESP32-C5 processors:

* **C5 #1:** 2.4 GHz Wi-Fi observation
* **C5 #2:** 5 GHz Wi-Fi observation

The two-radio design allows simultaneous observation of both Wi-Fi bands without requiring a single radio to alternate between 2.4 GHz and 5 GHz.

Sensor development will proceed alongside **ARGUS REDLINE v0.6.0 through v1.0 development**.

Following the REDLINE v1.0 release, the C5 sensors will be integrated with a separate BLACKSHEEP control-board implementation based on Heltec ESP32-S3 / SX1262 hardware. The control board will coordinate the sensors, add BLE and GNSS context, and interface with REDLINE transport.

## v0.1.0 — C5 Hardware Bring-Up

**Status:** In progress

Completed preparation:

* repository/bootstrap preparation
* hardware received
* both development boards mounted on breadboards

Completed bring-up work:

* reproducible ESP32-C5 PlatformIO/ESP-IDF toolchain established
* basic ESP32-C5 firmware build verified
* correct ESP32-C5-WROOM-1U-N32R8 board/module configuration established
* project-local native ESP-IDF CMake bootloader selection and regression guard
  established
* serial programming verified on both boards
* boot and serial diagnostics verified on both boards
* 32 MB flash and 8 MB PSRAM initialization verified on both boards
* 2.4 GHz Wi-Fi initialization and bounded infrastructure scan verified on C5 #1

PlatformIO's ESP32-C5 SCons integration regenerated the native ESP-IDF
bootloader and produced an artifact that failed during ROM startup. Selecting
the authoritative native ESP-IDF CMake bootloader resolved the failure. Both
boards now reach the second-stage bootloader, pass the PSRAM test, print the
BLACKSHEEP READY banner, and maintain a stable heartbeat. See
[`V0.1.0_C5_BRINGUP.md`](V0.1.0_C5_BRINGUP.md) for the evidence and workaround.

The 5 GHz, passive-observation, channel-control, and metadata-characterization
work remains pending for v0.1.0.

Establish the initial ESP32-C5 development environment and verify both boards independently.

Planned work:

* confirm board configuration and toolchain
* verify serial programming and debugging
* validate 2.4 GHz operation — complete
* validate 5 GHz operation
* confirm promiscuous/passive Wi-Fi observation capability
* establish basic channel-selection and channel-hopping control
* characterize available metadata exposed by the ESP32-C5 Wi-Fi subsystem

No REDLINE integration is required for this milestone.

## v0.2.0 — Raw Sensor Attribute Discovery

Determine which observable Wi-Fi attributes are useful and reliably available from the C5 hardware.

Initial target attributes include:

* MAC address fields
* OUI / manufacturer information
* RSSI
* frequency / channel
* Wi-Fi band
* frame type and subtype
* frame length
* timestamps
* sequence information
* retry state
* protected/encrypted-frame indication
* PHY/rate metadata where available
* activity and timing characteristics

The purpose of this milestone is discovery and characterization. The final BLACKSHEEP sensor record is not expected to be frozen yet.

## v0.3.0 — Sensor Observation Model

Define the first structured BLACKSHEEP Wi-Fi observation model.

Planned work:

* distinguish address roles such as transmitter, receiver, source, destination, and BSSID where applicable
* identify globally administered versus locally administered/randomized MAC addresses
* derive OUI/vendor information where valid
* establish RSSI and activity aggregation behavior
* define channel and band metadata
* define frame-profile and timing-profile fields
* determine which raw observations are processed transiently versus retained in summaries
* define bounded machine-readable sensor output

The C5 sensors should reduce high-volume RF observations into useful metadata rather than forwarding raw packet captures.

## v0.4.0 — Dual-Sensor Operation

Operate both C5 sensors simultaneously as one BLACKSHEEP sensing subsystem.

Planned configuration:

* C5 #1 dedicated to 2.4 GHz
* C5 #2 dedicated to 5 GHz
* independent channel hopping within each assigned band
* simultaneous observation windows
* consistent sensor timestamps and observation sequencing
* bounded output suitable for a future control-board interface

This milestone establishes the first complete dual-band BLACKSHEEP Wi-Fi sensor.

## v0.5.0 — Local Aggregation and Feature Output

Move from raw metadata reporting toward compact BLACKSHEEP feature summaries.

Planned work:

* observation windows
* RSSI summaries
* activity counts
* channel-use summaries
* frame-profile summaries
* manufacturer/OUI features
* timing characteristics
* short-lived or rotating cluster features where appropriate
* output rate and buffer limits

The objective is to produce data suitable for constrained transport without placing BLACKSHEEP correlation or mapping logic on the sensor processors.

## v0.6.0 — Control-Board Interface Preparation

Define the sensor-side interface expected by the future BLACKSHEEP control board.

Planned work:

* select UART, SPI, or other suitable local interface
* define framing and message boundaries
* define sensor identification and band-role reporting
* define observation/result messages
* define health and diagnostic reporting
* establish bounded buffering and backpressure behavior
* document electrical and logical interface requirements

This milestone remains independent of REDLINE transport implementation.

## v0.7.0 — Sensor Qualification

Qualify the dual-C5 sensor subsystem before control-board integration.

Planned testing:

* extended simultaneous 2.4/5 GHz operation
* channel-hopping behavior
* observation consistency
* RSSI behavior
* antenna placement/orientation testing
* buffer and sustained-load behavior
* restart/recovery behavior
* malformed/internal-message handling
* long-duration stability

The initial development boards use external dual-band Wi-Fi antennas, allowing antenna behavior to be characterized before final mobile hardware is designed.

## v1.0.0 — BLACKSHEEP Sensor Interface Candidate

**Target:** coincide with readiness for post-REDLINE-v1.0 integration.

Establish the first stable sensor-side interface suitable for integration with the BLACKSHEEP control board.

Expected state:

* dual C5 operation proven
* 2.4 GHz and 5 GHz roles defined
* sensor attributes characterized
* bounded observation model defined
* local aggregation implemented
* control-board interface documented
* sensor diagnostics available
* hardware behavior qualified sufficiently for integration work

## Post-v1.0 Direction — BLACKSHEEP Control Integration

Following the **ARGUS REDLINE v1.0 release**, development moves into the separate BLACKSHEEP control-board layer.

Expected architecture:

```text
ESP32-C5 #1
2.4 GHz Wi-Fi sensor
        │
        ├──────────┐
        │          │
ESP32-C5 #2        │
5 GHz Wi-Fi sensor │
        │          │
        └──────► BLACKSHEEP CONTROL BOARD
                 Heltec ESP32-S3 / SX1262
                 - sensor coordination
                 - BLE observation
                 - GNSS position/time
                 - local buffering
                 - REDLINE integration
                          │
                          ▼
                    ARGUS REDLINE
```

GNSS is expected to support mobile deployment and spatial correlation/heat-map generation, while BLE provides an additional local RF sensing domain.

Additional sensing domains may be added later without requiring redesign of the core dual-C5 Wi-Fi sensor architecture.
