# Open-source licences

## The firmware

The AkidaTag firmware is licensed under the terms in the repository's
[LICENSE](../LICENSE) file. At launch that is the Apache License 2.0, with the copyright held by
BrainChip Holdings Ltd. TBD: confirm once the licensing change is merged.

The repository's [NOTICE](../NOTICE) file is the authority on the code the firmware imports: for
every component it records the upstream, the version, the paths, and the licence statement the
files themselves carry. This page is the summary.

## Code imported into the repository

Everything imported lives under `src/deps`, committed rather than downloaded, so that a clone plus
the toolchain image builds the firmware. `src/deps/VENDORING.md` says where each tree came from
and how it is upgraded.

| Component    | Version    | Licence                                                          |
| ------------ | ---------- | ---------------------------------------------------------------- |
| Akida Engine | 2.17.0     | Apache License 2.0, as declared by the library's README.         |
| FlatBuffers  | 2.0.8      | Apache License 2.0.                                              |
| Kiss FFT     | see NOTICE | BSD 3-Clause. TBD: confirm against the SPDX headers in the tree. |

## Code the firmware is built against

The firmware is built with nRF Connect SDK v3.1.1, which is not part of this repository. It
brings the Zephyr RTOS and the MCUboot bootloader, both under the Apache License 2.0, and Nordic
Semiconductor's own components under the Nordic 5-Clause licence. Their licence texts and notices
ship with the SDK.

## The app

BrainChip Connect has its own licences page. TBD: the link, once the app documentation carries
one.

---

© 2026 BrainChip Holdings Ltd. All rights reserved.
