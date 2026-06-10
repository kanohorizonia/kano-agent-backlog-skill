# Native Tokenizer Quickstart

Build the native CLI first:

```bash
pixi run build-dev
```

Run the native tokenizer commands:

```bash
kob tokenizer diagnose
kob tokenizer count --text "Backlog items are markdown files."
kob tokenizer accuracy
kob tokenizer cache-stats
kob tokenizer telemetry
```

The native adapter is `heuristic`. It is intended for deterministic budget
estimation and smoke checks. It does not load Python packages or model files.

To migrate old tokenizer configuration:

```bash
kob tokenizer migrate --input old-tokenizer-config.json --output native-tokenizer.toml
```

The output config uses `heuristic` and records excluded legacy adapter names as
metadata when present.
