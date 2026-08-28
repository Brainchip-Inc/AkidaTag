# Project agent memory

This file is the project's committed home for project-intrinsic agent knowledge: build, test, release, architecture, and sharp-edge notes that should travel with the code.

- Add durable project-specific notes here as they are discovered through real work.

## Build

Everything goes through `scripts/run.sh` in the `spark-ncs` Docker image; `docs/setup.md`
is the reference. The application app is `demo_apps`, whose source directory is `source/`.

## Both boards share one Zephyr board target

The spark board and the nRF5340 DK are both built for `nrf5340dk/nrf5340/cpuapp`, fixed as
`BOARD` in `docker/Dockerfile`. A `source/boards/<board>.conf` therefore cannot tell them
apart, because Zephyr would merge the same file into both builds. What separates the two is
the set of CMake arguments the `--dk` branch of `scripts/run.sh` selects: the devicetree
overlay, the mcuboot overlay, `CONFIG_SPARK_BOARD`, and `EXTRA_CONF_FILE=boards/dk.conf`.
Put any new board-specific Kconfig there rather than in a board-named conf file.

## Formatting

`.clang-format` only started being honoured at commit 362bc45, so most of the tree is still
formatted against clang-format's LLVM defaults. The `lint` job in
`.github/workflows/ci-gates-lint.yml` gates only the files a pull request changes, so touching a
stale file means reformatting the whole file in its own `style(...)` commit, the way e06eb31 did.
Run `./scripts/clang_format.sh check <files>` inside the Docker image; clang-format is pinned to
22.1.1 and is not installed on the host.

## Flashing from macOS

`docs/setup.md` assumes Linux. Docker Desktop on macOS has no USB passthrough, so the `-d`
flash path never sees the debug probe. Flash on the host instead, pointing the script at the
container-built tree:

```sh
BUILD_DIR=build_docker ./scripts/run.sh -f -jf --app demo_apps   # add _dk for the DK build dir
```

`-jf` drives `JLinkExe` directly (NET core first, then APP) and needs no west on the host.
Plain `-f` (`west flash`, nrfjprog) is not usable here: the spark board's debug header reports
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
