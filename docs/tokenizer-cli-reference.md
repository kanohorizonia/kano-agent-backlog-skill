# Native Tokenizer CLI Reference

The tokenizer CLI is native C++ only. The built-in adapter is the deterministic
`heuristic` adapter. In-process Python providers such as `tiktoken`,
HuggingFace transformers, and sentence-transformers are outside the executable
contract for this milestone.

## Commands

```bash
kob tokenizer diagnose
kob tokenizer count --text "sample text"
kob tokenizer estimate-file README.md
kob tokenizer cache-stats
kob tokenizer accuracy --format json
kob tokenizer compare --text "sample text"
kob tokenizer migrate --input old-tokenizer-config.json --output native-tokenizer.toml
kob tokenizer telemetry
kob tokenizer telemetry-export --output tokenizer-telemetry.json
kob tokenizer telemetry-clear
kob tokenizer monitor
kob tokenizer alerts
kob tokenizer install-guide
kob tokenizer adapter-status --adapter heuristic
```

## Native Policy

- `heuristic` is always available.
- Cache and telemetry commands report deterministic native state.
- `install` and `install-guide` explain native provider policy; they do not
  install Python packages.
- `migrate` preserves excluded legacy adapter names as metadata and writes a
  native default configuration.

Exact-token behavior should be added later through a native provider adapter,
not by restoring Python runtime dependencies.
