# Token-Aware Overlap

The former Python tokenizer/chunking API has been removed from this repo.

Native chunking and tokenizer behavior now live in the C++ CLI:

```bash
bash scripts/kob tokenizer status
bash scripts/kob chunks build --help
bash scripts/kob chunks query --help
```

Current native tokenizer behavior is heuristic and deterministic. Exact external tokenizer providers require future native adapters.
