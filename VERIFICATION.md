# Verification — 2026-11-06

## Current TLS investigation and fix

The connected Tab5 on COM4 reported:

```text
Auth HTTP-client=-1 TLS=-32512 verify=0x00000000 SSL - Memory allocation failed
```

`-32512` is `-0x7F00`, an SSL allocation failure, not a reported certificate validation failure. The initial Tab5 implementation parsed all 118 CA roots from PEM per connection, while the pinned P4 SDK has `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=1`. PSRAM availability does not remove that internal-memory requirement. The configured server's certificate validated from the PC against the project's original CA source; its observed validity was November 5 through December 5, 2026. No credentials or clinical values are included in these logs.

The first fix used `setCACertBundle()` with the ESP-IDF 5.5.2 little-endian offset-table format. It removed the allocation failure, but the device then reported `TLS=-12288` (`-0x3000`) from the SDK certificate callback. A PC inspection of the TLS 1.2 server chain confirmed a cross-signed GTS Root R4 whose subject/public key exactly matched a reviewed trusted root; its external issuer was GlobalSign Root CA.

The final fix ports the original Core2 exact root-key trust adapter to the Arduino 3.3.7 / mbedTLS 3 SDK bundle interface. It also installs the Core2 PSRAM-preferred TLS allocator with internal fallback. All 118 roots are preserved. Hostname, date, usage and other verification failures remain enforced; no insecure fallback is present. The adapter uses the native-format binary in flash and does not parse the full CA PEM into internal RAM.

## Current completed checks

| Check | Result |
|---|---|
| Full `tab5` production build and link, Arduino 3.3.7 / IDF 5.5.2 | **Passed**, final adapter build 85.59 seconds |
| Linked internal RAM | 93,540 / 512,000 bytes (18.3%); runtime heap is additional |
| Application flash | 1,424,954 / 6,553,600 bytes (21.7%) |
| Native bundle vs official Espressif 5.5.2 generator | **Byte-for-byte match**, 55,222 bytes / 118 roots |
| Python CA tests | **5 passed**, including active SDK-format round trip, all root identities, malformed/truncated data and rejection of the incompatible Core2 format |
| Node/OpenSSL CA tests | **Passed**, all 118 public keys and cross-signed-root reference fixture |
| Host C++ fixtures | **157 passed, 0 failed** using Zig/Clang C++17 |
| Portal frontend tests | **Passed**, login, secret retention, network ordering, CSRF, revision checks and lock cleanup |
| Physical Tab5 flash | Application bytes written and checksum verified; standard target matches chip revision 1.3 |
| Live authenticated HTTPS | **Passed**: glucose, AAPS status and treatments succeeded across at least two complete polling cycles with no TLS errors |
| User screen confirmation | Dashboard returned and data loaded |
| Supplied release SHA-256 | Matches the attached manifest; original archive and Core2 project unchanged |
| PowerShell helper | Parsing/config creation/preservation and dry-run argument checks passed earlier |

The production build emitted a vendor SPI-header warning about a discarded `volatile` qualifier. No project compilation errors occurred. Configured firmware binaries and `include/config.h` are private local build artifacts and are excluded from the source ZIP.

The original initial-port build was blocked by a Windows compiler-launch restriction. That restriction no longer applies in this session, and the successful full build above supersedes the earlier syntax-only result. Historical logs remain in `verification/` for traceability. The Core2 root-key policy fixtures now exercise the policy used by the Tab5 adapter. The linker map confirms the bundle and allocator functions resolve to the project adapter.

## Device follow-through and remaining limits

The final adapter was flashed to the connected Tab5 on COM4 with no flash erase, NVS change or C6-radio update. The application checksum was verified. Two complete glucose/status/treatment polling cycles returned HTTP 200 with complete response bodies and no TLS errors. The user confirmed the dashboard returned and data loaded. Serial logs contain sizes/diagnostics, not clinical values or credentials.

An earlier post-upload hardware reset left the screen black until the user rebooted the device. A watchdog reset on the final upload caused a USB re-enumeration/port-configuration exception after the data hash had already verified; the application subsequently ran and served the successful polling cycles above. Future upload troubleshooting should distinguish a verified write from the reset/USB reconnect stage. The PowerShell helper now forces UTF-8 for Python output and restores the prior environment afterward, avoiding a separate Windows upload-progress encoding failure seen during this session.

Remaining validation: long-run reconnects, NTP recovery, deliberate invalid-certificate handshakes and a 24–48 hour soak. The rev-3.x environment and alternate panel revisions remain untested. The initial memory error and subsequent cross-signed-root failure were resolved for this device/server combination; this is not a certification of every server chain.

Current evidence: `verification/tab5-anchor-build.txt`, `verification/tab5-anchor-upload.txt`, `verification/tab5-anchor-serial.txt` and `verification/tab5-host-tests.txt`. Earlier logs are retained as history.

## Trend arrow and treatment-history follow-up

The Tab5 now draws native vector arrows for all seven Nightscout direction values, with an explicit unknown symbol for missing direction data. Treatment cache capacity increased from 128 to 512 markers. V3 treatment fetching now pages through the bounded full-day query, deduplicates across pages, and commits the completed cache atomically. Failed page fetches retain the previous cache. A staging buffer is allocated in PSRAM; the parser appends in place rather than placing a large treatment cache on the network task stack. A warning remains when the marker or page budget actually truncates history. Legacy mode retains its existing single-page limit. Paging follows the Nightscout v3 `limit`/`skip` interface documented in the official OpenAPI specification.

Validation: full firmware build/link passed (89.48 seconds), 162 host fixtures passed, including direction mapping, missing directions, page merging/deduplication, full intermediate pages and cache overflow retention. Device application write and checksum verification passed. Live device logs showed two treatment pages yielding **72 relevant markers, truncated=0**, with successful glucose/status requests. The user confirmed both the visible arrow and disappearance of the old treatment-limit message. Final linked RAM: 90,004 / 512,000 bytes; flash: 1,426,162 / 6,553,600 bytes.

Evidence: `verification/tab5-trend-build.txt`, `verification/tab5-trend-host-tests.txt`, `verification/tab5-trend-serial.txt`.

### Multi-page network scheduling

Continued monitoring of the first arrow/history build caught a CPU0 idle-task watchdog timeout while the network task was in HTTP response reading. An initial attempt to share idle priority and pause only between treatment pages did not eliminate the issue; continued monitoring reproduced it. The final candidate also blocks for one tick in SecureClient::available(), ensuring each HTTP/TLS receive-poll attempt lets other CPU0 work run. HTTP/handshake deadlines remain unchanged. The watchdog configuration has not been disabled or extended. The updated production build passed in 22.01 seconds; linked RAM remains 90,004 bytes and flash is 1,426,172 bytes. The original watchdog log remains in `tab5-trend-serial.txt` as historical evidence.

Final scheduling validation: the receive-poll blocking change built successfully in 20.04 seconds and was flashed with application checksum verification. A **240-second live serial observation** then completed **three full glucose/status/treatment polling cycles** with **zero watchdog resets, TLS failures or treatment truncation warnings**. Each treatment refresh read two pages and retained all 70 relevant markers in that window. Final application flash usage is 1,426,202 bytes; linked RAM remains 90,004 bytes. The 162 host fixtures passed for the arrow, paging/cache and retained API/security logic. This short observation does not replace the longer soak recommended above. The final source ZIP includes the receive-loop fix, superseding both earlier scheduling attempts.

Evidence: `verification/tab5-receive-yield-build.txt`, `verification/tab5-receive-yield-upload.txt`, `verification/tab5-receive-yield-serial.txt`.

## Readability and icon redesign

The dashboard and settings now use native FreeSans (18/24/36-pixel class) and DejaVu 40/72 numeric fonts, without scaling the small bitmap font. Glucose, IOB, COB and loop state occupy distinct cards; text fitting prevents long status strings from colliding with adjacent values. The graph has quieter target shading, higher-contrast labels, smoother traces and a separate footer. Forecast, Refresh, Day/Night and Settings use drawn vector icons with shared 52x52 drawing/touch rectangles. Time ranges and settings controls use larger labels; range changes keep the cached history offset within one day. Forecast scaling considers only the visible horizon.

The layout was inspected in a 1280x720 preview rendered with the pinned M5GFX glyph bitmaps and clearly labeled illustrative data. The final firmware build/link passed in 24.53 seconds; linked RAM is 90,020 bytes and flash is 1,479,426 bytes. All **162 host fixtures**, five CA tests and the portal frontend checks passed. The connected Tab5 application write and checksum verified, hardware initialization completed, and live glucose/status/treatment requests succeeded with two-page treatment caches and no truncation. The prior TLS trust and receive-loop watchdog fixes remain active.

The preview is in `docs/ui-preview.png`; build and serial evidence is in `verification/tab5-readability-*.txt`. Physical visual/icon feedback was requested from the user; do not mistake the illustrative preview for a device screenshot.

The readability build completed a 180-second device observation with three full glucose/status/treatment refreshes, no TLS errors, no watchdog resets and complete treatment caches. The serial capture was closed after the observation.

## Wi-Fi signal and header sound toggle

Added a four-bar Wi-Fi indicator using the Core2 RSSI thresholds (-55/-67/-75 dBm). Disconnected state has no active bars and a diagonal slash; zero/positive RSSI is treated as unavailable rather than excellent signal. The C6 signal measurement is sampled at five-second intervals; tapping the indicator shows signal quality and dBm. Added a 52x52 speaker control wired to the existing persistent sound setting, with crossed-speaker muted state and brief text feedback. It controls both glucose and missing-update sounds and stays synchronized with Settings. No separate sound flag or settings migration was introduced.

Full production build passed in 86.39 seconds. All **165 host fixtures** passed, including RSSI threshold boundaries and disconnected/invalid readings. Application write and checksum verified on the connected Tab5, and hardware initialization succeeded. Linked RAM: 90,020 bytes; flash: 1,481,022 bytes. Physical user feedback and the live polling observation are recorded below when complete.

The Wi-Fi/sound build completed a 180-second serial observation with **4 complete glucose/status/treatment refreshes**, zero watchdog resets/TLS failures, and no treatment truncation. The serial capture was closed after this check. Physical icon/toggle feedback was requested; persistence and sound behavior reuse the existing settings/alert implementation.

## Centered RTC clock

Added a centered HH:MM clock with a weekday/day/month line. The time comes from the system clock restored by M5Unified from the built-in RTC, then synchronized by the existing NTP configuration. Display uses the saved POSIX time zone, while the hardware RTC stores UTC. The NTP callback only sets an atomic flag; the UI task writes the RTC and reads it back, keeping I2C work out of the network callback. An unset/invalid clock shows placeholders rather than an epoch date.

The production build passed in 88.45 seconds. All **167 host fixtures** passed, including zero-padded time/date formatting and invalid-clock placeholders. Application checksum verification passed on the connected Tab5. Startup reported RTC hardware detected, and NTP-triggered hardware readback reported `2026-11-06 22:32:04 UTC`, consistent with the current UTC observation. Linked RAM: 90,020 bytes; flash: 1,482,190 bytes. This verifies synchronization/readback; long backup-power retention was not tested.

The RTC build completed a 180-second serial observation with 2 complete glucose/status/treatment refreshes and zero TLS errors/watchdog resets. Hardware RTC detection and NTP write/readback succeeded. The user separately confirmed the preceding Wi-Fi and sound controls work. Physical feedback on the centered clock was requested; no offline backup-retention duration is claimed.

## Cyan SMB labels and 12/24-hour setting

All SMB triangles are now cyan, with separate legend keys for SMB, regular insulin and carbs. Only SMB doses **strictly greater than 0.5 U** receive floating dose badges. The retained Core2 label-packing algorithm uses two lanes and bounded shifts, with explicitly marked totals/counts when adjacent qualifying labels cannot fit separately. Label capacity now matches all 512 cached treatment events, enforced by a compile-time assertion. Every triangle remains at its original timestamp; packing does not change treatment records or doses.

Settings now has a **Clock: 12 hour / 24 hour** button alongside Sound and Units. It persists `clock-12` in the existing `ns-tab5` preference namespace, leaving the Nightscout settings schema/profile intact. Both header and graph times use the same formatter. Midnight/noon and AM/PM are handled explicitly; RTC/NTP continue storing UTC.

Validation: final firmware build/link passed in 18.81 seconds; all **172 host fixtures passed**. Coverage includes the strict 0.5 U boundary, no labels on non-SMB insulin, dense 512-event label sums/counts, and midnight/noon/evening formatting. The illustrative preview was inspected with native library glyphs and shows cyan markers, floating labels and 12-hour time. Application write/hash verification and a 180-second serial observation passed on the device, with **3 complete polling cycles**, no TLS failures/watchdog resets and no treatment truncation. The user confirmed the cyan markers/labels and the 12/24-hour control look right and work. Clock-format persistence uses the existing Preferences API; an additional power-cycle retention test was not performed.

Evidence: `verification/tab5-smb-clock-build.txt`, `verification/tab5-smb-clock-host-tests.txt`, `verification/tab5-smb-clock-upload.txt`, `verification/tab5-smb-clock-serial.txt`.

## Saved 180-degree rotation — source update, hardware validation pending

Added Settings > Rotation: 0 deg / 180 deg alongside Sound, Units and Clock. It persists `rotate-180` in `ns-tab5`, preserving the Nightscout settings profile. Normal/rotated landscape use M5GFX rotation values 1/3. The same logical 1280x720 canvas is rendered in both orientations. M5Unified's touch update calls the display's `convertRawXY` on every sample, so it uses the active display rotation without a second coordinate flip. Changing rotation is handled on touch release; the next press starts in the new logical coordinates.

Completed: the settings row was visually checked with the actual native font glyphs, and all four captions fit their touch rectangles. PlatformIO compilation-database generation passed. A Clang 19.1.7 RISC-V syntax check of the full production translation unit against the pinned SDK/library headers passed (vendor-header warnings remain).

Blocked: PlatformIO could not execute the ESP32 compiler in the current restricted Windows session (`The system cannot execute the specified program`), and serial-device discovery via CIM was denied. No new rotation firmware image was built or flashed. The device still runs the previously verified SMB/clock-format firmware. Earlier successful build/test counts describe those earlier versions, not a successful rotation build.

Before marking this update hardware verified: build and flash with the supplied PowerShell helper in a supported environment; switch rotation both ways; verify Back, header icons, graph taps and swipe direction; then reboot to check the saved orientation. The source is packaged for that next step. Evidence is in `verification/tab5-rotation-build.txt` and `verification/tab5-rotation-syntax.txt`.

### Rotation follow-through after access was restored

Full production build/link passed in 17.29 seconds, superseding the earlier restricted-session build block. The update was flashed to COM4 with application checksum verification. The 172 retained host fixtures passed. Runtime logs show 4 completed treatment-cache refreshes and 0 watchdog/TLS error patterns; the user reported that the updated display looks great. The serial connection was closed afterward. A separate reboot-retention test and explicit confirmation of every rotated touch control were not recorded.

## Read-only Socket.IO live updates with full backfill

Implemented Engine.IO4 / Socket.IO4 `/storage` subscriptions for entries, devicestatus and treatments, using the existing CA-verified TLS adapter. The configured server's unauthenticated protocol probe returned HTTP 200 with Engine.IO4, WebSocket upgrade support and a 25-second ping interval / 20-second timeout; a separate CA-verified upgrade probe returned HTTP 101. No clinical records were written for testing.

Live events coalesce for 1.5 seconds and request complete validated REST snapshots of the affected collection. Startup, acknowledged subscription/reconnect, manual Refresh, range changes and throttled history navigation request all collections. The full-day cache remains available immediately while refreshing. A full safety refresh runs every 30�300 seconds according to configured freshness thresholds; partial event refreshes do not reset that deadline. Polling remains the fallback, with legacy mode unchanged. Settings > Live updates persists independently of the Nightscout profile.

Final production build/link passed in **19.86 seconds**. Linked RAM: **38,196 bytes**; flash: **1,493,886 bytes** (see build evidence for the exact linked size). Application checksum verification passed on COM4 and startup/full REST downloads succeeded. Esptool reported a Windows USB re-enumeration exception after checksum verification and watchdog reset; the device subsequently produced normal startup/API logs.

All **191 host fixtures passed**, including fragmented frames with interleaved control messages, extended lengths, bounded memory, malformed/masked/binary/control rejection, burst coalescing and rollover, events arriving after a fetch starts, and filtered collection routing without clinical payload retention. The 5 Python CA checks, all 118 Node/OpenSSL public-key comparisons and portal frontend checks also passed.

The persistent client explicitly re-arms the CA-bundle callback after closing: the pinned Arduino `stop_ssl_socket()` clears that callback. HTTP reads retain the existing watchdog scheduling protection; the bounded live reader schedules at the worker loop and services existing heartbeats during HTTPS body reads. Certificate verification stays enabled throughout. Earlier debug flashes fell back safely to REST while this integration behavior was corrected.

The corrected device run acknowledged all three read subscriptions (the user confirmed the **Live updates** footer). A real entries create event triggered an entries-only snapshot (`mask=1`). Treatment update/create notifications and a devicestatus create event arrived during a full history refresh; they were retained and coalesced into the following status+treatments refresh (`mask=6`). This directly exercises notifications arriving during backfill. The user confirmed Earlier / Return to live and Refresh work. The connection remained up beyond multiple heartbeat deadlines with no fallback, TLS failure or watchdog-reset messages in the corrected run. Some closely spaced USB diagnostic lines were truncated; full-backfill/live-refresh records and physical footer feedback establish successful subscription.

No long-duration outage soak, forced network outage/reconnect test, or changes/deletions of real Nightscout records are claimed. Automatic safety backfill timing is implemented but its entire maximum five-minute period was not required for the interactive observation. Toggle retention across a separate power cycle was not tested. Earlier failed handshake/debug flashes are superseded by the final corrected firmware and are not counted as passing live observations.

Evidence: `verification/tab5-live-build.txt`, `verification/tab5-live-upload.txt`, `verification/tab5-live-serial.txt`, `verification/tab5-live-host-tests.txt` and `verification/tab5-live-summary.txt`. The source ZIP excludes private configuration and configured binaries.

## Header battery / power indicator

Added a 100x52 tile immediately before Wi-Fi and Sound, clear of the centered clock. Samples M5Unified's Tab5 INA226-backed pack voltage, current and voltage-based level every five seconds on the UI task. Shows an estimated percentage, green/amber/red level coloring, and a cyan charging bolt only when charging status and positive current (>10 mA) agree. Tapping displays voltage and charging/discharging/idle details. Invalid voltage/percentage readings show an unavailable state. No charging controls or network behavior changed.

The production build passed in **30.08 seconds** (RAM 38,196 bytes; flash 1,496,072 bytes). Application write/checksum verification passed on COM4; normal startup and RTC synchronization followed the USB re-enumeration. Hardware reported **8.355 V, +416 mA, estimated 100%, charging**. The retained host suite passed **191/191**; those fixtures cover the existing model/protocol behavior, not physical battery calibration or icon rendering.

The percentage is an approximation from pack voltage, not a calibrated fuel-gauge reading. The pinned driver cannot reliably distinguish an empty battery bay with charger voltage from a connected idle pack, and it does not report Tab5 external-input presence; no battery-absence, USB-presence or runtime claim is made. Unplug/discharge behavior and percentage calibration were not tested in this run. Runtime observation and physical UI feedback follow below.

The user confirmed the battery tile is visible and its tap details work. The device completed startup and post-subscription full history refreshes with no TLS/watchdog/fallback error patterns observed. Serial monitoring was closed afterward. Evidence: `verification/tab5-battery-build.txt`, `verification/tab5-battery-upload.txt`, `verification/tab5-battery-serial.txt`, `verification/tab5-battery-host-tests.txt`.

## All-dose SMB labels and selectable Trio alert recordings (2026-11-08)

Removed the historical >0.5 U label filter: every finite positive SMB dose now participates in labeling. The graph uses three lanes, larger bounded shifts, and explicit summed/count badges with timestamp brackets when a cluster cannot fit separately. Zero/nonfinite amounts are not labeled. Underlying treatment events and marker timestamps remain unchanged. Labels for a positive value below 0.001 U explicitly use `<0.001U`, avoiding a misleading zero after decimal formatting.

Added Settings > Alert sounds with separate choices for low glucose, high glucose, stale glucose and stale loop. All 44 user-supplied Trio recordings are embedded as 12 kHz unsigned 8-bit mono PCM in flash. Acoustic/Electronic families use the saved global Mild/Medium/Harsh recording variant; the eight extra tones retain their single recording. Defaults are Acoustic LG/HG/DTS/DCR at Medium. Preview, Stop preview, mute and volume controls are available. A newly due real alert preempts a preview; glucose alerts preempt stale audio. Queue failures leave episodes eligible for retry. The existing low/high episode logic, low repeat interval and fixed ten-minute stale/startup-grace policy are unchanged; no new urgent-low or predictive triggers were introduced, as requested.

The pack contains **44 recordings / 3,490,037 PCM bytes**, with original M4As, source/PCM SHA-256 manifests, reproducible FFmpeg conversion script, attribution and the Trio MIT licence retained. The application build/link passed in **20.08 seconds**, using **38,324 bytes linked RAM** and **4,991,728 bytes flash / 6,553,600-byte application partition (76.2%)**. No runtime AAC decoder or large audio allocation is needed. Playback uses flash-resident data with permanent lifetime through the existing M5Unified speaker task.

All **197 host fixtures passed**. Added checks cover all 44 sound mappings, stored-setting validation, intensity behavior for families versus tones, labels on positive doses <=0.5 U and three-lane individual placement. All **2 audio-asset integrity tests passed**, checking every original/source hash and packed PCM span/hash, unique names, bounded clip sizes and complete family/variant/tone coverage. These host fixtures do not substitute for acoustic verification or calibrate sound loudness. Main sound settings and the 20-choice picker were rendered and inspected using the actual native font glyphs; no captions clip in those layouts.

Application-only flash to COM4 passed checksum verification; the normal post-reset Windows USB re-enumeration exception occurred after checksum verification. Startup, power/RTC readings, full history downloads and live-subscription recovery/backfill succeeded on the corrected build. The saved Nightscout profile and battery/clock/rotation settings were preserved. Physical label/preview confirmation and the short runtime observation are recorded below when complete. No actual low/high/stale readings or Nightscout writes were manufactured for testing.

### Speaker-output follow-through and physical confirmation

The user confirmed the all-dose SMB labels work. Initial clip requests were accepted by the speaker queue but were physically silent, including at increased volume. A two-slot/stereo transport change alone also remained silent. This was not marked as passing acoustic verification.

The final build adds an ES8388 DAC-only startup adapter in project source, using the clock/mixer/state-machine sequence described by Espressif's reference codec driver. It primes MCLK/BCLK with a permanent silent buffer, waits 10 ms once at first use, selects the DAC clock/mixer paths, restarts the digital state machine, then queues the actual clip. I2C failures prevent that attempt from being consumed. No dependency-cache edits or disabled checks are required. Output uses 48 kHz, two-slot I2S; source clips remain 12 kHz mono and are resampled by M5Unified. A manual Speaker check tone is provided alongside previews.

The final firmware build passed in **17.64 seconds**, with linked RAM **38,324 bytes** and flash **4,993,138 bytes** (76.2%). Application checksum verification passed. Codec readback showed DAC unmuted, DAC power active and speaker amplifier enabled. The user then explicitly confirmed **both Speaker check and Preview are audible**. Multiple accepted clip requests at different intensity settings were logged while full/live history updates continued. Three asset tests now pass, including a non-silent PCM check for every recording; the retained 197 host fixtures still pass.

This is a short functional observation, not a long-duration soak or a calibrated comparison of perceived loudness across all 44 recordings. No real glucose thresholds/readings or Nightscout records were altered to force an alarm. Setting persistence uses the checked Preferences write/read path; a separate power-cycle retention test was not performed. Preview and the actual alert scheduler share the same clip-playback path.

Evidence: `verification/tab5-trio-build.txt`, `verification/tab5-trio-upload.txt`, `verification/tab5-trio-serial.txt`, `verification/tab5-trio-host-tests.txt`, `verification/tab5-trio-asset-tests.txt`, `verification/tab5-trio-firmware-sha256.txt`; native-font layout previews are in `docs/alert-sounds-preview.png` and `docs/alert-sound-picker-preview.png`.

Final corrected-run diagnostic error patterns: **0**; completed treatment snapshots: **3**; accepted audio requests: **4**. Earlier silent debug runs are superseded by the physical confirmation above.

## Recorded-alert playback level adjustment (2026-11-08)

The user reported that the speaker-check tone was loud but recorded playback was still quiet. Measured original PCM peaks were substantially below the full-scale test waveform. Conversion now decodes the original M4As to floating point at 12 kHz before applying a constant gain per recording and quantizing to unsigned 8-bit PCM. All 44 clips peak at +/-112 around silence 128, about -1.2 dBFS, leaving headroom and avoiding endpoint clipping. Original files, timing, rhythm, timbre and within-recording dynamics remain unchanged; original relative peak levels between recordings are intentionally normalized. The recorded Mild/Medium/Harsh variants remain separate; the test tone, mute/volume settings and alert triggers are unchanged.

Examples of applied gain: Acoustic Low Medium **+8.5 dB**, Acoustic High Medium **+10.0 dB**, Acoustic Time-sensitive Harsh **+17.8 dB**, Glass bell **+13.0 dB**. Gains/decoded peaks/PCM peaks are recorded in the manifest. Normalizing from float avoids amplifying the earlier 8-bit quantization error. Pack length remains 3,490,037 bytes; no runtime audio-gain allocation is added.

All **4 asset tests passed**, including every original/PCM hash, complete catalog coverage, non-silent content and exact normalized peaks with no 0/255 endpoint samples. The firmware build passed in **11.15 seconds** with unchanged linked RAM (38,324 bytes) and flash (4,993,138 bytes). Application checksum verification and normal startup succeeded on COM4. Physical loudness feedback follows after the preview check.

Evidence: `verification/tab5-trio-gain-build.txt`, `verification/tab5-trio-gain-upload.txt`, `verification/tab5-trio-gain-asset-tests.txt` and the final runtime capture.

The 150-second post-flash serial observation completed without TLS/watchdog/reset error patterns. Full history downloads and subsequent live-triggered updates succeeded. Serial monitoring was closed. Audible loudness/clarity confirmation was requested but had not arrived when this package was finalized; software peak-level checks do not establish perceived loudness. Final runtime evidence: `verification/tab5-trio-gain-serial.txt`.

### Playback-level physical confirmation

The user subsequently confirmed the normalized recorded alerts are **loud enough and clear**, resolving the pending listening check above. This confirms subjective audibility/clarity on the connected Tab5 at the chosen volume; it is not a calibrated loudness measurement of every clip.


## Review fixes — 2026-11-09

Changes: alarm volume floor (32/255; volume 0 no longer silences alarms while the icon shows sound on); v3 devicestatus window 2 -> 10 records with a PSRAM JSON document sized from the body; 30 s task watchdog on the UI/alarm loop (reboot on hang); UI, worker and hand-off snapshots moved from internal RAM to PSRAM (queue replaced by a mutex-protected latest-wins hand-off); range/history gestures and display/alert-only saves no longer trigger network refetches. `Flash-Tab5.ps1` Python probes fixed for Windows PowerShell 5.1 (inner double quotes were stripped).

| Check | Result |
|---|---|
| Host C++ fixtures (g++ 13, ArduinoJson 6.21.5) | **202 passed, 0 failed** (5 new: volume floor, network-change filter, non-loop devicestatus) |
| Firmware build, `tab5` | **Passed**; DIRAM .bss 171,612 -> 39,768 bytes |
| Upload COM4 | **Passed**, hash verified |
| Boot internal heap | 374,056 -> **504,904** bytes free; log confirms `Snapshots: 3 x 65920 bytes in PSRAM` |
| Watchdog | `Loop watchdog: enabled (30 s, reboot on hang)` |
| 150 s serial run | Full backfill (AAPS body 28 KB for 10 records), live subscription, partial live refreshes for entries/treatments/devicestatus; no resets, panics or watchdog events |

Evidence: `verification/tab5-review-fixes-upload.txt`, `verification/tab5-review-fixes-serial.txt`.


## 12h/24h SMB strip — 2026-11-09

12h/24h views draw SMB totals per local-clock bucket (1 h / 2 h) in a dedicated strip below the glucose plot; 3h/6h keep per-dose labels. Axis ticks now fall on round local hours. New `include/graph_time.h` (offset/alignment/buckets) with host fixtures.

| Check | Result |
|---|---|
| Host C++ fixtures | **210 passed, 0 failed** (8 new: EST/EDT offset, tick alignment and spacing, bucket mode, contiguous 12h buckets, totals/counts, DST fall-back) |
| Firmware build from `.toolchain` | **Passed** |
| Upload COM4 | **Passed**, hash verified |
| 60 s serial run | Normal boot, PSRAM snapshots, watchdog enabled, full backfill, live subscription; no resets or panics |

Evidence: `verification/tab5-smb-strip-upload.txt`, `verification/tab5-smb-strip-serial.txt`.


## SMB strip in all views + dynamic glucose scale — 2026-11-09

The SMB strip replaces floating per-dose labels in every view (about 12 clock-aligned buckets per view). The glucose scale is fitted per view to visible readings plus drawn forecast lines (`graphscale::Range`, previously unused) instead of a fixed 40-250 mg/dL minimum; gridline spacing adapts via `graphscale::gridStep`.

| Check | Result |
|---|---|
| Host C++ fixtures | **212 passed, 0 failed** (new: per-view bucket sizes, scale fit/forecast expansion/empty default, gridline spacing) |
| Firmware build / upload COM4 | **Passed**, hash verified; linked RAM 46,600 bytes (per-dose label layout removed) |
| 60 s serial run | Boot internal heap 517,688 bytes free; watchdog enabled; full backfill and live subscription; no resets or panics |

Evidence: `verification/tab5-all-views-upload.txt`, `verification/tab5-all-views-serial.txt`.


## Loop card: uploader battery + CAGE/SAGE/IAGE — 2026-11-09

New `include/care_age.h` (care-event parsing, age text, warning levels). `model::statuses` reads `uploaderBattery`/`isCharging`/`uploader.battery`; v3 devicestatus `fields` extended. Care events fetched with `/api/v3/treatments?eventType$in=Site Change|Sensor Start|Sensor Change|Insulin Change` (operator and `|` separator confirmed in cgm-remote-monitor `lib/api3/storage/mongoCollection/utils.js`; event names confirmed in AndroidAPS `TE.kt`).

| Check | Result |
|---|---|
| Host C++ fixtures | **218 passed, 0 failed** (6 new: AAPS and nested uploader battery, out-of-range battery, latest-of-each care event incl. deleted/future/other types, age text, warning levels) |
| Firmware build / upload COM4 | **Passed**, hash verified |
| Serial run | Care request 3,929 bytes; `Care events found: cannula=1 sensor=1 insulin=1`; second full refresh correctly skipped by the 10-minute limit; live updates normal; no resets or panics |

README repair: the all-views README edit had matched an LF paragraph break in a CRLF file and removed the Rotation, Clock format, display/controls, Hardware, Windows helper and local-toolchain sections. They were restored from `.backup-2026-11-09/all-views-scale/README.md` with the current SMB, glucose-scale and Loop card text.

Evidence: `verification/tab5-loop-side-upload.txt`, `verification/tab5-loop-side-serial.txt`.
