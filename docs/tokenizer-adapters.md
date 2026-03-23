# Tokenizer Adapters Documentation

**Accurate token counting for reliable chunking, cost estimation, and token budget management**

The tokenizer adapter system provides pluggable, accurate token counting for different model providers (OpenAI, HuggingFace, local models) to support reliable chunking operations in the kano-agent-backlog-skill embedding pipeline.

## Table of Contents

- [Quick Start](#quick-start)
- [User Guide](#user-guide)
- [Configuration Reference](#configuration-reference)
- [Troubleshooting Guide](#troubleshooting-guide)
- [Performance Tuning](#performance-tuning)
- [CLI Reference](#cli-reference)
- [Advanced Usage](#advanced-usage)

## Quick Start

### 1. Check System Status

```bash
# Check overall tokenizer system health
kob tokenizer status

# Check which adapters are available
kob tokenizer adapter-status

# Check dependencies
kob tokenizer dependencies
```

### 2. Test Tokenization

```bash
# Test with default settings
kob tokenizer test --text "This is a sample text for tokenization testing."

# Test specific adapter
kob tokenizer test --adapter tiktoken --model gpt-4

# Compare adapters
kob tokenizer benchmark --text "Sample text to compare across different tokenizers"
```

### 3. Create Configuration

```bash
# Create example configuration file
kob tokenizer create-example --output tokenizer_config.toml

# Validate configuration
kob tokenizer validate --config tokenizer_config.toml
```

## User Guide

### Understanding Tokenizer Adapters

Tokenizer adapters provide accurate token counting for different model providers. Each adapter has different characteristics:

| Adapter | Accuracy | Speed | Dependencies | Best For |
|---------|----------|-------|--------------|----------|
| **Heuristic** | ~85-90% | Very Fast | None | Development, fallback |
| **TikToken** | 100% | Fast | `tiktoken` | OpenAI models |
| **HuggingFace** | 100% | Medium | `transformers` | HF models |

### Choosing the Right Adapter

#### For OpenAI Models (GPT, text-embedding-*)
```bash
# Recommended: TikToken for exact tokenization
kob tokenizer recommend gpt-4
kob tokenizer recommend text-embedding-3-small
```

**Use TikToken when:**
- Working with OpenAI models (GPT-3.5, GPT-4, text-embedding-*)
- Need exact token counts for cost estimation
- Have tiktoken dependency available

#### For HuggingFace Models (BERT, sentence-transformers, etc.)
```bash
# Recommended: HuggingFace for exact tokenization
kob tokenizer recommend sentence-transformers/all-MiniLM-L6-v2
kob tokenizer recommend bert-base-uncased
```

**Use HuggingFace when:**
- Working with transformer models from HuggingFace
- Need exact tokenization for embedding models
- Have transformers dependency available

#### For Development and Fallback
```bash
# Heuristic adapter works with any model
kob tokenizer test --adapter heuristic --model any-model-name
```

**Use Heuristic when:**
- Dependencies not available
- Need fast approximation for development
- Working with unknown/custom models

### Adapter Selection Strategy

The system uses an intelligent fallback chain:

1. **Primary Choice**: Your configured adapter
2. **Fallback Chain**: tiktoken → huggingface → heuristic
3. **Graceful Degradation**: Clear error messages and recovery suggestions

```bash
# Check what adapter will be used
kob tokenizer adapter-status --model your-model-name

# Test fallback behavior
kob tokenizer test --adapter nonexistent-adapter
```

### Model Support

#### OpenAI Models
```bash
# List supported OpenAI models
kob tokenizer list-models --adapter tiktoken
```

Supported models include:
- GPT models: `gpt-4`, `gpt-4-turbo`, `gpt-3.5-turbo`, etc.
- Embedding models: `text-embedding-3-small`, `text-embedding-3-large`, `text-embedding-ada-002`
- Legacy models: `text-davinci-003`, `code-davinci-002`

#### HuggingFace Models
```bash
# List supported HuggingFace models
kob tokenizer list-models --adapter huggingface
```

Supported model families:
- **sentence-transformers**: `sentence-transformers/all-MiniLM-L6-v2`, `sentence-transformers/all-mpnet-base-v2`
- **BERT family**: `bert-base-uncased`, `distilbert-base-uncased`
- **RoBERTa family**: `roberta-base`, `distilroberta-base`
- **Other transformers**: `t5-base`, `facebook/bart-base`

### Integration with Embedding Pipeline

The tokenizer adapters integrate seamlessly with the embedding pipeline:

```bash
# Use specific tokenizer in embedding commands
kob embedding build --tokenizer-adapter tiktoken --tokenizer-model gpt-4

# Configure via environment variables
export KANO_TOKENIZER_ADAPTER=huggingface
export KANO_TOKENIZER_MODEL=sentence-transformers/all-MiniLM-L6-v2
kob embedding build
```

## Configuration Reference

### Configuration File Format

Tokenizer configuration uses TOML format with environment variable overrides:

```toml
# tokenizer_config.toml
[tokenizer]
# Primary adapter ("auto" for fallback chain, or specific: "heuristic", "tiktoken", "huggingface")
adapter = "auto"

# Model name (affects token limits and encoding selection)
model = "text-embedding-3-small"

# Optional: Override max tokens (uses model defaults if not specified)
max_tokens = 8192

# Fallback chain when primary adapter fails
fallback_chain = ["tiktoken", "huggingface", "heuristic"]

# Heuristic adapter configuration
[tokenizer.heuristic]
chars_per_token = 4.0  # Characters per token estimation

# TikToken adapter configuration
[tokenizer.tiktoken]
# encoding = "cl100k_base"  # Optional: specific encoding

# HuggingFace adapter configuration
[tokenizer.huggingface]
use_fast = true           # Use fast tokenizer when available
trust_remote_code = false # Security: don't execute remote code
```

### Configuration Options

#### Main Settings

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `adapter` | string | `"auto"` | Primary adapter name or "auto" for fallback |
| `model` | string | `"text-embedding-3-small"` | Model name for tokenization |
| `max_tokens` | integer | `null` | Override model's max token limit |
| `fallback_chain` | array | `["tiktoken", "huggingface", "heuristic"]` | Adapter fallback order |

#### Heuristic Adapter Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `chars_per_token` | float | `4.0` | Average characters per token |

**Language-Aware Estimation**: The heuristic adapter automatically adjusts for different text types:
- **CJK Text**: ~1.2 chars/token (Chinese, Japanese, Korean)
- **Mixed Text**: Blended ratio based on CJK content
- **ASCII/Latin**: Uses configured `chars_per_token` ratio

#### TikToken Adapter Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `encoding` | string | `null` | Specific encoding name (auto-detected if not set) |

**Supported Encodings**:
- `cl100k_base`: GPT-4, GPT-3.5-turbo, text-embedding-3-*
- `p50k_base`: text-davinci-003, code-davinci-002

#### HuggingFace Adapter Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `use_fast` | boolean | `true` | Use fast tokenizer implementation |
| `trust_remote_code` | boolean | `false` | Allow remote code execution (security risk) |

### Environment Variable Overrides

Override any configuration setting using environment variables:

```bash
# Main settings
export KANO_TOKENIZER_ADAPTER=tiktoken
export KANO_TOKENIZER_MODEL=gpt-4
export KANO_TOKENIZER_MAX_TOKENS=8192

# Heuristic settings
export KANO_TOKENIZER_HEURISTIC_CHARS_PER_TOKEN=3.5

# TikToken settings
export KANO_TOKENIZER_TIKTOKEN_ENCODING=p50k_base

# HuggingFace settings
export KANO_TOKENIZER_HUGGINGFACE_USE_FAST=false
export KANO_TOKENIZER_HUGGINGFACE_TRUST_REMOTE_CODE=false
```

View all available environment variables:
```bash
kob tokenizer env-vars
```

### Configuration Examples

#### Development Setup (Fast, No Dependencies)
```toml
[tokenizer]
adapter = "heuristic"
model = "development-model"

[tokenizer.heuristic]
chars_per_token = 4.0
```

#### Production OpenAI Setup
```toml
[tokenizer]
adapter = "tiktoken"
model = "text-embedding-3-small"
fallback_chain = ["tiktoken", "heuristic"]

[tokenizer.tiktoken]
encoding = "cl100k_base"
```

#### Production HuggingFace Setup
```toml
[tokenizer]
adapter = "huggingface"
model = "sentence-transformers/all-MiniLM-L6-v2"
fallback_chain = ["huggingface", "heuristic"]

[tokenizer.huggingface]
use_fast = true
trust_remote_code = false
```

#### Multi-Environment Setup
```toml
[tokenizer]
adapter = "auto"  # Use fallback chain
model = "text-embedding-3-small"
fallback_chain = ["tiktoken", "huggingface", "heuristic"]

# All adapter configurations available
[tokenizer.heuristic]
chars_per_token = 4.0

[tokenizer.tiktoken]
# encoding auto-detected

[tokenizer.huggingface]
use_fast = true
trust_remote_code = false
```

### Configuration Management

```bash
# Create example configuration
kob tokenizer create-example --output my_config.toml

# Validate configuration
kob tokenizer validate --config my_config.toml

# Show current configuration (with environment overrides)
kob tokenizer config --config my_config.toml --format json

# Migrate old configuration format
kob tokenizer migrate old_config.json --output new_config.toml
```

## Troubleshooting Guide

### Common Issues and Solutions

#### 1. "No tokenizer adapter available" Error

**Symptoms:**
```
FallbackChainExhaustedError: No tokenizer available. Errors: [tiktoken: No module named 'tiktoken', huggingface: No module named 'transformers', heuristic: <some error>]
```

**Solutions:**

**Option A: Install Dependencies**
```bash
# For OpenAI models
pip install tiktoken

# For HuggingFace models
pip install transformers

# Check installation
kob tokenizer dependencies
```

**Option B: Use Heuristic Adapter**
```bash
# Force heuristic adapter
export KANO_TOKENIZER_ADAPTER=heuristic
kob tokenizer test

# Or in configuration
[tokenizer]
adapter = "heuristic"
```

**Option C: Check Installation Guide**
```bash
kob tokenizer install-guide
```

#### 2. TikToken Import Errors

**Symptoms:**
```
ImportError: tiktoken package required for TiktokenAdapter. Install with: pip install tiktoken
```

**Solutions:**

**Install TikToken:**
```bash
pip install tiktoken

# Verify installation
python -c "import tiktoken; print('TikToken available')"

# Test adapter
kob tokenizer health-check tiktoken
```

**Check Python Environment:**
```bash
# Ensure you're in the right environment
which python
pip list | grep tiktoken

# If using conda
conda install tiktoken -c conda-forge
```

**Fallback to Heuristic:**
```bash
export KANO_TOKENIZER_ADAPTER=heuristic
```

#### 3. HuggingFace Transformers Issues

**Symptoms:**
```
ImportError: transformers package required for HuggingFaceAdapter
```
or
```
OSError: Can't load tokenizer for 'model-name'. Make sure that 'model-name' is a correct model identifier
```

**Solutions:**

**Install Transformers:**
```bash
pip install transformers

# For sentence-transformers models
pip install sentence-transformers

# Verify installation
python -c "import transformers; print('Transformers available')"
```

**Check Model Name:**
```bash
# List supported models
kob tokenizer list-models --adapter huggingface

# Test with known model
kob tokenizer test --adapter huggingface --model bert-base-uncased
```

**Network/Cache Issues:**
```bash
# Clear HuggingFace cache
rm -rf ~/.cache/huggingface/

# Use offline mode if needed
export TRANSFORMERS_OFFLINE=1
```

#### 4. Configuration Validation Errors

**Symptoms:**
```
ConfigError: heuristic.chars_per_token must be a positive number
```

**Solutions:**

**Fix Configuration Values:**
```toml
[tokenizer.heuristic]
chars_per_token = 4.0  # Must be positive number

[tokenizer.huggingface]
use_fast = true        # Must be boolean
trust_remote_code = false  # Must be boolean
```

**Validate Configuration:**
```bash
kob tokenizer validate --config your_config.toml
```

**Check Environment Variables:**
```bash
# Ensure environment variables are valid
export KANO_TOKENIZER_HEURISTIC_CHARS_PER_TOKEN=4.0  # Not "4.0.0"
export KANO_TOKENIZER_HUGGINGFACE_USE_FAST=true      # Not "True"
```

#### 5. Token Count Inconsistencies

**Symptoms:**
- Different token counts between adapters
- Unexpected chunking behavior
- Budget overruns

**Solutions:**

**Compare Adapters:**
```bash
# Compare token counts across adapters
kob tokenizer benchmark --text "Your sample text here"

# Benchmark adapters
kob tokenizer benchmark --text "Your sample text" --iterations 5
```

**Check Model Configuration:**
```bash
# Verify model settings
kob tokenizer adapter-status --model your-model-name

# Check model max tokens
kob tokenizer list-models | grep your-model
```

**Use Exact Adapters:**
```bash
# For OpenAI models, use tiktoken
export KANO_TOKENIZER_ADAPTER=tiktoken

# For HuggingFace models, use huggingface
export KANO_TOKENIZER_ADAPTER=huggingface
```

#### 6. Performance Issues

**Symptoms:**
- Slow tokenization
- High memory usage
- Timeouts

**Solutions:**

**Use Faster Adapters:**
```bash
# Heuristic is fastest
export KANO_TOKENIZER_ADAPTER=heuristic

# Enable fast tokenizers for HuggingFace
export KANO_TOKENIZER_HUGGINGFACE_USE_FAST=true
```

**Benchmark Performance:**
```bash
kob tokenizer benchmark --adapters heuristic,tiktoken --iterations 10
```

**Check System Resources:**
```bash
# Monitor during tokenization
kob tokenizer test --text "$(cat large_file.txt)" &
top -p $!
```

### Diagnostic Commands

#### System Health Check
```bash
# Comprehensive status
kob tokenizer status --verbose

# Check specific adapter
kob tokenizer health-check tiktoken

# Dependency report
kob tokenizer dependencies --verbose
```

#### Configuration Debugging
```bash
# Show effective configuration
kob tokenizer config --format json

# Validate configuration
kob tokenizer validate --config your_config.toml

# Test configuration
kob tokenizer test --config your_config.toml
```

#### Performance Analysis
```bash
# Benchmark all adapters
kob tokenizer benchmark

# Compare specific adapters
kob tokenizer benchmark --text "test text" --adapters tiktoken,heuristic

# Profile memory usage
kob tokenizer benchmark --text "$(cat large_file.txt)" --format json
```

### Getting Help

#### Built-in Help
```bash
# Command help
kob tokenizer --help
kob tokenizer test --help

# Environment variables
kob tokenizer env-vars

# Installation guide
kob tokenizer install-guide
```

#### Diagnostic Information
```bash
# System information for bug reports
kob tokenizer status --format json > tokenizer_status.json

# Dependency information
kob tokenizer dependencies --verbose > dependencies.txt

# Configuration dump
kob tokenizer config --format json > current_config.json
```

## Performance Tuning

### Performance Characteristics

Understanding the performance trade-offs helps choose the right adapter:

| Adapter | Speed | Memory | Accuracy | Startup Time |
|---------|-------|--------|----------|--------------|
| Heuristic | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| TikToken | ⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ |
| HuggingFace | ⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐ |

### Benchmarking Your Setup

#### Basic Performance Test
```bash
# Test with your typical text
kob tokenizer benchmark --text "Your typical document content here"

# Test with different text sizes
kob tokenizer benchmark --text "$(head -c 1000 large_file.txt)"   # 1KB
kob tokenizer benchmark --text "$(head -c 10000 large_file.txt)"  # 10KB
kob tokenizer benchmark --text "$(head -c 100000 large_file.txt)" # 100KB
```

#### Comprehensive Benchmark
```bash
# Benchmark all available adapters
kob tokenizer benchmark --iterations 20 --format json > benchmark_results.json

# Compare specific adapters
kob tokenizer benchmark --adapters tiktoken,heuristic --iterations 50
```

#### Memory Profiling
```bash
# Monitor memory usage during tokenization
kob tokenizer benchmark --text "$(cat very_large_file.txt)" &
PID=$!
while kill -0 $PID 2>/dev/null; do
    ps -o pid,vsz,rss,comm -p $PID
    sleep 1
done
```

### Optimization Strategies

#### 1. Adapter Selection for Performance

**For Maximum Speed:**
```toml
[tokenizer]
adapter = "heuristic"
fallback_chain = ["heuristic"]

[tokenizer.heuristic]
chars_per_token = 4.0  # Tune based on your content
```

**For Balanced Performance:**
```toml
[tokenizer]
adapter = "tiktoken"
fallback_chain = ["tiktoken", "heuristic"]
```

**For Maximum Accuracy:**
```toml
[tokenizer]
adapter = "auto"  # Use exact adapters when available
fallback_chain = ["tiktoken", "huggingface", "heuristic"]
```

#### 2. HuggingFace Optimization

**Enable Fast Tokenizers:**
```toml
[tokenizer.huggingface]
use_fast = true  # Significant speed improvement
trust_remote_code = false
```

**Model Selection:**
```bash
# Prefer smaller, faster models when possible
# Instead of: sentence-transformers/all-mpnet-base-v2
# Use: sentence-transformers/all-MiniLM-L6-v2
```

#### 3. Heuristic Tuning

**Optimize chars_per_token for Your Content:**

```bash
# Test different ratios
for ratio in 3.0 3.5 4.0 4.5 5.0; do
    export KANO_TOKENIZER_HEURISTIC_CHARS_PER_TOKEN=$ratio
    echo "Ratio $ratio:"
    kob tokenizer benchmark --text "Your typical content" --adapters heuristic,tiktoken
done
```

**Language-Specific Tuning:**
```toml
[tokenizer.heuristic]
# For English technical content
chars_per_token = 4.2

# For mixed English/code content
chars_per_token = 3.8

# For documentation with lots of punctuation
chars_per_token = 4.5
```

#### 4. Environment Optimization

**Python Environment:**
```bash
# Use Python 3.11+ for better performance
python --version

# Ensure packages are up to date
pip install --upgrade tiktoken transformers
```

**System Resources:**
```bash
# Monitor resource usage
kob tokenizer benchmark --verbose

# For large documents, ensure adequate memory
free -h
```

### Performance Monitoring

#### Built-in Metrics
```bash
# Get performance statistics
kob tokenizer benchmark --format json | jq '.results[] | {adapter, avg_time_ms, avg_tokens}'

# Monitor consistency
kob tokenizer benchmark --iterations 100 | grep "Consistent"
```

#### Custom Monitoring
```python
# Example: Monitor tokenization performance in your application
import time
from kano_backlog_core.tokenizer import get_default_registry

registry = get_default_registry()
adapter = registry.resolve("tiktoken", model_name="gpt-4")

start_time = time.perf_counter()
result = adapter.count_tokens(your_text)
end_time = time.perf_counter()

print(f"Tokenization took {(end_time - start_time) * 1000:.2f}ms")
print(f"Token count: {result.count}")
print(f"Tokens per second: {result.count / (end_time - start_time):.0f}")
```

### Production Recommendations

#### Configuration for Production
```toml
[tokenizer]
# Use exact adapters with heuristic fallback
adapter = "auto"
fallback_chain = ["tiktoken", "huggingface", "heuristic"]

# Set appropriate model
model = "text-embedding-3-small"  # Or your production model

[tokenizer.heuristic]
# Tune based on your content analysis
chars_per_token = 4.0

[tokenizer.tiktoken]
# Let encoding be auto-detected

[tokenizer.huggingface]
use_fast = true
trust_remote_code = false  # Security best practice
```

#### Monitoring in Production
```bash
# Regular health checks
kob tokenizer status --format json

# Performance monitoring
kob tokenizer benchmark --adapters auto --iterations 10

# Dependency monitoring
kob tokenizer dependencies --format json
```

#### Scaling Considerations

**For High-Volume Processing:**
1. Use TikToken for OpenAI models (fastest exact adapter)
2. Enable fast tokenizers for HuggingFace models
3. Consider heuristic adapter for non-critical paths
4. Monitor memory usage with large documents

**For Multi-Model Environments:**
1. Use "auto" adapter with comprehensive fallback chain
2. Monitor adapter usage statistics
3. Optimize fallback chain based on your model distribution

**For Resource-Constrained Environments:**
1. Use heuristic adapter primarily
2. Tune chars_per_token based on accuracy requirements
3. Minimize dependency footprint

## CLI Reference

### Core Commands

#### `kob tokenizer status`
Show comprehensive system status including configuration, adapters, dependencies, and health.

```bash
kob tokenizer status [OPTIONS]

Options:
  --config PATH     Configuration file path
  --verbose         Show detailed information
  --format FORMAT   Output format (markdown, json)
```

#### `kob tokenizer test`
Test tokenizer adapters with sample text.

```bash
kob tokenizer test [OPTIONS]

Options:
  --config PATH     Configuration file path
  --text TEXT       Text to tokenize (default: sample text)
  --adapter NAME    Specific adapter to test
  --model NAME      Model name to use
```

#### `kob tokenizer benchmark --text`
Compare tokenization results across different adapters.

```bash
kob tokenizer benchmark --text TEXT [OPTIONS]

Arguments:
  TEXT              Text to tokenize and compare

Options:
  --adapters LIST   Comma-separated adapter names
  --model NAME      Model name to use
  --show-tokens     Show token breakdown (when supported)
```

### Configuration Commands

#### `kob tokenizer config`
Show current configuration with environment overrides.

```bash
kob tokenizer config [OPTIONS]

Options:
  --config PATH     Configuration file path
  --format FORMAT   Output format (json, toml, yaml)
```

#### `kob tokenizer validate`
Validate tokenizer configuration.

```bash
kob tokenizer validate [OPTIONS]

Options:
  --config PATH     Configuration file path
```

#### `kob tokenizer create-example`
Create example configuration file.

```bash
kob tokenizer create-example [OPTIONS]

Options:
  --output PATH     Output file path
  --force           Overwrite existing file
```

#### `kob tokenizer migrate`
Migrate configuration from old format to TOML.

```bash
kob tokenizer migrate INPUT_PATH [OPTIONS]

Arguments:
  INPUT_PATH        Input configuration file

Options:
  --output PATH     Output TOML file path
  --force           Overwrite existing output
```

### Diagnostic Commands

#### `kob tokenizer adapter-status`
Run comprehensive tokenizer diagnostics.

```bash
kob tokenizer adapter-status [OPTIONS]

Options:
  --config PATH     Configuration file path
  --model NAME      Specific model to diagnose
  --verbose         Show detailed diagnostic information
```

#### `kob tokenizer health`
Check health of specific tokenizer adapter.

```bash
kob tokenizer health-check ADAPTER [OPTIONS]

Arguments:
  ADAPTER           Adapter name (heuristic, tiktoken, huggingface)

Options:
  --model NAME      Model name to test with
```

#### `kob tokenizer dependencies`
Check status of tokenizer dependencies.

```bash
kob tokenizer dependencies [OPTIONS]

Options:
  --verbose         Show detailed dependency information
  --refresh         Force refresh of dependency cache
```

#### `kob tokenizer adapter-status`
Show status of tokenizer adapters.

```bash
kob tokenizer adapter-status [OPTIONS]

Options:
  --adapter NAME    Show status for specific adapter only
```

### Performance Commands

#### `kob tokenizer benchmark`
Benchmark tokenizer adapter performance.

```bash
kob tokenizer benchmark [OPTIONS]

Options:
  --text TEXT       Text for benchmarking
  --iterations N    Number of test iterations
  --adapters LIST   Comma-separated adapter names
  --model NAME      Model name to use
  --format FORMAT   Output format (markdown, json, csv)
```

### Utility Commands

#### `kob tokenizer env-vars`
Show available environment variables.

```bash
kob tokenizer env-vars
```

#### `kob tokenizer install-guide`
Show installation guide for missing dependencies.

```bash
kob tokenizer install-guide
```

#### `kob tokenizer list-models`
List supported models and token limits.

```bash
kob tokenizer list-models [OPTIONS]

Options:
  --adapter NAME    Show models for specific adapter
  --format FORMAT   Output format (markdown, json, csv)
```

#### `kob tokenizer recommend`
Get adapter recommendation for specific model.

```bash
kob tokenizer recommend MODEL [OPTIONS]

Arguments:
  MODEL             Model name

Options:
  --requirements    Requirements (e.g., 'accuracy=high,speed=medium')
```

## Advanced Usage

### Custom Integration

#### Using Tokenizer Adapters in Python Code

```python
from kano_backlog_core.tokenizer import get_default_registry
from kano_backlog_core.tokenizer_config import load_tokenizer_config

# Load configuration
config = load_tokenizer_config()

# Get registry and set fallback chain
registry = get_default_registry()
registry.set_fallback_chain(config.fallback_chain)

# Resolve adapter
adapter = registry.resolve(
    adapter_name=config.adapter,
    model_name=config.model,
    max_tokens=config.max_tokens,
    **config.get_adapter_options(config.adapter)
)

# Count tokens
result = adapter.count_tokens("Your text here")
print(f"Token count: {result.count}")
print(f"Method: {result.method}")
print(f"Is exact: {result.is_exact}")
```

#### Custom Adapter Implementation

```python
from kano_backlog_core.tokenizer import TokenizerAdapter, TokenCount

class CustomTokenizerAdapter(TokenizerAdapter):
    """Custom tokenizer adapter implementation."""
    
    def __init__(self, model_name: str, max_tokens: Optional[int] = None, **kwargs):
        super().__init__(model_name, max_tokens)
        # Initialize your custom tokenizer
        
    @property
    def adapter_id(self) -> str:
        return "custom"
    
    def count_tokens(self, text: str) -> TokenCount:
        # Implement your tokenization logic
        token_count = your_tokenization_function(text)
        
        return TokenCount(
            count=token_count,
            method="custom",
            tokenizer_id=f"custom:{self.model_name}",
            is_exact=True,  # or False for approximations
            model_max_tokens=self.max_tokens()
        )
    
    def max_tokens(self) -> int:
        # Return max tokens for your model
        return self._max_tokens or 8192

# Register custom adapter
registry = get_default_registry()
registry.register("custom", CustomTokenizerAdapter, custom_param="value")
```

### Integration with Chunking Pipeline

The tokenizer adapters integrate with the chunking system for accurate token-aware document processing:

```python
from kano_backlog_core.chunking import ChunkingOptions, chunk_text
from kano_backlog_core.tokenizer_config import load_tokenizer_config

# Load tokenizer configuration
tokenizer_config = load_tokenizer_config()

# Configure chunking with tokenizer settings
chunking_options = ChunkingOptions(
    target_tokens=512,
    max_tokens=1024,
    overlap_tokens=50,
    safety_margin=0.1,
    tokenizer_adapter=tokenizer_config.adapter,
    tokenizer_model=tokenizer_config.model
)

# Chunk document with accurate token counting
chunks = chunk_text(
    text="Your document content here",
    source_id="document_id",
    options=chunking_options
)

for chunk in chunks:
    print(f"Chunk {chunk.chunk_index}: {chunk.token_count.count} tokens")
    print(f"Method: {chunk.token_count.method}")
    print(f"Was trimmed: {chunk.was_trimmed}")
```

### Error Handling and Recovery

```python
from kano_backlog_core.tokenizer import get_default_registry
from kano_backlog_core.tokenizer_errors import (
    TokenizerError,
    AdapterNotAvailableError,
    FallbackChainExhaustedError
)

registry = get_default_registry()

try:
    adapter = registry.resolve("tiktoken", model_name="gpt-4")
    result = adapter.count_tokens("Your text")
except AdapterNotAvailableError as e:
    print(f"Adapter not available: {e}")
    # Get recovery suggestions
    suggestions = registry.suggest_recovery_strategy(e, "tiktoken", "gpt-4")
    print(f"Suggestion: {suggestions['user_message']}")
except FallbackChainExhaustedError as e:
    print(f"No adapters available: {e}")
    # Check dependencies
    from kano_backlog_core.tokenizer_dependencies import get_dependency_manager
    manager = get_dependency_manager()
    report = manager.check_all_dependencies()
    print(f"Missing dependencies: {report.get_missing_dependencies()}")
except TokenizerError as e:
    print(f"Tokenization error: {e}")
```

### Monitoring and Telemetry

```python
from kano_backlog_core.tokenizer import get_default_registry
import time

registry = get_default_registry()

# Monitor adapter usage
adapter = registry.resolve("auto", model_name="gpt-4")
start_time = time.perf_counter()
result = adapter.count_tokens("Your text")
end_time = time.perf_counter()

# Log telemetry
telemetry = {
    "adapter_used": adapter.adapter_id,
    "model_name": adapter.model_name,
    "token_count": result.count,
    "is_exact": result.is_exact,
    "processing_time_ms": (end_time - start_time) * 1000,
    "text_length": len("Your text")
}

print(f"Tokenization telemetry: {telemetry}")

# Get recovery statistics
stats = registry.get_recovery_statistics()
if stats["total_recovery_attempts"] > 0:
    print(f"Recovery attempts: {stats['total_recovery_attempts']}")
    print(f"Most problematic adapter: {stats['most_problematic_adapter']}")
```

---

## Support and Contributing

### Getting Help

1. **Check Status**: `kob tokenizer status --verbose`
2. **Run Diagnostics**: `kob tokenizer adapter-status`
3. **Check Dependencies**: `kob tokenizer dependencies`
4. **Review Configuration**: `kob tokenizer config --format json`

### Reporting Issues

When reporting issues, please include:

```bash
# System information
kob tokenizer status --format json > system_status.json

# Dependency information  
kob tokenizer dependencies --verbose > dependencies.txt

# Configuration
kob tokenizer config --format json > config.json

# Test results
kob tokenizer test --verbose > test_results.txt 2>&1
```

### Contributing

The tokenizer adapter system is designed to be extensible. Contributions are welcome for:

- New adapter implementations
- Performance optimizations
- Additional model support
- Documentation improvements
- Test coverage enhancements

See the main project README for contribution guidelines.
