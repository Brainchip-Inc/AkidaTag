# Local environment

Everything in this directory is git-ignored except this README and one deliberate exception,
`development_key.pem`. Keep local-only environment files here: per-model build configs
(`<app>/<model>.yaml`), `models.conf`, a private signing key of your own, and anything else that
belongs to your machine rather than to the repository.

## The two signing keys

`development_key.pem` is the committed one, and it is the exception above. `production_key.pem` is
never committed; it exists here only inside a release build. A key of your own goes here too, under
a name of your own, and stays ignored like everything else.

See the "Application Security" section of `src/README.md`, which is the reference for what each key
is for, which key a board trusts, and how to change it.
