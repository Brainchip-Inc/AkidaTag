# Changelog

All notable changes to the Spark firmware are documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/).

Versions use MCUboot semi-semantic versioning, `major.minor.revision+build`, as
consumed by `imgtool` and stored in the image header's `iv_build_num` field. The
build counter resets to `0` on every change to `major.minor.revision`. The two
earliest entries predate that scheme and carry plain `major.minor.patch` tags.

The `## [x.y.z+b]` version headings are parsed by
`.github/workflows/release.yml`, which extracts the section for the version in
the root `VERSION` file and publishes it as the GitHub release body. Keep the
heading form exact.

## [Unreleased]

<!--
Collects changes that have landed on `main` but have not yet been published as a
release. On release, this section is renamed to the new version heading and a
fresh empty `[Unreleased]` is added above it. Not to be confused with the
roadmap in README.md, which lists work that has not landed at all.
-->

## [1.1.0+0] - 2026-08-25

Second pre-release, building on the v1.0.0 alpha. The focus of this cycle is
power measurement and power control on the Spark board, alongside a rewritten
keyword-spotting scoring path and the repository's first hardware-in-the-loop CI
job.

### Added

- **Fuel gauge:** BQ27427 driver reading real state of charge over I2C, with
  interrupt-driven SOC-change notification on the gauge's GPOUT/SOC_INT pin and
  push of the new value to the phone app (#50)
- **Battery:** charger-status reporting from GPIO, degrading to charger-only
  operation if fuel gauge initialisation fails (#50)
- **Current sense:** `current_ic` driver exposing per-rail current, power and
  energy, backed by an always-on background sampler at a runtime-adjustable rate
  (#50, #57)
- **Current sense:** build-time INA190 variant selection via
  `CONFIG_INA190_VARIANT_A1` / `_A3` for the fitted amplifier gain (25 V/V or
  100 V/V), with a runtime `power variant` override. The part is analog and
  cannot be auto-detected, so this must match the board (#57)
- **Current sense:** `power` shell command with `read`, `measure [ms]`,
  `rate [hz]`, `energy`, `reset` and `variant` subcommands. `power measure` runs
  a synchronous averaged burst independent of the background sampler rate (#57)
- **Current sense:** live power in the demo application metrics line (#57)
- **BLE:** `CMD_CURRENT_START` (13) and `CMD_CURRENT_STOP` (14) opcodes to
  stream current values to the phone app (#50)
- **BLE:** `CMD_STREAM_WAVE` (0x0C) binary PCM waveform notification for
  oscilloscope-style rendering on the phone. A 6-byte header ('B' magic, opcode,
  uint16 LE sequence, uint16 LE sample count) precedes an int16 LE payload.
  Envelope mode sends 32 interleaved min/max pairs per 60 ms block (134 bytes,
  about 17.8 kbps) and preserves transient peaks; a decimated fallback mode
  sends 70 bytes (about 9.3 kbps). The link auto-downshifts to fallback when the
  negotiated MTU is under 140 bytes or more than 5 of the last 50 frames needed
  a retry, and recovers after 100 consecutive clean frames. Opcodes 9 and 11
  still start and stop the stream (#52)
- **BLE:** peripheral-initiated MTU exchange on connect, plus diagnostic logging
  of connection parameters, PHY and data-length updates (#52)
- **BLE:** `CMD_CONFIG` GET / SET / RESET shapes, returning a 6-frame parameter
  snapshot and per-parameter ACKs. `CMD_DEPLOY_START` and `CMD_DEPLOY_STOP` are
  wired to the new KWS start/stop lifecycle (#52)
- **BLE:** parsed frames carry a payload pointer into the receive buffer so
  handlers can read command-specific data (#52)
- **KWS:** `kws_config` module with typed parameter IDs (RMS threshold,
  debounce, smoothing alpha, score threshold, chiming, speech timeout), NVS
  persistence and DMIC-state-aware setters, reachable from both the shell and
  BLE (#52)
- **KWS:** `kws_app` start/stop/is-running lifecycle control, idempotent over
  the DMIC and the edge-learning gate, exposed as `app start`, `app stop` and
  `app reset` (#52)
- **KWS:** structured multi-utterance edge learning. The learn flow now asks for
  the keyword 5 times and derives `2 * neurons_per_class` augmented inputs per
  utterance, chained through interrupt-driven work items (#46, #52)
- **KWS:** runtime sync/async inference mode switching with IRQ control
  (`kws_mode`, `kws_mode_get`), an enqueue path for async submission, and a
  workqueue that fetches outputs post-inference. The active mode is reported to
  the phone app. Async requires interrupt wiring present only on the Spark
  board, so DK builds warn and fall back to sync (#49)
- **KWS:** model metadata is read from `info.yaml` at BLE upload time and
  persisted to LittleFS rather than compiled in: MFCC sample rate, silence and
  unknown class indices, class count and neurons per class. `fetch_model.py` and
  `send_model_via_ble.py` extended accordingly, and `generate_info.py` and
  `model_config.py` added (#53)
- **KWS:** SPARK board LED cues, with the red LED solid during each "speak now"
  prompt in edge learning and a roughly 500 ms flash on every keyword prediction
  in inference mode (#52)
- **Audio:** `dmic_reset_dc_state()` clears the DC-block IIR transient so a
  pipeline restart does not carry the previous one (#52)
- **Audio:** runtime PDM capture block size via `app blkms`, defaulting to
  `CONFIG_AUDIO_BLOCK_MS` (60 ms) and hard-bounded by `CONFIG_AUDIO_MAX_BLOCK_MS`
  (80 ms). Buffers are statically sized for the maximum, so a runtime change
  cannot overrun them (#58)
- **AKD1500:** hardware SLEEP for low-power idle plus runtime core-clock, PLL,
  system-divider and clock-reference control, as `akd_sleep`, `akd_coreclk`,
  `akd_pll`, `akd_pll_on`, `akd_sysdiv`, `akd_clkref` and `akd_clkinfo`. An
  app-level power policy drives them: `app start` restores the operating clocks
  and `app stop` drops them (#58)
- **AKD1500:** `spi_freq` and `spi_rxdelay` shell commands (#58)
- **Application:** `infer_utils.c` with `compute_per_class_max`, `softmax` and
  `predict_class`, plus `akida_predict`, `akida_toggle_clock_counter` and
  `akida_get_clock_counter` on the `akida.h` wrapper API (#51)
- **Boards:** separate MCUboot overlays per board,
  `source/sysbuild/mcuboot_dk.overlay` and
  `source/sysbuild/mcuboot_spark.overlay` (#47, #50)
- **Boards:** `gpio_init` centralising power-enable and button-interrupt setup
  (#47)
- **Boards:** overlay differences documented in `docs/BOARD_OVERLAY_CHANGES.md`
  (#47)
- **CI:** `HIL_Test` workflow on the self-hosted runner, on pull requests into
  `main` and pushes to `main`. It generates a signing key, materialises the
  model config from a repo secret, fetches and generates the model, builds and
  flashes the DK, runs the CLI test suite, uploads the model over BLE and runs
  the inference test. Serialised behind a `hil-hardware-test` concurrency group
  so runs never contend for the board (#23)
- **CI:** `source/utils/hil_test.py` covering 8 cases: Akida device ID, Akida
  SRAM, Akida flash ID, Akida full erase, watchdog, DMIC, IMU and KWS inference
  (#23)
- **CI:** `scripts/run.sh` gained `-t` / `--test-cli` to run the CLI validation
  test inside Docker, and `--infer-test` to run only the KWS inference case
  (#23)
- **CI:** `.github/workflows/lint.yml` enforcing clang-format on pull requests
  into `main`. It runs on `ubuntu-latest` rather than the HIL runner, checks
  only the files the pull request changes, pins clang-format to 22.1.1 (the
  version in the repo's own `spark-ncs` toolchain image) and prints the unified
  diff the formatter wanted on failure. Pre-existing violations elsewhere in the
  tree are out of scope and do not fail unrelated pull requests (#59)

### Changed

- **System:** nRF5340 application core raised from the 64 MHz reset default to
  128 MHz (`CONFIG_SYS_CPU_128MHZ`, default on for both boards). Required for
  SPIM4 above 16 MHz; roughly doubles core dynamic power (#58)
- **AKD1500:** moved to SPIM4 at up to 32 MHz (#58)
- **KWS:** scoring replaced. Sliding-window scoring gives way to per-class max
  pooling into a softmax, then EMA smoothing, then a chiming counter (#46)
- **KWS:** detection threshold is now uniform across base and edge-learned
  classes and runtime-tunable via `app score`, default 0.50, replacing the
  previously fixed per-class-type values (#52, #53)
- **KWS:** the device stays in learn-select after the learn flow completes;
  `el 0` returns it to inference (#52)
- **KWS:** initial inference mode comes from the `inference_mode` field in the
  model metadata rather than a compile-time default; `kws_mode` still overrides
  it at runtime (#49, #53)
- **KWS:** shell commands renamed for clarity, `kws_el` to `app` and `evt` to
  `el` (#46)
- **Battery:** the phone app now receives real state of charge in place of the
  previous static percentage (#50)
- **Current sense:** default background sampler rate lowered to 10 Hz
  (`CONFIG_CURRENT_DEFAULT_RATE_HZ`) for a stable reading and low overhead;
  accurate spot values come from `power measure` (#57)
- **Current sense:** the sampler no longer shares the BLE stream semaphore, so
  shell reads work while a BLE stream is running (#57)
- **Application:** direct `akd_device` calls replaced with `akida.h` wrappers;
  interrupt-only code guarded and double-promotion warnings suppressed globally
  (#51)
- **Logging:** `printk` replaced with the LOG module throughout, and float
  output moved to LOG macros (#51, #53)
- **Boards:** LED handling moved out of `ble_initialization.c` into
  `led_init.c`; button and GPIO helpers moved to the common folder and the delay
  in GPIO initialisation removed (#47, #50)
- **Repository:** `.clang-format` moved from `scripts/` to the repository root
  so `clang-format --style=file`, which searches upward from each file, can
  actually reach it from `source/`. Config contents unchanged and no source file
  reformatted by the move (#59)
- **Repository:** CRLF line endings normalised to LF across the tree (#53)
- **Release process:** this cumulative `CHANGELOG.md` replaces the per-release
  `RELEASE_NOTES.md`, and `.github/workflows/release.yml` now extracts the
  released version's section and appends a static artifacts footer instead of
  publishing a whole file

### Fixed

- **Current sense:** INA190 shunt conversion corrected to the schematic values
  (0.1 Ω and 0.02 Ω). Readings before this were wrong by the ratio of the
  assumed to the actual shunt (#57)
- **KWS:** async inference DMA time was logged as a raw cycle count in a field
  labelled microseconds (#58)
- **KWS:** argmax now seeds from `INT32_MIN`, so negative dense-layer outputs
  are handled correctly (#46)
- **KWS:** smoothed scores and chiming counters are cleared when a predict call
  fails unexpectedly, instead of carrying stale state into the next inference
  (#53)
- **KWS:** out-of-bounds edge case involving `SPECTROGRAM_RES` (#53)
- **SPI flash:** out-of-bounds write in `spi_read_burst`, and its read transfer
  size (#51)
- **MFCC:** Hann window uses `cosf` instead of `cos` and DCT matrix operands are
  cast explicitly, removing implicit float-to-double promotion in the hot path
  (#51)
- **IMU:** `imu_calibrate` restored (#51)
- **Camera and watchdog:** unused variable and missing return (#51)
- **BLE:** flag handling for the PDM data and deploy-app commands (#47)
- **BLE:** compiler warnings in the BLE services (#51, #52)
- **Boards:** AKD1500 sleep pin configuration corrected to match the Spark board
  schematic (#50)
- **Repository:** the formatting check had been silently validating against
  clang-format's built-in LLVM defaults rather than this repo's Google-based
  style, because the config was unreachable from `source/`. Against `main` the
  failing-file count went from 4 to 56 once it began comparing against the real
  config (#59)

### Removed

- **BLE:** the RMS scalar waveform stream, superseded by the `CMD_STREAM_WAVE`
  binary format. RMS is still computed internally for the VAD threshold; only
  the BLE emission is gone (#52)
- **KWS:** sliding-window scoring, and the minimum-inference-frames gate along
  with `g_min_inference_frames` and its shell command (#46)
- **Boards:** NFC on the Spark board. Its pins are now regular GPIOs
  (`nfct-pins-as-gpios`) (#49)

## [1.0.0+0] - 2026-04-07

First alpha release of the Spark firmware platform, moving from early
prototyping (v0.2.0) to a firmware with keyword spotting, edge learning,
BLE services, serial recovery and secure OTA application updates. Published as
a pre-release.

### Added

- **KWS inference:** keyword spotting on the AKD1500 AI accelerator, real-time
  audio classification from PDM microphone input, and debounce plus warm-up
  mechanisms to prevent false detections during power-on
- **Edge learning:** on-device keyword spotting training via AKD1500 edge
  learning, dual-mode support for edge-learning-enabled and inference-only
  models, and CRC validation for model integrity (#31)
- **BLE commands and phone app integration (#18, #25):** Nordic UART Service
  (NUS) with multi-frame fragmentation for large data transfers; a model
  information service exposing metadata, input and output shapes, CRC and flash
  address; an edge learning service with inference control, training start/stop
  and class management, with ACK notifications; device information broadcasting
  for firmware version, chip ID and device ID; a device reset command; and a
  battery information service
- **BLE:** device ID authentication and BLE privacy support (#36)
- **BLE:** wireless model transfer, streaming model metadata, program info and
  program data with validation
- **Sensors:** PDM microphone (DMIC) capture at 16 kHz, 16-bit mono, with DC
  offset removal and RMS computation (#34)
- **Sensors:** ISM330 IMU driver over I2C for accelerometer and gyroscope
- **Sensors:** SPI camera on SPI3 at 8 MHz, 96x96 and 128x128 RGB888 with
  RGB565-to-RGB888 conversion and base64 encoding for UART and BLE transmission
  (#17)
- **Boot and updates:** MCUboot bootloader with RSA-3072 firmware signature
  verification
- **Boot and updates:** BLE DFU for the application core via MCUmgr and the nRF
  Connect mobile app
- **Boot and updates:** serial recovery support for firmware restoration
- **Boot and updates:** dual-slot architecture with external flash for the
  secondary image
- **Boot and updates:** CRC validation on model data transfers
- **Boot and updates:** production signing key integration for secure firmware
  builds
- **Boot management:** multi-counter boot tracking for total lifetime,
  per-firmware-version and watchdog-specific counters, all NVS-persisted across
  reboots (#27, #33)
- **Akida:** delayed device object creation for a better startup sequence, with
  watchdog initialisation moved earlier in boot (#37)
- **Boards:** Spark board (default) and nRF5340 DK overlays, with runtime board
  selection via build flags (#30)

### Fixed

- Watchdog timer fix (#27)

## [0.2.0] - 2026-03-19

Second firmware milestone for the BrainChip AKD1500 on nRF5340 DK. This was a
development milestone marker, not a formal release, and has no GitHub release
page.

### Added

- LED indicator support for system status feedback
- Watchdog timer configuration for system reliability
- Accelerometer and gyroscope (IMU) integration
- Serial recovery logic for DFU via UART

### Changed

- KWS output improvements: metrics mode, confidence scoring and spelling fixes

## [0.1.0] - 2026-02-10

First working baseline of the Spark firmware for the BrainChip AKD1500 on
nRF5340 DK. This was a development milestone marker, not a formal release, and
has no GitHub release page.

### Added

- KWS (Keyword Spotting) demo with Akida inference
- Edge learning support for the KWS model
- DFU (Device Firmware Update) with MCUboot and secure boot
- PDM microphone driver with DMIC audio acquisition
- Model CRC integrity checks
- SPI chip select handling via DeviceTree
- App version reporting
- BLE GATT file transfer service with CRC32 validation
- LittleFS on external SPI flash
- Sample applications for DK board validation
- Dockerfile and scripts for the development environment

[Unreleased]: https://github.com/Brainchip-Inc/spark/compare/v1.1.0+0...HEAD
[1.1.0+0]: https://github.com/Brainchip-Inc/spark/compare/v1.0.0+0...v1.1.0+0
[1.0.0+0]: https://github.com/Brainchip-Inc/spark/compare/v0.2.0...v1.0.0+0
[0.2.0]: https://github.com/Brainchip-Inc/spark/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/Brainchip-Inc/spark/releases/tag/v0.1.0
