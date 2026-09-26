# AkidaTag firmware release notes

Firmware releases are published at https://github.com/Brainchip-Inc/AkidaTag/releases. Every
release carries its full list of changes in its release body, taken from the repository's
[CHANGELOG.md](../CHANGELOG.md), which is the authority. This page is the short version: what
each release was for, what is known to be wrong with it, and what each attached file does.

## Versions

Versions follow MCUboot's `major.minor.revision+build` form, the version the tag's bootloader
reads out of the image header. The build counter resets to `0` whenever `major.minor.revision`
changes. The app shows the same version on its firmware update screen.

## Releases

| Release    | Date       | What it was for                                                                                                                                                                                              |
| ---------- | ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `v1.2.0+0` | 2026-09-14 | Pre-release. The product rename from Spark to AkidaTag, firmware update over the USB-C cable with no probe and no button, a build that works from a fresh clone, and the Akida engine committed to the tree. |
| `v1.1.1+0` | 2026-08-25 | Patch release fixing the two defects that shipped in `v1.1.0+0`: a model update over Bluetooth never completed, and the DK build did not compile.                                                            |
| `v1.1.0+0` | 2026-08-25 | Pre-release. Power measurement and power control on the board, a rewritten keyword-spotting scoring path, and the first hardware-in-the-loop CI job.                                                         |
| `v1.0.0+0` | 2026-04-07 | First alpha of the firmware platform.                                                                                                                                                                        |

Releases before `v1.2.0+0` carry the product's earlier name, Spark, in their file names.

### Known issues in `v1.2.0+0`

- **A tag on `v1.2.0+0` cannot load a model from BrainChip Connect `v1.0.0+0`.** The app speaks
  the block-by-block model transfer that landed on `main` after the release. Update the firmware
  before loading a model. TBD: the first release that carries the new transfer.
- **Turning Edge Learning on in the app restarts the tag.** Fixed on `main`; not yet released.

### On `main`, not yet released

- The block-by-block model transfer the app uses, with every write carrying its position and the
  tag acknowledging what it has stored.
- The edge learning toggle fault above.
- The microphone gain set for keyword spotting.

## What each release file is for

| File                            | Use it for                                                                                                                                                              |
| ------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `akidatag-<version>.signed.bin` | Updating from the app over Bluetooth, or from a computer over USB-C. The application image only, signed with BrainChip's production key.                                |
| `akidatag-<version>-dfu.zip`    | The same image packaged with a manifest. The app accepts either file.                                                                                                   |
| `akidatag-<version>.signed.hex` | The same image for a programmer that takes Intel HEX.                                                                                                                   |
| `akidatag-<version>-merged.hex` | Flashing a board with a debug probe: the bootloader plus the application. This is the only file that changes which signing key a board trusts. See the developer guide. |
| `SHA256SUMS.txt`                | Checking a download: the checksum of every file above.                                                                                                                  |

TBD: the model package attached to each release, and its name.

---

© 2026 BrainChip Holdings Ltd. All rights reserved.
