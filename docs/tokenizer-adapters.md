# Tokenizer Adapters

The supported tokenizer surface is native C++ only.

```bash
bash scripts/kob tokenizer status
bash scripts/kob tokenizer adapter-status
bash scripts/kob tokenizer compare "Sample text"
```

The built-in `heuristic` adapter is available. Python in-process adapters such as `tiktoken`, HuggingFace, and sentence-transformers are not part of the executable contract. Future exact-token behavior should be added as native provider adapters.
