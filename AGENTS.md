# Project agent memory

This file is the project's committed home for project-intrinsic agent knowledge: build, test, release, architecture, and sharp-edge notes that should travel with the code.

- Add durable project-specific notes here as they are discovered through real work.

## Build

Everything goes through `scripts/run.sh` in the `akidatag-ncs` Docker image; `docs/setup.md`
is the reference. The application app is `demo_apps`, whose source directory is `src/`.

Every build signs, and the key it signs with is committed at
`keys/NOT-SECRET-development-signing-key.pem`, so a fresh clone builds with no setup. That key is
public on purpose and its own header says so. `.github/workflows/release.yml` overrides
`SB_CONFIG_BOOT_SIGNATURE_KEY_FILE` with the private production key from a CI secret, and that one
override is the whole boundary between a local build and a published release. Leave it alone.

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

## Formatting

`.clang-format` only started being honoured at commit 362bc45, so most of the tree is still
formatted against clang-format's LLVM defaults. The `lint` job in
`.github/workflows/ci-gates-lint.yml` gates only the files a pull request changes, so touching a
stale file means reformatting the whole file in its own `style(...)` commit, the way e06eb31 did.
Run `./scripts/clang_format.sh check <files>` inside the Docker image; clang-format is not
installed on the host. The version CI uses is `CLANG_FORMAT_VERSION` in that workflow, and it
has to track the image, which picks clang-format up as an unpinned NCS pip dependency.

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

## Do not edit

`CHANGELOG.md` and `VERSION` are release-managed. Commit subjects and pull request titles are
checked in CI by `.github/ci-gates/check-subject.sh`, which is the authority on what is
accepted; see `CONTRIBUTING.md`. The old local `commit-msg` hook is gone, so a stale copy left
in `.git/hooks/` by the retired installer enforces rules that no longer apply.

## Maintaining this file

Keep this file for knowledge useful to almost every future agent session in this project.
Do not repeat what the codebase already shows; point to the authoritative file or command instead.
Prefer rewriting or pruning existing entries over appending new ones.
When updating this file, preserve this bar for all agents and keep entries concise.
