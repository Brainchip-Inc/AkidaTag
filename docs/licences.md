# Open-source licences

## The firmware

The AkidaTag firmware is licensed under the Apache License 2.0. The text is the repository's
[LICENSE](../LICENSE) file, and the copyright is held by BrainChip Holdings Ltd.

The repository's [NOTICE](../NOTICE) file is the authority on the code the firmware imports: for
every component it records the upstream, the version, the paths, and the licence statement the
files themselves carry. This page is the summary.

## Code imported into the repository

Everything imported lives under `src/deps`, committed rather than downloaded, so that a clone plus
the toolchain image builds the firmware. `src/deps/VENDORING.md` says where each tree came from
and how it is upgraded.

| Component                    | Version                | Licence            |
| ---------------------------- | ---------------------- | ------------------ |
| Akida Engine                 | 2.17.0                 | Apache License 2.0 |
| FlatBuffers                  | 2.0.8                  | Apache License 2.0 |
| Kiss FFT, float-only variant | unrecorded; see NOTICE | BSD 3-Clause       |

NOTICE also records three smaller items outside `src/deps`: the MFCC feature extraction adapted
from Arm's ML-KWS-for-MCU (Apache License 2.0), five build and sample files that carry the nRF
Connect SDK sample header (Nordic 5-Clause licence), and the keyword-spotting sample input
derived from the Google Speech Commands dataset (Creative Commons Attribution 4.0).

## Code the firmware is built against

The firmware is built with nRF Connect SDK v3.1.1, which is not part of this repository. It
brings the Zephyr RTOS and the MCUboot bootloader, both under the Apache License 2.0, and Nordic
Semiconductor's own components under the Nordic 5-Clause licence. Their licence texts and notices
ship with the SDK.

## The app

BrainChip Connect is documented separately at https://brainchip-inc.github.io/BrainChip-Connect/.

---

© 2026 BrainChip Holdings Ltd. All rights reserved.
