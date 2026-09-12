# Local environment (DO NOT COMMIT)

Keep local-only environment files here (per-model build configs, a private signing key of your
own, etc.); everything in this directory except this README is git-ignored.

The development key every build signs with is not here: it is committed at
`keys/NOT-SECRET-development-signing-key.pem`, and official releases are signed in CI with a
production key that is not in this repository at all. This directory is where a key you generate
yourself goes, and where CI writes the production key during a release. See the "Application
Security" section of `src/README.md`.
