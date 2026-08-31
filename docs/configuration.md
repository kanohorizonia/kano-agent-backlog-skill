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

## Product selectors

Each product can expose configured selectors while retaining its table key as the
canonical storage identity:

```toml
[products.horizon-rpg]
name = "Horizon RPG"
prefix = "HRR"
backlog_root = "_kano/backlog/products/horizon-rpg"
aliases = ["horizon", "horizon-game"]
repo_bindings = ["horizon-rpg-plugin"]
```

Every selector, including a canonical product key, uses only leading/trailing
ASCII whitespace trimming and ASCII `A-Z` lowercasing. KOB does not remove
separators or apply Unicode case folding. Claims from one canonical product are
deduplicated with this precedence: canonical slug, prefix, display name, repo
binding, then explicit alias.

A lookup fails closed when the requested normalized token is owned by multiple
canonical products, even if the original input exactly matches one canonical
table key. Collisions on unrelated tokens do not block safe selectors, so a
different unique selector remains available for registry repair. Relation
operations use the same resolver.

`kob admin init` and `kob migration register-product plan|apply` accept
repeatable `--alias` and `--repo-binding` options. Values are normalized,
deduplicated, sorted, and checked for cross-product collisions before a write.

## Product assignment defaults

Product configuration can define repo-visible actor aliases for new items:

```toml
[product]
default_assignee = "koa"
default_bug_reviewer = "reviewer-koa"
```

`default_assignee` applies to newly created items when the caller does not pass
`--owner` or `--assignee`. `default_bug_reviewer` applies only to new Bug items
when the caller does not pass `--reviewer`.

Product-local `_config/config.toml` overrides project-level `[products.<name>]`
defaults. Created items record whether assignment came from an explicit CLI
argument or from product policy.

## Topic naming policy

Topics should normally be named `YYYY-MM-DD-<slug>` so lifecycle audits and
directory listings expose when the work context started. KOB still accepts legacy
safe slugs, but product configuration can choose how strongly to handle missing
date prefixes:

```toml
[product]
topics_date_prefix_policy = "warn" # off | warn | enforce
```

The same flat key can be set in project config under `[products.<name>]`:

```toml
[products.my-app]
backlog_root = "_kano/backlog/products/my-app"
topics_date_prefix_policy = "enforce"
```

Accepted values are:

- `warn` (default): `kob topic create` succeeds for legacy names and warns to stderr.
- `enforce`: missing `YYYY-MM-DD-` prefixes fail before topic directories/files are created.
- `off`: legacy names succeed without warnings.

`kob topic audit --ttl-days 14 --stale-days 30 --format plain|json|markdown`
is read-only and stdout-only. It reports `mutated: false` in JSON and never
writes audit artifacts, closes topics, cleans materials, deletes topic
directories, distills briefs, switches active topics, or dispatches agents.

## 0.0.3 configuration notes

Release `0.0.3` tightens the effective config flow:

- stable effective config writes to `.kano/cache/effective_backlog_config.toml`
- runtime override output writes to `.kano/cache/effective_runtime_backlog_config.toml`
- local env auto load defaults to `env/local.secrets.env` when present

## References

- [Tokenizer configuration](tokenizer-configuration.md)
- [Actor alias and assignment policy](design/actor-alias-and-assignment-policy.md)
- [Skill reference](../REFERENCE.md)
- [Project config implementation notes](../references/project-config-implementation.md)
- [0.0.3 release notes](releases/0.0.3.md)
