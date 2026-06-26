# Local environment secrets (DO NOT COMMIT)

This directory is used for **local-only secrets** required during development,
such as MCUboot / imgtool signing keys.

## What belongs here
- `signing_key.pem` – PRIVATE key used to sign images during development/testing
- `models.conf` – Model download URLs for the build system (see below)
- `<app>/<model>.yaml` – Per-(app, model) build configs for `fetch_model.py` /
  `generate_info.py` (see "Model build configs" below)

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

## Model build configs (`.env/<app>/<model>.yaml`)

`fetch_model.py` (model conversion) and `generate_info.py` (info.yaml generation) take **only**
`--config` — every parameter comes from a per-(app, model) YAML file. This keeps model URLs and
tuning values out of git and lets CI run the same flow as local dev. The file is laid out as
`.env/<app>/<model>.yaml`, e.g. `.env/demo_apps/kws.yaml` and
`.env/demo_apps/kws_edge_learning.yaml`.

One self-contained file holds both the conversion fields (used by `fetch_model.py`) and the
info.yaml fields (used by `generate_info.py`):

```yaml
# .env/demo_apps/kws.yaml
app: demo_apps                                   # generate_info profile (selects which fields info.yaml carries)
model_name: kws                                  # file prefix for the generated bins/cpp/.h + shapes sidecar
output_dir: source/external/model_files/kws      # where converted artifacts + info.yaml are written
model_url: http://<internal-host>/path/to/akida_model.fbz   # .fbz to download (VPN required)
map_mode: 2                                      # Akida MapMode (optional, default 1)
neurons_per_class: 1                             # regular kws: 1, edge-learning: e.g. 10
num_el_classes: 0                                # edge-learning novel classes (regular: 0)
flash_address: "0x101000"                        # quote so info.yaml keeps the hex form
mfcc_fs: 123.56967163085938                      # MFCC normalisation scalar
silence_class: 10
unknown_class: 11
inference_mode: async                            # sync | async (DK board falls back to sync)
```

Required keys: `app`, `model_name`, `output_dir` (plus everything the chosen `app` profile
requires — for `demo_apps`: `flash_address`, `neurons_per_class`, `num_el_classes`, `mfcc_fs`,
`silence_class`, `unknown_class`, `inference_mode`). The scripts report any missing keys by name.

Usage (via `run.sh`; the akida SDK lives in the Docker image so use `-d` for the fetch step):

```
# fetch + convert, then generate info.yaml in one go (config named on each step)
./scripts/run.sh -d \
    --fetch_model .env/demo_apps/kws.yaml \
    --generate_info .env/demo_apps/kws.yaml
```

**Reused models:** when several apps use the same model, point each app's file at the same
`output_dir` (the bins are produced once); only the info.yaml fields differ. Apps that hardcode
their values simply have no file here.

**CI:** the HIL workflow needs `.env/demo_apps/kws.yaml`, but `actions/checkout` wipes
git-ignored files (`git clean -ffdx`) on every run, so it can't just live on the runner. Instead
the workflow writes it from a **repo secret** before the fetch step:

- Secret name: `MODEL_CONFIG_DEMO_APPS_KWS`
- Secret value: the full contents of `.env/demo_apps/kws.yaml` (paste the YAML)

Set it under **Settings → Secrets and variables → Actions → New repository secret**. To change a CI
model parameter, edit that secret. Add a new secret per (app, model) config you want CI to use.
(Repo secrets are not exposed to PRs from forks — fine for this internal repo.)

## How to generate a dev signing key
From the repo root:

```
./scripts/run.sh --key
# or inside Docker
./scripts/run.sh -d --key
```
