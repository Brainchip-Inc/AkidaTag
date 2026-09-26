# Contributing

Thank you for your interest in AkidaTag.

## Pull requests

This repository is maintained by BrainChip, and pull requests from outside the maintainer
team are welcome when they add value to AkidaTag: a new demo that runs on the board, support
for more of its sensors, a fix for something that does not work, or a clearer guide. The
maintainers review every pull request and close the ones that are not a fit for the project.

Before you open one:

- Say what the change does and why in the description, and how you tested it. Changes to
  the firmware are validated on hardware, so name the board you ran it on, the AkidaTag
  board or the nRF5340 DK.
- Follow the commit format below. CI checks every commit subject and the pull request
  title, and a pull request that fails the `format` check cannot be merged.
- Keep imported code under `src/deps` and record it in `src/deps/VENDORING.md` and
  `NOTICE`, with its licence. `scripts/clang_format.sh` never formats that directory.

The hardware-in-the-loop test runs only when a maintainer asks for it, by commenting
`/dk-test` on the pull request, and only against a branch in this repository. A maintainer
will run it on your change once it is ready.

## Reporting a problem

If the firmware does not build, flash or run for you, please open an
[issue](https://github.com/Brainchip-Inc/AkidaTag/issues). That is the most useful thing
you can send us, and we read every one.

Please search the existing issues first, then include:

- what you ran, and what happened instead of what you expected
- which board: the AkidaTag board, or the nRF5340 DK with the AKD1500 PCIe board
- the firmware version, from the release you flashed or from
  `smpmgr --ble <address> image state-read` on a running board
- whether you built inside the Docker image, and the nRF Connect SDK version if not
  (`v3.1.1` is what `docker/Dockerfile` installs)
- the smallest log excerpt that shows the failure, from the UART shell or from the
  BrainChip Connect app

You do not need to diagnose the cause or propose a fix. A clear description of what broke
is enough for us to work from.

## Questions and ideas

The [BrainChip Developer Hub](https://developer.brainchip.com/signup/) has the tools, model
zoo and documentation for the wider Akida platform, with
[a page for AkidaTag](https://developer.brainchip.com/akida-tag/), and the
[BrainChip Discord](https://discord.com/invite/9bmd9g52vn) is the place for questions,
ideas, and showing us what you have built. The AkidaTag guides are published at
<https://brainchip-inc.github.io/AkidaTag/>.

## Building on this work

Please do. This repository is Apache 2.0 licensed precisely so you can fork it and take it
in your own direction without asking us first. See [LICENSE](LICENSE) and [NOTICE](NOTICE)
for the terms, including those of the imported code under `src/deps`, the nRF Connect SDK
sample files the application grew from, and the components the build fetches.

Two things to know before you ship firmware of your own:

- Every build from this repository is signed with `.env/development_key.pem`, which is
  public. Generate a key of your own and point `src/sysbuild.conf` at it;
  [Application Security](src/README.md#application-security) has the recipe and explains
  which key a board trusts.
- The nRF Connect SDK sample files named in `NOTICE` carry Nordic Semiconductor's licence,
  which restricts them to use with a Nordic Semiconductor integrated circuit.

---

## For maintainers

### The commit format

Every commit subject, and every pull request title, reads:

> *type(scope): concise message*

**Rules:**

- **type** &rarr; required, lowercase, one of the types in the table below.
- **scope** &rarr; optional but recommended, lowercase, one word where possible: `feat(ble):`,
  `fix(kws):`. Omit the parentheses entirely when no scope is meaningful. Append `!` for a
  breaking change: `feat(ble)!: ...`.
- **message** &rarr; says what the change does, in the imperative. It starts lowercase, or with
  an all-capital acronym such as BLE or KWS. A capitalized word such as `Added` is rejected.
- No trailing period. 100 characters maximum, though under 72 stays the habit.

Describe the effect, not the file touched. `fix(kws): handle zero-length audio input` beats
`fix(kws): update kws.c`.

Good:

```
feat(ble): stream PCM waveform over the audio characteristic
fix(gpio): wake the AKD1500 before flash access
docs(setup): describe the macOS flashing path
```

Rejected:

```
Update files                     no type, no scope, says nothing
feat: Added new feature.         past tense, capitalized, trailing period
refact(kws): tidy logging        refact is not a type, refactor is
```

The gate cannot catch a subject that is well formed and still says nothing. `fix(app): fix bug`
passes it and is still a bad subject.

<details>
<summary><b>Allowed types</b></summary>

| Type         | When to use |
|--------------|-------------|
| **feat**     | Adding a new feature or enhancement. |
| **fix**      | Fixing a bug. |
| **docs**     | Documentation-only changes (README, comments, docs folder). |
| **style**    | Code style or formatting changes that do **not** affect functionality. |
| **refactor** | Restructuring code without changing behavior; **use this for most file renames**. |
| **perf**     | Performance improvements. |
| **test**     | Adding or updating tests. |
| **build**    | Build system or tooling changes (Makefile, Dockerfile, scripts). |
| **ci**       | Changes to CI/CD configuration (GitHub Actions, pipelines). |
| **chore**    | Routine maintenance tasks (dependency updates, cleanup). |
| **config**   | Configuration and tooling settings. |
| **revert**   | Reverting a previous commit. |

</details>

`.github/ci-gates/check-subject.sh` is the validator the format check runs, and it is the
authority on what is accepted. Run it on a subject before you push:

```sh
.github/ci-gates/check-subject.sh "feat(ble): add the thing"
```

### What runs on a pull request

Two checks are required before a pull request can merge into `main`:

- **format** checks the pull request title, and on pull requests into `main` every commit
  subject you wrote. Merge commits are skipped.
- **lint** runs ruff, shellcheck, clang-format and prettier over only the files whose
  content the pull request changed; a file it merely moved is skipped. C and C++ go through
  `scripts/clang_format.sh`, so `.clang-format` is the agreed style and imported code under
  `src/deps` is never checked. That script's `PRUNED_DIRS` owns the rule: it prunes
  `src/deps` as a whole, so a newly imported tree belongs inside it.

Pull requests into `release` require **format** only.

The **hardware** check is the hardware-in-the-loop run described in
[README.md](README.md#ci-trigger). It is started by a maintainer with `/dk-test` and is
not required, so an unplugged board never blocks a merge.

### Merging

Pull requests squash into `main`. GitHub builds the squash subject from the pull request
title and appends the number, so the title *is* the commit that lands. The squash body is
blank unless whoever merges writes one.

A release is collected by opening a pull request from `main` into `release` and merging it
with every commit intact, so `release` is the one branch that takes a merge commit.
`CHANGELOG.md` and `VERSION` are release-managed; the release workflow reads them.

### Upgrading from the old local hook

This repo used to ship a `commit-msg` hook installed by `scripts/install_git_hooks.sh`. Both
scripts are gone: the CI gate enforces the same idea, on everyone, without needing to be
installed. The hook also disagreed with the gate in both directions, so keeping it would only
mislead.

If you ran the installer at any point, that copy is still sitting in your clone and will keep
enforcing the old rules. Remove it once:

```sh
rm -f .git/hooks/commit-msg
```
