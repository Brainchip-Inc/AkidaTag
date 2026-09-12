# Local environment (DO NOT COMMIT)

Keep local-only environment files here (per-model build configs, etc.); everything in this directory except this README is git-ignored.

Firmware signing keys do not belong here. Builds sign with the development key committed at
`keys/NOT-SECRET-development-signing-key.pem`, and official releases are signed in CI with a
production key that is not in this repository. See the "Application Security" section of
`src/README.md`.
