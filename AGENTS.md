# Project agent memory

This file is the project's committed home for project-intrinsic agent knowledge: build, test, release, architecture, and sharp-edge notes that should travel with the code.

- Add durable project-specific notes here as they are discovered through real work.

## Build

Everything goes through `scripts/run.sh` in the `akidatag-ncs` Docker image; `docs/setup.md`
is the reference. The application app is `demo_apps`, whose source directory is `src/`.

Every build signs, and the key it signs with is committed at `.env/development_key.pem`, so a fresh
clone builds with no setup. That key is public on purpose; nothing in its name says so, so its own
header is what tells you, and `src/README.md` "Application Security" describes both keys.
`.github/workflows/release.yml` writes the private production key to `.env/production_key.pem` and
repoints `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE` at it, and that one override is the whole boundary
between a local build and a published release. Two guards in that step fail the release if the
config still names the development key or does not name the production one; both skip comment
lines, because the comments in `src/sysbuild.conf` name both files. Leave that step alone.

Because MCUboot carries the public half inside its own image, a board trusts whichever key built
the bootloader on it. Only `merged.hex` over SWD replaces MCUboot, so only that re-keys a board;
BLE and serial-recovery updates write the application slot alone and must match the key already
there. `src/README.md` "Application Security" is the reference.

The model workflow still has a fresh-worktree gap: the per-model configs it reads live at
`.env/<app>/<model>.yaml`, which is git-ignored, so every documented `--fetch_model` /
`--generate_info` command dies on a missing config in a fresh worktree until you copy or
recreate them. The schema is in the "Model build config" section of `src/README.md`, and the
`model_url` they name is an internal host, so the fetch needs VPN.

Those configs also decide where the output lands, via `output_dir`. It is
`models/<model>/` at the repository root, git-ignored, and deliberately outside the
firmware tree so renaming that tree cannot break the pipeline. The catch is that
`.github/workflows/hardware.yml` rebuilds the same config verbatim from the
`MODEL_CONFIG_DEMO_APPS_KWS` repository secret, so `output_dir` lives partly outside the
repository: changing it means rotating that secret, which no pull request can do.

## Both boards share one Zephyr board target

The AkidaTag board and the nRF5340 DK are both built for `nrf5340dk/nrf5340/cpuapp`, fixed as
`BOARD` in `docker/Dockerfile`. A `src/boards/<board>.conf` therefore cannot tell them
apart, because Zephyr would merge the same file into both builds. What separates the two is
the set of CMake arguments the `--dk` branch of `scripts/run.sh` selects: the devicetree
overlay, the mcuboot overlay, `CONFIG_AKIDATAG_BOARD`, and `EXTRA_CONF_FILE=boards/dk.conf`.
Put any new board-specific Kconfig there rather than in a board-named conf file.

## Imported code lives under `src/deps`

The Akida engine, the FlatBuffers headers and kissfft are all committed, not downloaded and not
generated at configure time. `src/deps/VENDORING.md` is the authority on where each came from, at
which version, what is pruned, and how to upgrade it. Two rules matter more than the detail: never
hand-edit a tree under `src/deps`, put adaptations in `src/core/cmake/akida_engine_setup.cmake`
instead; and put any newly imported tree under `src/deps`, because `PRUNED_DIRS` in
`scripts/clang_format.sh` is a blocklist of top-level directories under `src/`, and `deps` is the
entry that keeps imported code away from clang-format.

The engine is the one to be careful with. `AKIDA_VERSION` is defined only by the engine's own
`cmake/akida-engine.cmake`, so it is single-sourced from the committed tree, though nothing in the
firmware currently reads it. That CMake also globs `src/*.cpp` into the `akida_engine` target, so
the app must never list engine sources itself. `scripts/requirements.txt` pins the same `akida`
version for model conversion; move the two together.

## A model the engine cannot parse makes the board unreachable

The boot path programs the stored model from `main()`. If the AKD1500 engine
rejects its program_info it prints `Unable to parse program info` and never
returns, which starves the Bluetooth RX and shell threads: the board keeps
advertising (the controller is on the network core) but GATT discovery times
out, the shell stops draining its RX ring, and the watchdog reboots it every
8 s. So the one channel that could replace the bad model is the channel the bad
model takes away, and reflashing firmware does not help because the model and
its LittleFS records live on the AKD1500's SPI flash, which `merged.hex` does
not touch.

To recover, break the loop first: build with the boot-time programming skipped
(`hdr_ret = 1` in `main()`, which takes the existing "no model" path), transfer
a good model, then restore. `full_erase` is not a way out; it erases 16 MB from
0x1000, takes minutes, and wipes `/ext` with it.

## The PDM microphone gain is set by hand, and gets clobbered if you set it once

Zephyr's DMIC API has no gain field and `nordic,nrf-pdm` no gain property, so the only way
to set it is writing GAINL/GAINR through `nrf_pdm_gain_set()`. That write has to repeat
after every `dmic_configure()`, because that call ends in `nrfx_pdm_init()`, which restores
the registers from its own default. `dmic_apply_config()` in `src/core/interface/audio/pdm_mic.c`
is the one place that owns this; the constant and the reasoning for its value live on
`AUDIO_MIC_GAIN_DEFAULT` in `src/include/audio/pdm_mic.h`.

Two measured facts worth not rediscovering. The noise floor, around 237 counts of block RMS,
is fixed board noise rather than sound: it does not rise with gain, so gain changes do not
drag the `kws_config.c` speech gate with them. And the board's microphone is quieter than
most, an Infineon IM69D130 at -36 dBFS, so any level comparison against another board has to
start from the two parts' sensitivities.

## Formatting

`.clang-format` only started being honoured at commit 362bc45, so most of the tree is still
formatted against clang-format's LLVM defaults. The `lint` job in
`.github/workflows/ci-gates-lint.yml` gates only the files a pull request changes, so touching a
stale file means reformatting the whole file in its own `style(...)` commit, the way e06eb31 did.
Run `./scripts/clang_format.sh check <files>` inside the Docker image; clang-format is not
installed on the host. The version CI uses is `CLANG_FORMAT_VERSION` in that workflow, and it
has to track the image, which picks clang-format up as an unpinned NCS pip dependency.

Line endings are mixed across the tree and `.clang-format` sets no policy, so preserve
whatever a file already has. Editing with a script that reads and writes text silently
rewrites CRLF to LF, which buries the real change in whole-file churn; clang-format itself
derives the ending per file and leaves it alone.

The same job gates python with `ruff check` plus `ruff format --check` and shell with
`shellcheck`, and the tree is stale against both too, so touching a `.py` or a `.sh` file
drags the same whole-file cleanup. `ruff` lives in the Docker image; `shellcheck` is in
neither the image nor the host, so run it from `koalaman/shellcheck:stable`.

"Changed" means content changed: the job's jq filter keeps only files with `changes > 0`, so
moving a file drags no cleanup with it. Editing one still does, however many files move
alongside it.

## Flashing from macOS

`docs/setup.md` assumes Linux. Docker Desktop on macOS has no USB passthrough, so the `-d`
flash path never sees the debug probe. Flash on the host instead, pointing the script at the
container-built tree:

```sh
BUILD_DIR=build_docker ./scripts/run.sh -f -jf --app demo_apps   # add _dk for the DK build dir
```

`-jf` drives `JLinkExe` directly (NET core first, then APP) and needs no west on the host.
Plain `-f` (`west flash`, nrfjprog) is not usable here: the AkidaTag board's debug header reports
`VTref` around 1.4 V, so nrfjprog aborts with `Low voltage ... detected in target device` even
though SWD reads and writes are reliable. A re-run that prints
`Flash download: ... Skipped. Contents already match` for both banks is the cheapest proof the
device matches the built images.

The USB-C connector reaches an on-board CP2105 dual bridge, so the board presents two serial
ports and only the higher-numbered one carries the debug UART; the other is silent. That wire
also serves MCUboot serial recovery, which is a probe-free way to flash and is documented in
`docs/firmware-update-over-usb.md`. The nRF5340's own USB pins go to a header, not to USB-C.

With only the probe attached there is no console (RTT logging is off in `src/prj.conf`), so
observe the board over BLE: a reset shows as a dropped link, `CMD_APP_INFO` proves the firmware
still answers, and `CMD_STREAM_START` streams the microphone envelope, which tells you whether
the board hears anything at all before you blame a detection or learning path.
`src/utils/edge_learning_hil_test.py` drives the edge-learning state machine that way from a
Mac with no serial port. The learning-started acknowledgement is notified only after
`CMD_DEPLOY_START` on the current connection, so its absence alone is not a failure.
The probe can also read the firmware's own state: `JLinkExe` `connect` does not halt the core,
so `mem32` at a variable's address from `zephyr.elf` reads it live (file-scope C++ statics are
mangled, `cur_kws_edge_state` is `_ZL18cur_kws_edge_state`), and `akd_wake_refs` in `gpio.c`
shows a stranded AKD1500 wake reference. Do not try to prove keyword recognition by playing
audio at the board from the laptop: the microphone is quiet and the result depends on the room,
so a human saying the word is the proof.

Do not assume the image on a board was signed with the key in `.env/`. Read the KEYHASH TLV out
of flash and compare before concluding anything about why an image is refused: the TLV area
starts at `mcuboot_primary` + `hdr_size` + `img_size`, and `imgtool dumpinfo` prints the same
field for a local file. A mismatch means the bootloader on the board embeds a different key,
not that the board is broken.

## The BLE model transfer protocol

`docs/ble-model-transfer.md` is the wire contract between this firmware and the
BrainChip Connect app, and is the authority on it. The transfer stages one flash
sector, carries an absolute offset in every write and a committed position in every
acknowledgement, and reports `DONE` (stored and verified) separately from `READY`
(programmed into the AKD1500 and inferring). Change the firmware and the document
together, and note that the app is a second repository that has to move with them.

## Do not edit

`CHANGELOG.md` and `VERSION` are release-managed. Commit subjects and pull request titles are
checked in CI by `.github/ci-gates/check-subject.sh`, which is the authority on what is
accepted; see `CONTRIBUTING.md`. The old local `commit-msg` hook is gone, so a stale copy left
in `.git/hooks/` by the retired installer enforces rules that no longer apply.

## A cancelled no-mistakes run can strand its gate ref

The pipeline pushes into its own staging repo without `--force`, and its `rebase` step
rewrites your commits, so cancelling a run after that step leaves the gate ref on the
pre-rebase head while `axi sync --recover` moves the local branch to the rebased one. Every
later `axi run` then dies with a non-fast-forward push and no run is ever created, so there is
no gate to respond to. `rerun`, `axi sync --check` and `axi sync --recover --keep-local` all
fail to clear it, the last reporting `recovered: true` with `changed: false`, which reads like
success. Compare the refs rather than trusting that line:

```sh
git ls-remote no-mistakes "refs/heads/<branch>"; git rev-parse HEAD
```

If they have diverged, delete the stale ref and let the next run recreate it, after confirming
the old commit survives locally and on origin: `git push no-mistakes :refs/heads/<branch>`.
Never force-push into the staging repo.

## Maintaining this file

Keep this file for knowledge useful to almost every future agent session in this project.
Do not repeat what the codebase already shows; point to the authoritative file or command instead.
Prefer rewriting or pruning existing entries over appending new ones.
When updating this file, preserve this bar for all agents and keep entries concise.
