# Source provenance

Primary input: `nightscout-core2-release-2026-11-04.zip` from the referenced chat.
SHA-256: `fd3b1b1f1565a09f2bbbd6593ff9f7b1185a681363a1fde68c16768986f37be1`.
The supplied archive, extracted reference and upstream checkout were read-only inputs.

Public repository: https://github.com/stavlor/M5Stack-core2-Nightscout
HEAD inspected: `2dde5643de2c412cb4ff93c2bd18070ebcc2e036` (Release update).
The supplied archive is authoritative when files differ. Comparison of common files, ignoring CRLF/LF differences, found these differences: none.

Retained: model/API/treatment parsers, runtime schema and migration, portal controller/frontend, alert state machines, CA snapshot, licenses and reference fixtures.
Adapted: acquisition cache window, history range validation and portal options, hardware/build target, TLS client adapter, touchscreen dashboard, night preference and main event loop.
Removed from active build: Core2 layout, Core2 flash scripts and private Arduino 2.x trust/allocator adapter. Historical Core2 notes are under docs/core2-reference; consult the top-level README for current behavior.

TLS follow-up: `src/ca_bundle.cpp` now ports the original Core2 trust policy and PSRAM allocator to Arduino 3.3.7 / mbedTLS 3 / IDF 5.5.2 bundle interfaces and offset-table data. This supersedes the initial SDK-only verifier described in the original port notes.

UI/history follow-up: added geometric trend arrows and direction fixtures; expanded treatment cache to 512; added bounded v3 paging, cross-page deduplication, PSRAM staging and in-place parsing. This supersedes the original single-page/128-marker Tab5 behavior. Physical verification confirmed both fixes.

Final scheduling follow-up: TLS available() polling blocks for one FreeRTOS tick to avoid idle-task starvation during HTTP responses. Watchdog protection remains enabled. The final device observation completed three full polls over 240 seconds without a restart.

Readability follow-up: redesigned the Tab5 dashboard/settings with native FreeSans/DejaVu typography, measured text fitting, a four-icon header, shared drawing/touch rectangles, separate metric cards and a quieter graph/status layout. The documentation preview uses the pinned library glyph bitmaps and illustrative data. Network/TLS and treatment-fetch behavior are retained.

Wi-Fi/sound follow-up: added a four-bar RSSI header indicator using Core2 thresholds and a header speaker toggle wired to the existing saved alert-sound setting. RSSI is sampled every five seconds to avoid excessive C6 control requests.

Clock follow-up: added a centered local-time/date header using the Tab5 onboard RTC and existing NTP configuration. RTC updates are deferred from the NTP callback to the UI task and stored as UTC.

SMB/clock-format follow-up: reused the original Core2 floating-label packing policy with capacity matching the Tab5 treatment cache, applied only to SMB doses >0.5 U; all SMB triangles are cyan. Added an isolated ns-tab5 clock-12 preference and shared 12/24-hour formatting for the header and time axis. Original Nightscout settings schema is unchanged. User confirmed both features on hardware.

Rotation source follow-up: saved ns-tab5 rotate-180 option selects display rotations 1/3 and relies on the pinned library touch transform. Full production syntax and native-font layout checks passed; full build/flash was blocked by the restricted session, so this change is not yet hardware verified.

## Supplied Trio alert recordings (2026-11-08)

The user supplied 44 `trio_*.m4a` recordings from their local AndroidAPS checkout. These are audio assets only; no AndroidAPS alert-engine implementation was copied. The accompanying `core/ui/SOUNDS.md` attribution and embedded Trio MIT licence are preserved at `assets/trio/ATTRIBUTION.md`. The originals are unchanged in `assets/trio/source`; generated 12 kHz unsigned 8-bit mono PCM and SHA-256 manifests are produced by `tools/build_trio_audio.py`. See the attribution file for the original Trio/AndroidAPS conversion history and sound-design credit.

Playback-level revision: conversion now decodes to floating-point PCM first, then applies one constant gain per recording so its unsigned 8-bit peak is +/-112 around silence 128 (about -1.2 dBFS). No compressor, time stretching or source-file edits are applied. Mild/Medium/Harsh retain their distinct recordings; original inter-recording peak-level differences are intentionally normalized. The manifest records decoded peak, applied gain and PCM peak for every clip.
