# Tokenizer Troubleshooting

Use the native CLI diagnostics:

```bash
bash scripts/kob tokenizer diagnose --verbose
bash scripts/kob tokenizer dependencies
bash scripts/kob tokenizer health-check --format json
```

If a stale Python-installed `kano-backlog` command reports missing modules, run from the repo root with `bash scripts/kob` after `pixi run build-dev`. The repo-local launcher does not fall back to Python.
