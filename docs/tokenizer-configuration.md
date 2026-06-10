# Native Tokenizer Configuration

Tokenizer configuration is native-only for this milestone.

```toml
[tokenizer]
adapter = "heuristic"
model = "native-heuristic"
fallback_chain = ["heuristic"]
cache_enabled = false
telemetry_enabled = false
```

## Supported Values

| Key | Supported value | Notes |
| --- | --- | --- |
| `adapter` | `heuristic` | Built into the native CLI. |
| `model` | `native-heuristic` | Descriptive identifier for native estimates. |
| `fallback_chain` | `["heuristic"]` | Python providers are not part of the chain. |
| `cache_enabled` | `false` | Native tokenizer is stateless. |
| `telemetry_enabled` | `false` | Telemetry commands return deterministic empty/native state. |

Legacy values such as Python `tiktoken`, HuggingFace, and
sentence-transformers may appear in migrated config metadata as
`previous_adapter`, but they are not executable providers.
