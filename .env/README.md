# Local environment secrets (DO NOT COMMIT)

This directory is used for **local-only secrets** required during development,
such as MCUboot / imgtool signing keys.

## What belongs here
- `signing_key.pem` – PRIVATE key used to sign images during development/testing

## What must NOT be committed
- Any private key material (`*.pem`, `*.key`, etc.)
- Production or release signing keys

The repository `.gitignore` ensures this directory is ignored
except for this README.

## How to generate a dev signing key
From the repo root:

```
./scripts/run.sh --key
# or inside Docker
./scripts/run.sh -d --key
```
