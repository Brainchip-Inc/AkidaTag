# Local environment secrets (DO NOT COMMIT)

This directory is used for **local-only secrets** required during development,
such as MCUboot / imgtool signing keys.

## What belongs here
- `signing_key.pem` – PRIVATE key used to sign images during development/testing
- `models.conf` – Model download URLs for the build system (see below)

## What must NOT be committed
- Any private key material (`*.pem`, `*.key`, etc.)
- Production or release signing keys

The repository `.gitignore` ensures this directory is ignored
except for this README.

## Model URLs (`models.conf`)

During the build, `fetch_model.py` downloads Akida `.fbz` models from an
internal server (VPN required). URLs are configured in `.env/models.conf`.

Create the file with one URL per model:

```
# .env/models.conf
MODEL_KWS_URL=http://salesdata.brainchipinc.local/spark/ml/artifacts/speech_commands/akida/ds_cnn_uint8_input/run17_1/akida_model.fbz
MODEL_MNIST_URL=http://salesdata.brainchipinc.local/spark/ml/artifacts/.../akida_model.fbz
```

The variable name format is `MODEL_<NAME>_URL` where `<NAME>` matches the
`--model` argument passed to `fetch_model.py` (uppercased, hyphens become
underscores).

## How to generate a dev signing key
From the repo root:

```
./scripts/run.sh --key
# or inside Docker
./scripts/run.sh -d --key
```
