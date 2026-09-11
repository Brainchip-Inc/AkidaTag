# Imported code

Everything under `src/deps` is code this repository tracks but does not author.
It is committed rather than downloaded or generated, so that a clone plus the
`akidatag-ncs` toolchain image is enough to build the firmware, and so that
moving to a new version is a reviewable diff instead of five manual steps.

Two rules keep that promise:

- **Never hand-edit a tree listed here.** Adaptations belong in BrainChip's own
  CMake, under `src/core/cmake/`. Each tree stays byte-identical to what the
  tool or the release emits, because that is the only thing that makes an
  upgrade diff readable.
- **Record every new tree here, and add it to `LINT_EXCLUDE_REGEX` in
  `.github/ci-gates/ci-gates.conf`, in the pull request that lands it.** The
  lint gate is a blocklist, so a tree nobody lists is handed to clang-format on
  every pull request that touches it.

---

## `akida/engine` — Akida Engine 2.17.0

| | |
|---|---|
| Upstream | the `akida` Python package, <https://pypi.org/project/akida/> |
| Version | 2.17.0 |
| Command | `akida engine deploy --dest-path .`, run from `src/deps/akida` |
| Tracked | 90 files, 556 KB |

The version is asserted by the tree itself, in `engine/cmake/akida-engine.cmake`:
`AKIDA_VERSION="2.17.0"` and `VERSION 2.17.0`. That file is the single source of
the version the firmware reports, via `engine/src/version.cpp` and
`akida::version()`. Nothing else defines `AKIDA_VERSION`.

`scripts/requirements.txt` separately pins `akida==2.17.0` for the model
conversion pipeline. It no longer decides what the firmware runs, but a model
converted by one version and executed by another is not supported, so move the
two together.

**Pruned:** `engine/test/`, by `.gitignore`. `akida engine deploy` has no flag to
skip it, and no build compiles it. It is 10 MB, of which
`test/akd1000/test_fnp2/program.cpp` alone is 9.3 MB of fixtures for the AKD1000,
a chip this firmware does not target.

**To upgrade:** bump `akida` in `scripts/requirements.txt` and in the toolchain
image, then from a container on the new version:

```sh
rm -rf src/deps/akida/engine
cd src/deps/akida && akida engine deploy --dest-path .
```

Commit the result on its own and read the diff. A source file appearing or
disappearing needs no build change: the engine's own CMake globs `src/*.cpp`.

---

## `flatbuffers/include` — FlatBuffers 2.0.8

| | |
|---|---|
| Upstream | <https://github.com/google/flatbuffers/archive/v2.0.8.tar.gz> |
| sha256 | `f97965a727d26386afaefff950badef2db3ab6af9afe23ed6d94bfb65f95f37e` |
| Version | 2.0.8, asserted by `include/flatbuffers/base.h` |
| Tracked | 31 files, 480 KB |

`include/` is byte-identical to the release tarball's `include/`: no file is
absent and none is added.

**Pruned:** everything else in the 14 MB tarball. Only headers are consumed.
No FlatBuffers library is linked, and the upstream build's tests and `flatc`
were already switched off before this tree was committed.

The engine's own `cmake/akida-engine.cmake` declares FlatBuffers as a
`FetchContent` download. It is not edited; `src/core/cmake/akida_engine_setup.cmake`
sets `FETCHCONTENT_SOURCE_DIR_FLATBUFFERS` to this directory, which tells CMake
to satisfy that declaration locally and skip the download.

**To upgrade:** extract a new release's `include/` over this one, refresh the URL
and checksum above, and check that the engine version in use still expects that
FlatBuffers version.

---

## `kissfft` — Kiss FFT, adapted

| | |
|---|---|
| Upstream | <https://github.com/mborgerding/kissfft> |
| Version | not recorded when it was first vendored |
| Tracked | 4 files, 28 KB |

Unlike the other two trees, this is not a pristine copy. It is a float-only
reduction of upstream: `_kiss_fft_guts.h` is folded into `kiss_fft.c`, the
fixed-point, SIMD and OpenMP paths and the allocator macros are gone, the files
are formatted to this repository's `.clang-format`, and SPDX headers were added.
Comparing token by token against the two nearest releases, it is closer to
`v131` than to `v130`, but it is not derived cleanly from either, and this
repository's history does not record which revision it came from.

So treat it as this repository's own float-only variant of Kiss FFT, kept here
because it is imported arithmetic nobody should reformat or rewrite, not because
a newer upstream can be dropped over it. Adopting a later upstream means
redoing the reduction deliberately.

It was moved here unchanged from `src/core/common/audio/kissfft/`.
`src/core/common/audio/mfcc.c` is its only consumer.

---

Copyright and licence statements for all three trees are in the repository's
`NOTICE`.
