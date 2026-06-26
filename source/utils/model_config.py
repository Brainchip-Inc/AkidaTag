"""Shared loader for per-(app, model) config files.

A model config lives at .env/<app>/<model>.yaml (local-only, git-ignored) and is
the single source of truth for fetch_model.py (conversion params) and
generate_info.py (info.yaml params). Both scripts take only --config and read
every value from this file. See the "Model build config" section of
source/README.md for the schema.
"""

import os
import sys
import types


def load_model_config(path):
    """Load a model config YAML and return it as a dict.

    Exits with a clear message if the file is missing or does not parse to a
    mapping. yaml is imported lazily so callers that never touch a config (none,
    currently) don't pay for the dependency.
    """
    if not path or not os.path.exists(path):
        sys.exit(f"Error: config file not found: {path}\n"
                 f"Create it under .env/<app>/<model>.yaml (schema in source/README.md).")
    import yaml  # lazy import
    with open(path) as f:
        data = yaml.safe_load(f)
    if not isinstance(data, dict):
        sys.exit(f"Error: config file did not parse to a mapping: {path}")
    return data


def require_keys(cfg, path, keys):
    """Exit listing any required keys missing from cfg (so errors name them)."""
    missing = [k for k in keys if cfg.get(k) is None]
    if missing:
        sys.exit(f"Error: config '{path}' is missing required keys: "
                 f"{', '.join(missing)}")


def resolve_args(cfg, schema):
    """Build a SimpleNamespace of args from cfg using a schema.

    schema maps arg attribute name -> (config_key, default). The value is taken
    from cfg[config_key] when present, otherwise the default. This lets the
    existing script bodies keep reading args.<name> unchanged.
    """
    ns = types.SimpleNamespace()
    for attr, (key, default) in schema.items():
        val = cfg.get(key)
        setattr(ns, attr, default if val is None else val)
    return ns
