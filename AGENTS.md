# Developing AkidaTag firmware

This guide is for developers and coding assistants working on the firmware.
Run the commands below from the repository root. See [docs/setup.md](docs/setup.md)
for hardware setup and [src/README.md](src/README.md) for firmware and model details.

## Toolchain

Use Docker with support for `linux/amd64` containers. The image defined in
[docker/Dockerfile](docker/Dockerfile) contains nRF Connect SDK v3.1.1, the Zephyr
build tools, Python 3.12 and the model conversion dependencies. Build it locally:

```sh
./scripts/build_docker_image.sh --ncs v3.1.1 --python 3.12
```

This creates `akidatag-ncs:v3.1.1-py3.12`, the default image used by
[scripts/run.sh](scripts/run.sh). The repository is mounted at `/akidatag` inside
the container, so build outputs are also available on the host. Use
`./scripts/run.sh --help` to see the supported operations.

## Build the existing firmware

The selectable application is `demo_apps`, with its entry point at
[src/apps/demo_apps/main.cpp](src/apps/demo_apps/main.cpp). It combines the
existing demonstrations and shared firmware services.

Build for the AkidaTag board:

```sh
./scripts/run.sh -d -b --app demo_apps
```

Build for the nRF5340 DK into a separate directory:

```sh
BUILD_DIR=build_docker_dk ./scripts/run.sh -d -b --dk --app demo_apps
```

The outputs are `build_docker/demo_apps/` and `build_docker_dk/demo_apps/`,
respectively. Without a `BUILD_DIR` override, both commands use the same output
directory. Each build starts with a pristine configuration.

Both boards use Zephyr's `nrf5340dk/nrf5340/cpuapp` target. `--dk` selects the DK
application and MCUboot overlays, `CONFIG_AKIDATAG_BOARD=n` and
[src/boards/dk.conf](src/boards/dk.conf). Keep board-specific settings with the
appropriate overlay or configuration fragment; the Zephyr target name alone
does not distinguish the two boards.

Ordinary builds use the RSA-3072 development key committed at
`.env/development_key.pem`. It is intentionally public so a fresh clone can
build without generating a key. Anyone can sign with it; its signature does not
establish who produced a firmware image. MCUboot accepts updates only when they
match the key embedded in the bootloader already on the board.

## Flash and check a build

Connect the board to power and a compatible SWD debug probe. Flashing replaces
firmware on the connected board, so check that the build matches the intended
board before running a flash command.

On Linux, the Docker wrapper exposes USB devices to the flashing tools. Use the
J-Link path for a compatible probe:

```sh
./scripts/run.sh -d -f -jf --app demo_apps
BUILD_DIR=build_docker_dk ./scripts/run.sh -d -f -jf --dk --app demo_apps
```

Run only the command for the connected board. On macOS, Docker Desktop does not
provide the USB access used by these commands. Install SEGGER J-Link on the host
and make `JLinkExe` available on `PATH`, then flash the container-built output:

```sh
BUILD_DIR=build_docker ./scripts/run.sh -f -jf --app demo_apps
```

For the DK build, use `BUILD_DIR=build_docker_dk` and add `--dk`. The script loads
`merged_CPUNET.hex` on the network core first and `merged.hex` on the application
core second. These files include the bootloader needed for initial programming.
Do not substitute an application-only image for the initial full flash.

After flashing, confirm the device boots, advertises over BLE and responds to
application commands. Exercise the demo on the actual board before considering
a hardware-dependent change validated. See
[docs/firmware-update-over-usb.md](docs/firmware-update-over-usb.md) for subsequent
USB firmware updates and [docs/ble-model-transfer.md](docs/ble-model-transfer.md)
for the model transfer protocol.

## Prepare a model

The model conversion steps are separate from compiling the firmware. Create a
local `.env/demo_apps/kws.yaml` using the **Model build config** schema in
[src/README.md](src/README.md). These configuration files are not included in a
fresh clone. Set `model_url` to a model URL you can access or a local `.fbz` file,
and use an output directory such as `models/kws`.

When converting in Docker, keep local model files inside the repository and use
repository-relative paths so they are available through the `/akidatag` mount.
Choose the model parameters to match your model; the documented sample values
are not suitable for every model.

```sh
./scripts/run.sh -d \
  --fetch_model .env/demo_apps/kws.yaml \
  --generate_info .env/demo_apps/kws.yaml
```

The first step converts the model. The second writes `info.yaml` and a bundle
such as `models/kws/kws.zip` for the companion app. Generated model outputs belong
under the ignored `models/` directory. Follow the transfer instructions in
`src/README.md` to load a compatible model onto the board.

## Add a demo alongside the existing application

Use `src/apps/demo_apps/` as the reference for an independent application, with
shared drivers and services in `src/core/` and public headers in `src/include/`.
Adding a directory alone does not make a new application selectable. For an app
named `my_demo`, update the following pieces together:

1. Create `src/apps/my_demo/` with its own `main.cpp`, `CMakeLists.txt` and any
   application-specific configuration. Start from the existing demo's
   initialization and service usage, retaining the pieces the new demo needs.
   Its CMake file should add its sources to Zephyr's existing `app` target with
   `target_sources(app PRIVATE ...)`, as the existing demo does. Only one
   application's entry point should be compiled into a build.
2. Add a `MY_DEMO` boolean option to `src/Kconfig`, following `DEMO_APPS`. In
   `src/CMakeLists.txt`, select `FEATURE` as `my_demo` when `CONFIG_MY_DEMO` is
   enabled. Ensure exactly one application is selected. The existing
   `src/apps/CMakeLists.txt` already adds the directory named by `FEATURE`.
3. Update `CONF_FILE` selection in `src/CMakeLists.txt` before
   `find_package(Zephyr ...)`: it currently always includes
   `apps/demo_apps/custom_app.conf`. Select the new app's configuration for
   `my_demo`, retaining the common fragments it needs and preserving the
   existing demo's configuration. Adding only a later `FEATURE` branch would
   still apply the old app's configuration.
4. Add `my_demo` to the app-selection case in `scripts/run.sh`, use `src` as
   `APP_SRC_DIR`, and pass `-DCONFIG_MY_DEMO=y` instead of `CONFIG_DEMO_APPS`.
   Carry over the board-selection arguments for each supported board. Add the
   corresponding entry to `get_jlink_jobs` for J-Link flashing and update the
   help text and available-app error. The build directory is already derived
   from the app name.
5. If the demo needs different model metadata, add the matching profile to
   `APP_PROFILES` in `src/utils/generate_info.py` and document its configuration
   fields. The existing model profile is `demo_apps`; a new firmware app name
   does not automatically create one. Keep any wire-format change consistent
   with `docs/ble-model-transfer.md` and its consumers.

Once these pieces are in place, build with
`./scripts/run.sh -d -b --app my_demo`. Build and exercise the existing
`demo_apps` as well as the new app, on each board the change supports. Keep
reusable functionality in `src/core/` rather than duplicating it in each demo.

## Working conventions

- Read [CONTRIBUTING.md](CONTRIBUTING.md) before proposing a change. Commit
  subjects and pull request titles use Conventional Commits; the accepted
  format is checked by `.github/ci-gates/check-subject.sh`.
- Do not hand-edit imported code under `src/deps/`. Follow
  [src/deps/VENDORING.md](src/deps/VENDORING.md); keep engine integration changes
  in `src/core/cmake/akida_engine_setup.cmake`. The engine CMake owns its source
  list. Keep the engine version and the `akida` dependency in
  `scripts/requirements.txt` compatible.
- Preserve a file's line endings. Check edited C/C++ files with
  `./scripts/clang_format.sh check <files>` inside the toolchain container.
  Python changes need `ruff check` and `ruff format --check`; shell changes need
  `shellcheck`. `.github/workflows/ci-gates-lint.yml` records the versions used
  by CI. Keep formatting-only changes separate from behavior changes.
- Do not manually edit the generated `CHANGELOG.md` or `VERSION`.
- Keep build outputs, local model configurations and credentials out of commits.
  Update this guide when its instructions become inaccurate.
