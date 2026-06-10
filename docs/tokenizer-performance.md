# Tokenizer Performance

Tokenizer performance checks are native CLI operations.

```bash
bash scripts/kob tokenizer benchmark --format json
bash scripts/kob tokenizer accuracy --format json
bash scripts/kob tokenizer monitor --format json
```

The native tokenizer is stateless and heuristic. Cache and telemetry commands report deterministic native state without relying on Python packages or a Python process-global cache.
