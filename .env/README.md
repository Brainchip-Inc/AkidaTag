# Local environment

Everything in this directory is git-ignored except this README and one deliberate exception,
`development_key.pem`. Keep local-only environment files here: per-model build configs
(`<app>/<model>.yaml`), `models.conf`, a private signing key of your own, and anything else that
belongs to your machine rather than to the repository.

## The two signing keys

Firmware is signed with an RSA-3072 key and MCUboot verifies that signature at boot and on DFU.
Two keys exist, and they sit side by side here so it is obvious that they are the same kind of
thing used for different purposes.

| File | Committed? | Secret? | Signs |
| --- | --- | --- | --- |
| `development_key.pem` | Yes, on purpose | No, everyone has it | Every build from this repository |
| `production_key.pem` | Never | Yes | BrainChip's official releases only |

`development_key.pem` is committed so that a fresh clone builds and flashes with no setup step.
It is published, so a signature made with it proves nothing about who produced an image; the file's
own header says so at the top. `src/sysbuild.conf` points `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE` at it
by default.

`production_key.pem` is a real secret and is not in this repository in any form. It exists only
inside a release build: `.github/workflows/release.yml` reconstructs it from the
`PRODUCTION_SIGNING_KEY_PEM_B64` repository secret, writes it here, and repoints
`SB_CONFIG_BOOT_SIGNATURE_KEY_FILE` at it for that build alone. Two `.gitignore` rules keep it out
of the repository, `.env/*` and `*.pem`, and neither has an exception for it.

A key of your own goes here too, under a name of your own, and stays ignored like everything else.
It has to be inside the working tree because a containerised build can see nothing outside it. See
the "Application Security" section of `src/README.md`, which is the reference for which key a board
trusts and how to change it.
