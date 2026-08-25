# Changelog

All notable changes to PROJECT BLACKSHEEP Sensors will be documented here.

## Unreleased

## v0.1.0 — 2026-08-25

### Added
- ESP-IDF 5.5.4 ESP32-C5 direct promiscuous receive-metadata inventory with
  bounded physical evidence on both assigned bands
- Dedicated 2.4 GHz and 5 GHz metadata-characterization firmware environments
- Bounded deterministic channel-selection and channel-hopping validation for
  both C5 band roles
- Bounded fixed-channel passive/promiscuous receive validation on both C5 band roles
- Bounded non-DFS 5 GHz infrastructure-scan validation for C5 #2
- Bounded one-shot 2.4 GHz infrastructure-scan validation for C5 #1
- Initial ESP32-C5 firmware/toolchain bring-up baseline
- Bounded boot diagnostics and low-rate serial heartbeat firmware
- Project-local ESP32-C5 native CMake bootloader selection hook and regression guard
- Initial public repository
- Project README
- Repository structure
- Contribution guidance
- Security policy
- AGPL-3.0 license

### Changed
- v0.1.0 C5 Hardware Bring-Up roadmap work completed and prepared for release
- ESP32-C5 channel set/get control, passive callback continuity across channel
  transitions, and clean shutdown physically validated on both C5 boards
- ESP32-C5 promiscuous receive callbacks and clean shutdown physically validated
  on C5 #1 at 2.4 GHz and C5 #2 at 5 GHz
- C5 #2 5 GHz Wi-Fi initialization and RF operation physically validated
- C5 #1 2.4 GHz Wi-Fi initialization and RF operation physically validated
- Release version finalized as `0.1.0`
- Project status updated after both ESP32-C5 boards were received and breadboarded
- Initial build, upload, boot, PSRAM, serial diagnostics, and heartbeat verified on both ESP32-C5 boards
- PlatformIO/SCons bootloader regeneration failure isolated and bypassed without modifying global packages
