# Embedding Pipeline Configuration

The embedding pipeline enables semantic search and similarity analysis across backlog items through vector embeddings. This document describes the TOML configuration schema for the pipeline components.

## Configuration Structure

The embedding pipeline configuration consists of four main sections:

- `[chunking]` - Text segmentation settings
- `[tokenizer]` - Token counting and budget management
- `[embedding]` - Vector embedding generation
- `[vector]` - Vector storage and retrieval

## [chunking] Section

Controls how documents are split into chunks for processing.

```toml
[chunking]
target_tokens = 256      # Target tokens per chunk
max_tokens = 512         # Maximum tokens per chunk (hard limit)
overlap_tokens = 32      # Token overlap between adjacent chunks
version = "chunk-v1"     # Chunking algorithm version
```

### Parameters

- **target_tokens** (integer, default: 256): Preferred number of tokens per chunk. The chunker aims for this size but may create smaller chunks at natural boundaries.
- **max_tokens** (integer, default: 512): Hard limit on chunk size. Chunks exceeding this limit will be truncated.
- **overlap_tokens** (integer, default: 32): Number of tokens to overlap between consecutive chunks to preserve context across boundaries.
- **version** (string, default: "chunk-v1"): Algorithm version identifier for reproducible chunking behavior.

### Constraints

- `overlap_tokens` must be less than `target_tokens`
- `target_tokens` must be less than or equal to `max_tokens`
- All values must be positive integers

## [tokenizer] Section

Configures token counting for budget management and chunking.

```toml
[tokenizer]
adapter = "heuristic"              # Tokenizer adapter type
model = "text-embedding-3-small"   # Model for token counting
max_tokens = 8192                  # Maximum tokens per model context (optional)
```

### Parameters

- **adapter** (string, default: "heuristic"): Tokenizer implementation
  - `"heuristic"`: Fast approximation based on character counts and language detection
- **model** (string, default: "text-embedding-3-small"): Model identifier for token counting rules
- **max_tokens** (integer, optional): Override model's default context limit

Python in-process tokenizers such as `tiktoken` and HuggingFace tokenizers are not part of the native executable contract. Exact token behavior must be added through future native adapters.

### Available Models

Common models and their default token limits:
- `text-embedding-3-small`: 8,192 tokens
- `text-embedding-3-large`: 8,192 tokens
- `text-embedding-ada-002`: 8,192 tokens

## [embedding] Section

Configures vector embedding generation.

```toml
[embedding]
provider = "noop"           # Embedding provider
model = "noop-embedding"    # Embedding model
dimension = 1536            # Vector dimension
```

### Parameters

- **provider** (string, default: "noop"): Embedding service provider
  - `"noop"`: Native placeholder provider used by the current executable contract
- **model** (string): Model identifier for embedding generation
- **dimension** (integer, default: 1536): Vector dimension size

The current native executable records embedding provider metadata, but build/query/status use deterministic native noop/heuristic behavior. Hosted or local embedding providers must be implemented as native adapters before becoming supported runtime providers. Python in-process providers are retired.

### Provider-Specific Models

**NoOp Provider**:
- `noop-embedding`: Native placeholder model for deterministic smoke tests and local-first indexing metadata

**Future native provider adapters**:
- Hosted providers such as OpenAI or Gemini may be supported only through native adapters.
- Local model providers must not require Python packages or in-process Python runtimes.

### Additional Options

Provider-specific options can be added under `[embedding.options]`:

```toml
[embedding.options]
timeout = 30                # Reserved for future native hosted providers
```

Python package cache variables are intentionally out of scope for this native executable milestone.

## [vector] Section

Configures vector storage and retrieval.

```toml
[vector]
backend = "sqlite"      # Vector storage backend
path = ".cache/vector"  # Storage path (relative to product root)
collection = "backlog"  # Collection/table name
metric = "cosine"       # Distance metric for similarity
```

### Parameters

- **backend** (string, default: "sqlite"): Vector storage implementation
  - `"noop"`: In-memory storage for testing
  - `"sqlite"`: SQLite-based persistent storage
- **path** (string, default: ".cache/vector"): Storage location
  - Relative paths are resolved from the product root
  - Absolute paths are used as-is
- **collection** (string, default: "backlog"): Collection or table name for organizing vectors
- **metric** (string, default: "cosine"): Distance metric for similarity calculations
  - `"cosine"`: Cosine similarity (recommended for most text embeddings)
  - `"l2"`: Euclidean distance
  - `"ip"`: Inner product

### Backend-Specific Options

Additional options can be configured under `[vector.options]`:

```toml
[vector.options]
# SQLite-specific options
timeout = 10.0              # Connection timeout
journal_mode = "WAL"        # SQLite journal mode
```

## Configuration Examples

### Testing/Development Profile

Minimal configuration for local development and testing:

```toml
[chunking]
target_tokens = 256
max_tokens = 512
overlap_tokens = 32

[tokenizer]
adapter = "heuristic"
model = "text-embedding-3-small"

[embedding]
provider = "noop"
model = "noop-embedding"
dimension = 1536

[vector]
backend = "sqlite"
path = ".cache/vector"
collection = "backlog"
metric = "cosine"
```

### Hosted Provider Placeholder

Configuration metadata for a future hosted native provider:

```toml
[chunking]
target_tokens = 512
max_tokens = 1024
overlap_tokens = 64

[tokenizer]
adapter = "heuristic"
model = "text-embedding-3-small"
max_tokens = 8192

[embedding]
provider = "openai"
model = "text-embedding-3-small"
dimension = 1536

[embedding.options]
timeout = 30

[vector]
backend = "sqlite"
path = ".cache/vector"
collection = "backlog"
metric = "cosine"
```

This profile records intended provider metadata. It does not enable a Python provider or bypass the current native noop/heuristic runtime behavior.

### High-Capacity Configuration

For large documents and detailed analysis:

```toml
[chunking]
target_tokens = 1024
max_tokens = 2048
overlap_tokens = 128

[tokenizer]
adapter = "heuristic"
model = "text-embedding-3-large"
max_tokens = 8192

[embedding]
provider = "openai"
model = "text-embedding-3-large"
dimension = 3072

[vector]
backend = "sqlite"
path = ".cache/vector"
collection = "backlog"
metric = "cosine"
```

## Configuration Loading

The pipeline configuration is loaded through the standard config system:

1. **Base configuration**: Default values from `PipelineConfig` class
2. **Product config**: Values from `_kano/backlog/products/{product}/_config/config.toml`
3. **Environment overrides**: Environment variables (if supported)

### Config File Location

Place embedding configuration in your product's config file:

```
_kano/backlog/products/{product}/_config/config.toml
```

### Validation

The configuration is validated when loaded:
- Required fields must be present
- Numeric values must be within valid ranges
- Provider and model combinations must be supported
- Tokenizer and embedding models must be compatible

### Debugging Configuration

Use the CLI to inspect the effective configuration:

```bash
kano-backlog config show --product {product}
```

## CLI Integration

The embedding pipeline integrates with the CLI through these commands:

```bash
# Build index for entire product
kano-backlog embedding build --product {product}

# Index specific file
kano-backlog embedding build /path/to/file.md --product {product}

# Index raw text
kano-backlog embedding build --text "content" --source-id "doc-1" --product {product}

# Query the index
kano-backlog embedding query "search terms" --product {product}

# Check index status
kano-backlog embedding status --product {product}
```

## Performance Considerations

### Chunking Strategy

- **Smaller chunks** (256-512 tokens): Better for precise matching, more chunks to process
- **Larger chunks** (1024+ tokens): Better for context preservation, fewer API calls

### Token Budgets

- Set `max_tokens` based on your embedding model's context limit
- Use `overlap_tokens` to preserve context across chunk boundaries
- Monitor token usage to optimize costs with paid embedding providers

### Vector Storage

- SQLite backend scales to millions of vectors for local-first use
- Use appropriate `metric` for your embedding model (cosine for most text embeddings)
- Consider storage location (`path`) for performance and backup requirements

## Troubleshooting

### Common Issues

1. **Configuration validation errors**: Check parameter types and ranges
2. **Model compatibility**: Ensure tokenizer and embedding models are compatible
3. **API authentication**: Verify API keys and endpoints for external providers
4. **Storage permissions**: Ensure write access to the configured vector path
5. **Token limits**: Verify chunk sizes don't exceed model context limits

### Debug Commands

```bash
# Validate configuration
kano-backlog config validate --product {product}

# Test embedding pipeline
kano-backlog embedding build --text "test" --source-id "test" --product {product}

# Check vector backend status
kano-backlog embedding status --product {product}
```
