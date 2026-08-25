## Highlights

Second pre-release of the Spark firmware, building on the v1.0.0 alpha. The focus of this cycle is power: the Spark board now reports real per-rail current, power and energy from its INA190 sense amplifiers, reads true state of charge from the BQ27427 fuel gauge instead of a hardcoded percentage, and runs the application core at 128 MHz with runtime control over the AKD1500 clocks and hardware SLEEP. Keyword spotting scoring has been replaced with softmax EMA smoothing, edge learning is now a structured multi-utterance flow, and the repository has gained its first hardware-in-the-loop CI job and a working clang-format gate.

## What's New

### Power, Battery and Current Measurement (#50, #57)
- **BQ27427 fuel gauge:** real state-of-charge readings over I2C replace the previous static battery percentage. SOC changes are delivered by interrupt on the gauge's GPOUT/SOC_INT pin and pushed to the phone app as they happen (#50)
- **Charger status:** battery module reports charger state from GPIO and degrades to charger-only operation if fuel gauge init fails (#50)
- **INA190 current sense:** new `current_ic` driver with a per-rail current, power and energy API backed by an always-on background sampler, default 10 Hz and runtime-adjustable (#50, #57)
- Shunt conversion corrected to the schematic values (0.1 Ω and 0.02 Ω); readings before this were wrong by the ratio of the assumed to the actual shunt (#57)
- **INA190 variant selection:** `CONFIG_INA190_VARIANT_A1` / `_A3` picks the fitted amplifier gain (25 V/V or 100 V/V) at build time, with a runtime `power variant` override. The part is analog and cannot be auto-detected, so this must match the board (#57)
- `power` shell command with `read`, `measure [ms]`, `rate [hz]`, `energy`, `reset` and `variant` subcommands. `power measure` runs a synchronous averaged burst independent of the background sampler rate (#57)
- Live power appears in the demo app metrics line; the sampler no longer shares the BLE stream semaphore, so shell reads work while a BLE stream is running (#57)
- New BLE opcodes `CMD_CURRENT_START` (13) and `CMD_CURRENT_STOP` (14) stream current values to the phone app (#50)

### System Clocking and AKD1500 Power Control (#58)
- nRF5340 application core raised to 128 MHz (`CONFIG_SYS_CPU_128MHZ`, default on for both boards). This is required for SPIM4 above 16 MHz and roughly doubles core dynamic power
- Akida moved to SPIM4 at up to 32 MHz, with `spi_freq` and `spi_rxdelay` shell commands
- AKD1500 hardware SLEEP for low-power idle, plus runtime core-clock, PLL, system-divider and clock-reference control: `akd_sleep`, `akd_coreclk`, `akd_pll`, `akd_pll_on`, `akd_sysdiv`, `akd_clkref`, `akd_clkinfo`
- An app-level power policy drives the above: `app start` restores the AKD1500 operating clocks, `app stop` drops them
- PDM capture block size is now a runtime parameter (`CONFIG_AUDIO_BLOCK_MS`, default 60 ms, hard bound `CONFIG_AUDIO_MAX_BLOCK_MS` 80 ms) via `app blkms`. Buffers are statically sized for the maximum, so a runtime change cannot overrun them
- Fixed the async inference path logging raw cycle counts in a field labelled microseconds

### BLE
- **PCM waveform stream (#52):** `CMD_STREAM_WAVE` (0x0C) replaces the RMS scalar stream with a binary notification format (6-byte header plus int16 LE payload) for oscilloscope-style rendering on the phone. Envelope mode sends 32 min/max pairs per 60 ms block (134 B, about 17.8 kbps) and preserves transient peaks; a decimated fallback mode sends 70 B (about 9.3 kbps). The link auto-downshifts to fallback when the negotiated MTU is under 140 B or more than 5 of the last 50 frames needed a retry, and recovers after 100 consecutive clean frames. Opcodes 9 and 11 still start and stop the stream; only the payload format changed
- Peripheral-initiated MTU exchange on connect, plus diagnostic logging of connection parameters, PHY and data-length updates (#52)
- **Runtime KWS configuration over BLE (#52):** `CMD_CONFIG` gains GET / SET / RESET shapes, returning a 6-frame snapshot and per-parameter ACKs. `CMD_DEPLOY_START` / `CMD_DEPLOY_STOP` are wired to the new KWS start/stop lifecycle
- Frame parsing carries a payload pointer into the receive buffer, so command handlers can read command-specific data (#52)
- Fixed flag handling for the PDM data and deploy-app commands (#47)
- Compiler warnings in the BLE services resolved (#51, #52)

### KWS and Edge Learning (#46, #52, #53)
- **Scoring replaced:** sliding-window scoring is gone, superseded by max pooling into a softmax followed by EMA smoothing with a chiming counter. Argmax now seeds from `INT32_MIN` so negative dense-layer outputs are handled correctly (#46)
- **Structured multi-utterance edge learning:** the learn flow now asks for the keyword 5 times and derives `2 x neurons_per_class` augmented inputs per utterance, chained through interrupt-driven work items. The device stays in learn-select after the flow completes; `el 0` returns it to inference (#46, #52)
- **Runtime start/stop and persistent parameters (#52):** new `kws_config` module with typed parameter IDs (RMS threshold, debounce, smoothing alpha, score threshold, chiming, speech timeout), NVS persistence and DMIC-state-aware setters. New `kws_app` start/stop/is-running control, idempotent over the DMIC and the edge-learning gate. Exposed via `app start`, `app stop`, `app reset` and `app <param> <val>`, all routed through one validating setter
- Shell commands renamed for clarity: `kws_el` is now `app`, `evt` is now `el` (#46)
- **Model metadata from `info.yaml` (#53):** MFCC sample rate, silence and unknown class indices, class count and neurons per class are read from the model metadata at BLE upload time and persisted to LittleFS, rather than being compiled in. `fetch_model.py` and `send_model_via_ble.py` were extended accordingly, and `generate_info.py` and `model_config.py` were added
- Detection threshold is uniform across base and edge-learned classes and runtime-tunable via `app score` (default 0.50), replacing the previously fixed per-class-type values (#52, #53)
- Smoothed scores and chiming counters are now cleared when a predict call fails unexpectedly, instead of carrying stale state into the next inference (#53)
- Fixed an out-of-bounds edge case involving `SPECTROGRAM_RES` (#53)
- **Async inference (#49):** runtime sync/async mode switching with IRQ control (`kws_mode`, `kws_mode_get`), an enqueue path for async submission, and a workqueue that fetches outputs post-inference. The active mode is reported to the phone app. The initial mode comes from the `inference_mode` field in the model metadata; `kws_mode` overrides it at runtime. Async requires the interrupt wiring present only on the Spark board, so DK builds log a warning and fall back to sync
- SPARK board LED cues: red LED solid during each "speak now" prompt in edge learning, and a roughly 500 ms flash on every keyword prediction in inference mode (#52)

### Sensors and Drivers
- **PDM microphone:** DC-block IIR state can now be reset (`dmic_reset_dc_state()`) so a pipeline restart does not carry the previous transient (#52)
- **MFCC:** Hann window uses `cosf` instead of `cos`, and DCT matrix operands are cast explicitly, removing implicit float-to-double promotion in the hot path (#51)
- **SPI flash:** fixed an out-of-bounds write in `spi_read_burst` and corrected its read transfer size (#51)
- **IMU:** `imu_calibrate` restored, logging moved to the LOG module (#51)
- **Camera and watchdog:** unused variable and missing return fixed (#51)

### Boards and Board Configuration (#47, #49, #50)
- Separate MCUboot overlays per board: `source/sysbuild/mcuboot_dk.overlay` and `source/sysbuild/mcuboot_spark.overlay`
- `gpio_init` centralises power-enable and button-interrupt setup; button and GPIO helpers moved to the common folder and the delay in GPIO init removed
- LED handling moved out of `ble_initialization.c` into `led_init.c`
- AKD1500 sleep pin configuration corrected to match the Spark board schematic
- NFC pins configured as regular GPIOs on the Spark board (`nfct-pins-as-gpios`), which disables NFC and frees those pins
- Board overlay differences documented in `docs/BOARD_OVERLAY_CHANGES.md`

### Application Refactor (#51)
- Inference helpers extracted into `infer_utils.c` (`compute_per_class_max`, `softmax`, `predict_class`)
- Direct `akd_device` calls in the app replaced with `akida.h` wrappers, and `akida_predict`, `akida_toggle_clock_counter` and `akida_get_clock_counter` added to that wrapper API
- Interrupt-only code guarded, double-promotion warnings suppressed globally, and float output moved to LOG macros
- `printk` calls replaced with the LOG module throughout, and CRLF line endings normalised to LF across the tree (#53)

### Build and CI (#23, #59)
- **Hardware-in-the-loop CI (#23):** new `HIL_Test` workflow on the self-hosted runner, triggered on pull requests into `main` and on pushes to `main`. It generates a signing key, materialises the model config from a repo secret, fetches and generates the model, builds and flashes the DK, runs the CLI test suite, uploads the model over BLE and then runs the inference test. Serialised behind a `hil-hardware-test` concurrency group so runs never contend for the board
- `source/utils/hil_test.py` covers 8 cases: Akida device ID, Akida SRAM, Akida flash ID, Akida full erase, watchdog, DMIC, IMU and KWS inference (#23)
- `scripts/run.sh` gained `-t` / `--test-cli` to run the CLI validation test inside Docker, and `--infer-test` to run only the KWS inference case (#23)
- **clang-format gate (#59):** `.clang-format` moved from `scripts/` to the repository root. `clang-format --style=file` searches upward from each file, so from anything under `source/` the old location was unreachable and the check had been silently validating against clang-format's built-in LLVM defaults rather than this repo's Google-based style. No source file was reformatted by that move
- New `.github/workflows/lint.yml` enforces formatting on pull requests into `main`. It runs on `ubuntu-latest` rather than the HIL runner, checks only the files the pull request changes, and pins clang-format to 22.1.1, the version in the repo's own `spark-ncs` toolchain image. On failure it prints the unified diff the formatter wanted. Pre-existing violations elsewhere in the tree are deliberately out of scope and do not fail unrelated pull requests (#59)

## Upcoming

- Secure boot
- SPI camera inference pipeline

## Artifacts

| File | Purpose |
|------|---------|
| `spark-{version}.signed.hex` | Signed firmware for JLink flashing |
| `spark-{version}.signed.bin` | Signed firmware for OTA/DFU via BLE |
| `spark-{version}-merged.hex` | MCUboot + app for initial programming |
| `spark-{version}-dfu.zip` | DFU package for nRF Connect mobile app |
| `SHA256SUMS.txt` | Integrity checksums |
