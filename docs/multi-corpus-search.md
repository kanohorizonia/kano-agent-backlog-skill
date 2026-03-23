# Multi-Corpus Hybrid Search

The kano-agent-backlog-skill supports hybrid search (FTS5 + vector rerank) over two distinct corpuses:

1. **Backlog corpus**: Items + ADRs + Topics (semantic domain: planning, decisions, context)
2. **Repo corpus**: Docs + Code (semantic domain: implementation, reference, errors)

Each corpus has its own rebuildable SQLite DB and separate embedding space to prevent cross-corpus ranking pollution.

## Corpus Definitions

### Backlog Corpus
- **Sources**: `products/*/items/**`, `products/*/decisions/**`, `topics/**`
- **DB Location**: `products/<product>/.cache/chunks.sqlite3`
- **Vector Collection**: `backlog_chunks`
- **Embedding Space ID**: `corpus:backlog|emb:...|tok:...|chunk:...|metric:...`

### Repo Corpus
- **Sources**: Workspace files (*.md, *.py, *.toml, *.json, *.txt, *.yaml, *.yml)
- **DB Location**: `<workspace>/.cache/repo_chunks.sqlite3`
- **Vector Collection**: `repo_chunks`
- **Embedding Space ID**: `corpus:repo|emb:...|tok:...|chunk:...|metric:...`
- **Excludes**: `.git`, `.cache`, `*.sqlite3`, `.env`, `node_modules`, `__pycache__`, etc.

## Build Commands

### Backlog Corpus

```bash
# Build FTS index (items + ADRs + topics)
kob chunks build --product <product> --force

# Build vector index
kob embedding build --product <product> --force

# Query FTS only
kob chunks query "embedding search" --product <product> --k 10

# Query hybrid (FTS + vector rerank)
kob search hybrid "semantic search for backlog items" --corpus backlog --product <product> --k 10
```

### Repo Corpus

```bash
# Build FTS index (docs + code)
kob chunks build-repo --force

# Build vector index
kob chunks build-repo-vectors --force

# Query FTS only
kob chunks query-repo "error message" --k 10

# Query hybrid (FTS + vector rerank)
kob search hybrid "where is the embedding pipeline implemented" --corpus repo --k 10 --fts-k 200
```

## Cache Freshness Policy

Both corpuses use **mtime-based heuristic** for freshness detection:
- Build commands check file modification times vs last index time
- Incremental builds skip unchanged files
- Use `--force` to rebuild when results look stale

**Why mtime instead of content hash?**
- Content hashing is too slow for large repos
- mtime heuristic + `--force` escape hatch is good enough
- Decision recorded in KABSD-TSK-0297

**When to force rebuild:**
- After major refactoring or file moves
- When search results seem outdated
- After changing chunking/embedding config
- Periodically (e.g., weekly) for corpus churn

## Suggested Chat Prompts

### For Agents

**Build indexes:**
```
"Please build the backlog and repo search indexes with force rebuild."
```

**Search backlog:**
```
"Search the backlog for items related to embedding search using hybrid search."
```

**Search repo:**
```
"Find where the error message 'chunks DB not found' is defined in the codebase."
```

**Cross-corpus search:**
```
"Search both the backlog and repo for information about vector indexing."
```

### For Humans

**Check index status:**
```bash
# Check backlog corpus
ls -lh _kano/backlog/products/<product>/.cache/chunks.sqlite3
ls -lh _kano/backlog/products/<product>/.cache/vectors/

# Check repo corpus
ls -lh .cache/repo_chunks.sqlite3
ls -lh .cache/vectors/
```

**Rebuild workflow:**
```bash
# Full rebuild (backlog + repo)
kob chunks build --product <product> --force
kob embedding build --product <product> --force
kob chunks build-repo --force
kob chunks build-repo-vectors --force
```

## Configuration

Both corpuses share the same pipeline configuration from `_kano/backlog/products/<product>/_config/config.toml`:

```toml
[chunking]
version = "v1"
target_tokens = 400
max_tokens = 512
overlap_tokens = 50
tokenizer_adapter = "tiktoken"

[tokenizer]
adapter = "tiktoken"
model = "cl100k_base"
max_tokens = 8191

[embedding]
provider = "sentence-transformers"
model = "all-MiniLM-L6-v2"
dimension = 384

[vector]
backend = "sqlite"
path = ".cache/vectors"
collection = "backlog_chunks"  # or "repo_chunks"
metric = "cosine"
```

## Implementation References

- **Feature**: KABSD-FTR-0058 (Multi-corpus hybrid search)
- **Backlog corpus chunks**: KABSD-TSK-0298
- **Repo corpus chunks**: KABSD-TSK-0299
- **Repo corpus vectors**: KABSD-TSK-0300
- **Documentation**: KABSD-TSK-0301
- **Topic**: `_kano/backlog/topics/multi-corpus-search/`

## Troubleshooting

**"Chunks DB not found"**
- Run `kob chunks build --product <product>` (backlog)
- Run `kob chunks build-repo` (repo)

**"Vector backend not found"**
- Run `kob embedding build --product <product>` (backlog)
- Run `kob chunks build-repo-vectors` (repo)

**Stale results**
- Use `--force` flag to rebuild indexes
- Check file mtimes vs DB mtime

**Slow queries**
- Reduce `--fts-candidates` for hybrid search (default: 200)
- Use FTS-only search for keyword queries
- Check vector backend performance

**Accidental secret indexing**
- Review exclude patterns in repo corpus
- Check `.cache/repo_chunks.sqlite3` for sensitive data
- Add patterns to `DEFAULT_EXCLUDE_PATTERNS` if needed
