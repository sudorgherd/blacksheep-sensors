# Changelog

All notable changes to PROJECT BLACKSHEEP Sensors will be documented here.

## Unreleased

### Added
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
- C5 #1 2.4 GHz Wi-Fi initialization and RF operation physically validated
- Development version advanced to `0.1.0-dev`; v0.1.0 remains in progress
- Project status updated after both ESP32-C5 boards were received and breadboarded
- Initial build, upload, boot, PSRAM, serial diagnostics, and heartbeat verified on both ESP32-C5 boards
- PlatformIO/SCons bootloader regeneration failure isolated and bypassed without modifying global packages
