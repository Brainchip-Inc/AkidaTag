# Changelog

All notable changes to the AkidaTag firmware are documented in this file.

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

## [1.2.0+0] - 2026-09-14

Pre-release carrying the product rename from Spark to AkidaTag, a firmware update
path that needs nothing but the USB-C cable, and a build that works from a fresh
clone with no setup. The Akida engine and its dependencies are now committed
rather than fetched at configure time, and the generated model output has moved
out of the firmware tree. The cycle carries new features and a rename rather than
bug fixes alone, so the version takes a minor bump and the build counter resets
to `0`.

### Added

- **DFU:** firmware update over the USB-C cable with no button press and no debug
  probe. MCUboot now listens for an mcumgr command at every boot
  (`CONFIG_BOOT_SERIAL_WAIT_FOR_DFU`, a 1000 ms window) and stays in serial
  recovery when no image is bootable (`CONFIG_BOOT_SERIAL_NO_APPLICATION`), so a
  customer with neither a J-Link probe nor the phone app is no longer stuck. The
  board's buttons are too small to be a usable entrance, which is why the
  entrance is buttonless rather than the button path being documented (#82)
- **DFU:** `src/utils/usb_dfu_enter.py`, a host helper that reboots the running
  application over the serial port and repeats an mcumgr request until the
  bootloader answers inside that short window (#82)
- **Docs:** `docs/firmware-update-over-usb.md`, the single procedure for updating
  over USB-C: port selection, entering update mode, listing images, upload, boot,
  refused-image behaviour and troubleshooting. `docs/setup.md`, the UART DFU and
  console-logging sections of `src/README.md` and `AGENTS.md` point at it,
  including the CP2105 dual-port and KEYHASH TLV sharp edges. Entering recovery
  over the cable, uploading a signed image, booting it and refusing an image
  signed with a different key were all demonstrated on a physical AkidaTag board
  (#82)
- **Model:** the documented fetch flow now writes the per-model `.zip` bundle the
  BrainChip Connect app reads, so a clean re-fetch produces a complete package
  with no manual zipping. The archive wraps a directory named after the output
  directory around `info.yaml` and the two program binaries, matching the bundles
  the app is known to accept (#78)
- **BLE:** the device serial is reported over the bonded link instead of on the
  air, as a fifth and final frame appended to the `CMD_DEVICE_INFO` burst, which
  is served only over a connection the firmware raises to `BT_SECURITY_L4`. It is
  emitted as 16 lowercase hex characters of `device_id.high`; `device_id.low` is
  always zero and so is padding rather than information (#66)
- **CI:** conventional commit and pull request title gates, so
  `type(scope): concise message` is enforced on pull requests rather than only
  suggested by a local hook. The retired `lint.yml` is folded into the gate's
  lint job, keeping its pinned clang-format and its delegation to
  `scripts/clang_format.sh`, so one required check now covers formatting for
  every language in the repository (#68)
- **Licensing:** `src/deps/VENDORING.md`, `NOTICE` and `LICENSE-APACHE-2.0`
  record where each imported tree came from, at which version, what is pruned and
  how to upgrade it (#80)

### Changed

- **Product:** Spark is renamed to AkidaTag across the repository, in mixed case
  rather than AkidaTAG. The toolchain image `spark-ncs` becomes `akidatag-ncs`,
  the board Kconfig symbol `CONFIG_SPARK_BOARD` becomes `CONFIG_AKIDATAG_BOARD`,
  the board and MCUboot overlays and `spark_peripherals_power_enable()` follow,
  release assets become `akidatag-<VERSION>.*`, and the advertised device names
  become `AkidaTag` and `AkidaTag-DK`. No compatibility aliases were kept: one
  name per thing. Wire-format constants, BLE UUIDs, opcodes, the AKD1500 chip-id
  string and the Zephyr board target are deliberately untouched, and boards
  already flashed keep advertising the old spelling until reflashed (#75)
- **Repository:** the firmware tree `source/` is renamed to `src/`, with every
  reference repointed: `run.sh` app and overlay paths, `clang_format.sh`,
  `.gitignore`, the hardware and release workflows, README, `docs/setup.md`,
  `CONTRIBUTING.md`, `AGENTS.md` and the utility docstrings. The lint gate's
  changed-file filter is narrowed at the same time, so a pull request that only
  moves files no longer hands every moved file to the linters (#77)
- **Build:** every build now signs with the committed development key at
  `.env/development_key.pem`, so a fresh clone builds with no setup. Previously
  each clone generated its own random RSA-3072 key, which meant two checkouts on
  one machine produced firmware that would not install on each other's boards,
  and which protected nothing: it sat on a developer machine and guarded a board
  on the same desk. The key is public on purpose and says so in its own header.
  Official releases are unaffected and still sign with the private production
  key, which CI writes to `.env/production_key.pem`; the release workflow now
  points the build at that key explicitly rather than relying on a shared path,
  and fails the release if the key it is about to sign with is not the production
  one (#81)
- **Dependencies:** the Akida engine 2.17.0 tree, the FlatBuffers 2.0.8 headers
  and kissfft are committed under `src/deps` instead of being produced at CMake
  configure time. Nothing in the repository previously recorded which engine
  shipped, a version bump took five manual steps of which three failed silently,
  and FlatBuffers was downloaded from github.com on every build with no checksum,
  so the firmware build needed public internet and trusted whatever that URL
  served. An upgrade is now a reviewable diff. The engine's own CMake owns its
  source list and `AKIDA_VERSION`, replacing the hand-maintained source list and
  the hardcoded version. This repository's forked AKD1500 SPI driver is renamed
  to `akd1500_spi_driver_nrf.cpp` so it no longer shares a basename with the
  vendor's reference implementation, which is committed but never compiled (#80)
- **Model:** generated model output now lands in a git-ignored `models/`
  directory at the repository root instead of inside the firmware tree, so no
  future rename of the source tree can break the model pipeline. The
  `MODEL_CONFIG_DEMO_APPS_KWS` repository secret and any local `.env` model
  config carry `output_dir` and must be updated to the new root before the
  hardware workflow next runs (#79)
- **CI:** the hardware-in-the-loop board test runs on request rather than on
  every pull request and every push to `main`. A maintainer comments `/dk-test`
  and the board runs against that pull request's head commit, guarded by an exact
  command match, an owner/member/collaborator check and a no-forks rule, all on a
  hosted runner, with the outcome published back as a check named `hardware`.
  Renamed from `HIL_Test` to `hardware` and from `ci.yml` to `hardware.yml`; the
  test steps themselves are unchanged (#71)
- **Contributing:** `CONTRIBUTING.md` now documents what the CI gate actually
  accepts, with `check-subject.sh` named as the authority, rather than describing
  a local hook whose rules disagreed with the gate in both directions (#70)
- **Docs:** the security section describes what is actually enabled instead of
  claiming secure boot. The firmware signs application images with RSA-3072 and
  MCUboot verifies them at boot and over DFU, but there is no immutable
  first-stage bootloader verifying MCUboot itself, no APPROTECT and no
  anti-rollback counter. The roadmap entry in README.md already lists secure boot
  as planned and is left as is (#67)

### Fixed

- **BLE privacy:** the scan response carried the chip's factory DEVICEID as a
  128-bit service UUID. That value never changes, so any passive scanner could
  follow a specific tag across every address rotation, defeating the resolvable
  private address that `CONFIG_BT_PRIVACY` and `CONFIG_BT_RPA_TIMEOUT` already
  provide. The scan response is dropped entirely at both `bt_le_adv_start()` call
  sites. Advertising stays connectable and the advertising data is unchanged, so
  the flags, device name and manufacturer data the app identifies the board by
  are still broadcast (#66)
- **BLE:** both boards advertised under the same name, so a DK on the bench and
  an AkidaTag board were indistinguishable on air. The DK now carries its own
  name through `src/boards/dk.conf`, which `scripts/run.sh` merges via
  `EXTRA_CONF_FILE` in its `--dk` branch. Both boards build for the same Zephyr
  board target, so the auto-discovered `boards/<board>.conf` route would have
  merged into both builds; what distinguishes the two is the set of CMake
  arguments `run.sh` selects, which is where the override belongs (#66)
- **Utils:** `get_device_name()` read `prj.conf`, which holds only the AkidaTag
  board's name, so every consumer would have scanned for the wrong name against a
  DK, including `send_model_via_ble.py` that the hardware workflow runs straight
  after flashing one. It now reads the merged Kconfig output of the most recent
  build, which is the configuration the flashed firmware was actually compiled
  from, with `prj.conf` kept as the fallback for a checkout that has not been
  built yet (#66)
- **CI:** reacting to a `/dk-test` request failed with HTTP 403. A comment on a
  pull request is authorised against the pull-requests scope rather than the
  issues scope, and the job held only `pull-requests: read`; both scopes are now
  written out. The board job also now requires the authorising job to have
  succeeded outright rather than merely to have resolved a commit, so the
  hardware is never spent on a run whose result has nowhere to be reported (#73)

### Removed

- **Samples:** the three early validation applications under `samples/`, now that
  `demo_apps` covers that ground. `scripts/run.sh` makes `demo_apps` the ordinary
  app path and rejects anything else by name, the setup walkthrough and README
  are retargeted, and two broken J-Link examples in the help text are corrected
  (#76)
- **Hooks:** `scripts/commit_msg_hook.sh` and `scripts/install_git_hooks.sh`,
  superseded by the CI gate. A copy already installed in a clone keeps enforcing
  the old rules, so `CONTRIBUTING.md` and `AGENTS.md` both say to remove
  `.git/hooks/commit-msg` (#70)

## [1.1.1+0] - 2026-08-25

Patch release fixing the two defects that shipped in `v1.1.0+0`, both introduced
by `628d2a4`: BLE model update never completed, and the DK build did not
compile.

### Fixed

- **BLE model update:** the DATA-phase erase ran against a sleeping AKD1500. The
  model flash sits behind the AKD1500 and is reachable only through its S2M
  feedthrough while the chip is awake, and since `628d2a4` the KWS async loop
  sleeps the chip between inferences, so asleep is its steady state. The erase's
  status poll never saw WIP clear, burned its full 1000 ms budget on the first
  sector and returned an error the phone app reported as a failed write to the
  file-size characteristic. The flash helpers now take a wake reference and hold
  the AKD1500 mutex across claim/wake/operate/release, so the BLE erase, the BLE
  chunk write, the post-write readback and the `full_erase` shell command are all
  covered. Measured on the Spark board with KWS in its normal async duty cycle,
  the erase goes from "Erase failed" after 1014 ms to "Erase Successful" in
  286 ms (#63)
- **SPI flash:** an unreachable flash reported success having written nothing. WIP
  is bit 0 of the status register, so the poll alone cannot separate "ready" from
  "nobody answered": an all-zero read looks instantly ready and an all-ones read
  looks busy forever. Erase and write are now gated on a part-agnostic JEDEC ID
  probe, and an all-ones status is rejected as a non-response. All-zero remains
  accepted by the poll itself, since an idle unprotected flash legitimately reads
  0x00 (#63)
- **BLE model update:** metadata for a model that was never written could be
  committed. The DATA-phase CRC covers the bytes that arrived over the air, which
  says nothing about whether the flash took them, and the `model_data_meta_t`
  record was persisted before the readback ran. The record is now built in RAM
  and validated against the flash first, persisting only on a match; a mismatch
  NACKs the host with `ACK_CRC_FAIL` and leaves the previous record in place
  (#63)
- **Build:** the DK build did not compile. `main.cpp` calls `akd_sleep()` from ten
  places that are not guarded by `CONFIG_SPARK_BOARD` but included `gpio/gpio.h`
  only inside such a guard, so the DK build failed with ten "'akd_sleep' was not
  declared in this scope" errors. `gpio.h` already guards its own Spark-only
  contents and supplies no-op inlines for the rest, so the include now sits
  outside the guard (#63)

### Added

- **HIL:** BLE model-update regression test (`model_update_hil_test.py`) covering
  the sleeping-AKD1500 erase defect. It runs a full INFO+DATA transfer against a
  board left in its normal async KWS duty cycle and asserts on the serial log,
  pairing a per-sector erase-duration band with the outcome string so that
  neither mask of an unreachable flash passes: the ~1014 ms poll timeout or the
  ~2 ms all-zero status that reports "Erase Successful" having erased nothing. It
  also asserts that readback validation precedes the metadata commit and that a
  reset re-validates the model from flash. Verified to exit 1 on `v1.1.0+0` and
  0 on the fixed build (#63)
- **Config:** `CONFIG_AKD_WAKE_SETTLE_US` (default 100), the settle delay applied
  after de-asserting SLEEP and before the first SPI transaction, paid only when
  the chip was actually asleep (#63)

### Changed

- **AKD1500 wake:** the wake state is now reference-counted (`akd_wake_get()` /
  `akd_wake_put()`, replacing `akd_sleep()`) rather than saved and restored
  around an operation. The holders are spread across threads and none can tell
  whether the chip is still needed by another: the audio thread wakes the chip to
  enqueue an inference while `akd_async_thread` hands the reference back after
  the matching fetch, and a BLE model update holds its own reference across a
  flash access that can overlap an inference. A saved-and-restored boolean lets
  one holder clock-gate the chip out from under another. `gpio.c` is now the only
  writer of the SLEEP pin, and exposes `akd_wake_count()` plus monotonic release
  and gate tallies for diagnostics (#63)
- **Repository:** CODEOWNERS team and path rules replaced with a single default
  owner (#62)

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
- DFU (Device Firmware Update) with MCUboot and RSA-signed application images
- PDM microphone driver with DMIC audio acquisition
- Model CRC integrity checks
- SPI chip select handling via DeviceTree
- App version reporting
- BLE GATT file transfer service with CRC32 validation
- LittleFS on external SPI flash
- Sample applications for DK board validation
- Dockerfile and scripts for the development environment

[Unreleased]: https://github.com/Brainchip-Inc/AkidaTag/compare/v1.2.0+0...HEAD
[1.2.0+0]: https://github.com/Brainchip-Inc/AkidaTag/compare/v1.1.1+0...v1.2.0+0
[1.1.1+0]: https://github.com/Brainchip-Inc/AkidaTag/compare/v1.1.0+0...v1.1.1+0
[1.1.0+0]: https://github.com/Brainchip-Inc/AkidaTag/compare/v1.0.0+0...v1.1.0+0
[1.0.0+0]: https://github.com/Brainchip-Inc/AkidaTag/compare/v0.2.0...v1.0.0+0
[0.2.0]: https://github.com/Brainchip-Inc/AkidaTag/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/Brainchip-Inc/AkidaTag/releases/tag/v0.1.0
