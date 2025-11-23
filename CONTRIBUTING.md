# Contributing

Thanks for contributing! 🤝

This project uses a simple commit message convention and a local Git hook to keep history clean and readable.

---

## Install local Git hooks (run once after clone)

After cloning the repo, run:

`./scripts/install_git_hooks.sh`

---

## Commit Message Guidelines

Each commit message must follow this structure:

>*type(scope): message with at least three words*

**Format Rules:**

- **type** &rarr; required (must be one of the allowed types below)
- **scope** &rarr; optional but recommended (e.g., `filenames`)  
- **message** &rarr; must contain **at least 3 words**  
- The Git hook installed via `./scripts/install-git-hooks.sh` will reject invalid messages.

### Allowed Types

| Type        | When to Use |
|-------------|-------------|
| **feat**    | Adding a new feature or enhancement. |
| **fix**     | Fixing a bug. |
| **docs**    | Documentation-only changes (README, comments, docs folder). |
| **style**   | Code style or formatting changes that do **not** affect functionality. |
| **refactor**| Restructuring code without changing behavior; **use this for most file renames**. |
| **perf**    | Performance improvements. |
| **test**    | Adding or updating tests. |
| **build**   | Build system or tooling changes (Makefile, Dockerfile, scripts). |
| **ci**      | Changes to CI/CD configuration (GitHub Actions, pipelines). |
| **chore**   | Routine maintenance tasks (dependency updates, cleanup). |
| **revert**  | Reverting a previous commit. |