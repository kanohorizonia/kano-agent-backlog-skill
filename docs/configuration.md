# Configuration

`kano-agent-backlog-skill` stays local first by keeping tracked defaults, product configuration, and ignored local overrides close to the repository.

## What to configure first

- product and backlog root initialization via `kob admin init`
- cache location if you do not want the default under `.kano/cache/`
- local environment secrets through ignored env files
- optional tokenizer, indexing, and search settings only if you actually need them

## Important paths

- `.kano/cache/` for derived cache output
- `_kano/backlog/_shared/logs/` for shared logs
- `_kano/backlog/products/<product>/_config/config.toml` for product-scoped settings

These derived paths should be ignored in version control unless your team has a deliberate reason to keep selected generated artifacts.

## 0.0.3 configuration notes

Release `0.0.3` tightens the effective config flow:

- stable effective config writes to `.kano/cache/effective_backlog_config.toml`
- runtime override output writes to `.kano/cache/effective_runtime_backlog_config.toml`
- local env auto load defaults to `env/local.secrets.env` when present

## References

- [Tokenizer configuration](tokenizer-configuration.md)
- [Skill reference](../skill/reference.md)
- [Project config implementation notes](../references/project-config-implementation.md)
- [0.0.3 release notes](../releases/0.0.3.md)
