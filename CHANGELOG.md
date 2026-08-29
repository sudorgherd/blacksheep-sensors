# Changelog

All notable changes to PROJECT BLACKSHEEP Sensors will be documented here.

## Unreleased

### Added
- v0.2.0 Stage 8 completed with fail-closed OUI/registry eligibility,
  deterministic variable-prefix host feasibility tests, aggregate-only
  dual-band evidence, and a future host/control lookup decision that rejects
  manufacturer and identity claims
- v0.2.0 Stage 7 completed with fail-closed public ESP32-C5 PHY-format
  normalization, conditional rate/raw-SIG scalar handling, both-band physical
  evidence, and no private ESP-IDF dependencies or raw-SIG decoder
- v0.2.0 Stage 6 completed with local receive-timing characterization,
  explicit epochs, exact 32-bit wrap arithmetic, drop-aware adjacency, bounded
  worker-side timing aggregates, and controlled-Beacon periodic validation
- v0.2.0 Stage 5 provisionally checkpointed with pure supported-channel/frequency
  mapping, bounded worker-side RF aggregation, validation-only controlled-Beacon
  selection, and controlled dual-band residential characterization; original
  physical qualification remains open for Stage 9 integrated closure
- v0.2.0 Stage 4 completed with bounded Sequence Control extraction and exact
  Retry, More Fragments, and Protected protocol-fact characterization
- v0.2.0 Stage 3 completed with bounded raw address extraction, protocol-defined
  management/control/data roles, complete DS mapping, local/global/group
  classification, and dual-band driver group-bit comparison
- v0.2.0 Stage 2 completed with subtype-aware management/control/data layout
  descriptors, QoS/Addr4/conditional HT Control handling, fail-closed Block Ack
  variants, and measured callback-class agreement on both C5 bands
- v0.2.0 Stage 1 completed with a host-testable, fail-closed 802.11
  frame-control/layout parser, a bounded 40-byte capture-event contract, and a
  fixed 128-entry non-blocking capture queue validated under controlled quiet,
  representative, stress, and known-length traffic on both C5 roles
- Authoritative staged implementation brief for v0.2.0 raw sensor attribute
  discovery, including parser safety, privacy, test, risk, and acceptance gates

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
