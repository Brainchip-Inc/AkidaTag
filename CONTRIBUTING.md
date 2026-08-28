# Contributing

Thanks for contributing! 🤝

This project uses one commit message convention, and it is enforced in CI rather than
suggested. A pull request whose title or commits do not follow it cannot be merged.

---

## The format

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

### Allowed types

| Type         | When to Use |
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

---

## What runs on a pull request

Two checks are required before a pull request can merge into `main`:

- **format** checks the pull request title, and on pull requests into `main` every commit
  subject you wrote. Merge commits are skipped.
- **lint** runs ruff, shellcheck, clang-format and prettier over only the files the pull
  request changed. C and C++ go through `scripts/clang_format.sh`, so `.clang-format` is the
  agreed style and `source/external` is never checked.

Pull requests into `release` require **format** only.

`.github/ci-gates/check-subject.sh` is the validator the format check runs, and it is the
authority on what is accepted. Run it on a subject before you push:

```sh
.github/ci-gates/check-subject.sh "feat(ble): add the thing"
```

---

## Merging

Pull requests squash into `main`. GitHub builds the squash subject from the pull request
title and appends the number, so the title *is* the commit that lands. The squash body is
blank unless whoever merges writes one.

A release is collected by opening a pull request from `main` into `release` and merging it
with every commit intact, so `release` is the one branch that takes a merge commit.

---

## Upgrading from the old local hook

This repo used to ship a `commit-msg` hook installed by `scripts/install_git_hooks.sh`. Both
scripts are gone: the CI gate enforces the same idea, on everyone, without needing to be
installed. The hook also disagreed with the gate in both directions, so keeping it would only
mislead.

If you ran the installer at any point, that copy is still sitting in your clone and will keep
enforcing the old rules. Remove it once:

```sh
rm -f .git/hooks/commit-msg
```
