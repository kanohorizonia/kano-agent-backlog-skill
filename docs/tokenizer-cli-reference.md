# Tokenizer Adapters CLI Reference

**Complete command-line interface reference for tokenizer adapters**

This document provides comprehensive reference for all tokenizer CLI commands, options, and usage patterns.

## Table of Contents

- [Command Overview](#command-overview)
- [Global Options](#global-options)
- [Core Commands](#core-commands)
- [Configuration Commands](#configuration-commands)
- [Diagnostic Commands](#diagnostic-commands)
- [Performance Commands](#performance-commands)
- [Utility Commands](#utility-commands)
- [Usage Patterns](#usage-patterns)
- [Output Formats](#output-formats)

## Command Overview

All tokenizer commands are accessed through the `kob tokenizer` subcommand:

```bash
kob tokenizer <command> [options]
```

### Command Categories

| Category | Commands | Purpose |
|----------|----------|---------|
| **Core** | `status`, `test` | Basic functionality and testing |
| **Configuration** | `config`, `validate`, `create-example`, `migrate` | Configuration management |
| **Diagnostic** | `health-check`, `dependencies`, `adapter-status`, `env-vars` | System diagnostics |
| **Performance** | `benchmark` | Performance testing and optimization |
| **Utility** | `env-vars`, `install-guide`, `list-models`, `recommend` | Helper utilities |

## Global Options

These options are available for most commands:

### `--config PATH`
Specify configuration file path.

```bash
kob tokenizer test --config /path/to/config.toml
kob tokenizer validate --config my_config.toml
```

### `--help`
Show command help.

```bash
kob tokenizer --help
kob tokenizer test --help
```

## Core Commands

### `kob tokenizer status`

Show comprehensive system status including configuration, adapters, dependencies, and health.

**Syntax:**
```bash
kob tokenizer status [OPTIONS]
```

**Options:**
- `--config PATH` - Configuration file path
- `--verbose` - Show detailed information
- `--format FORMAT` - Output format (`markdown`, `json`)

**Examples:**
```bash
# Basic status
kob tokenizer status

# Detailed status
kob tokenizer status --verbose

# JSON output for scripting
kob tokenizer status --format json

# With custom configuration
kob tokenizer status --config production_config.toml --verbose
```

**Sample Output:**
```markdown
# Tokenizer System Status

**Overall Health:** ✅ HEALTHY
**Python Version:** 3.11.0 ✅

## Configuration
- **Adapter:** auto
- **Model:** text-embedding-3-small
- **Max Tokens:** auto
- **Fallback Chain:** tiktoken → huggingface → heuristic

## Adapter Status
### ✅ HEURISTIC
- **Status:** Available
- **Dependencies:** Ready

### ✅ TIKTOKEN
- **Status:** Available
- **Dependencies:** Ready

### ❌ HUGGINGFACE
- **Status:** Not available
- **Error:** No module named 'transformers'
```

### `kob tokenizer test`

Test tokenizer adapters with sample text.

**Syntax:**
```bash
kob tokenizer test [OPTIONS]
```

**Options:**
- `--config PATH` - Configuration file path
- `--text TEXT` - Text to tokenize (default: sample text)
- `--adapter NAME` - Specific adapter to test
- `--model NAME` - Model name to use

**Examples:**
```bash
# Test with default settings
kob tokenizer test

# Test with custom text
kob tokenizer test --text "Your custom text here"

# Test specific adapter
kob tokenizer test --adapter tiktoken --model gpt-4

# Test with configuration file
kob tokenizer test --config my_config.toml

# Test with environment override
KANO_TOKENIZER_ADAPTER=heuristic kob tokenizer test
```

**Sample Output:**
```
Testing tokenizers with text: 'This is a test sentence for tokenizer adapter testing.'
Text length: 58 characters

✓ HEURISTIC Adapter:
  Token count: 14
  Method: heuristic
  Tokenizer ID: heuristic:text-embedding-3-small:chars_4.0
  Is exact: False
  Max tokens: 8192

✓ TIKTOKEN Adapter:
  Token count: 12
  Method: tiktoken
  Tokenizer ID: tiktoken:text-embedding-3-small:cl100k_base
  Is exact: True
  Max tokens: 8192

Primary adapter resolution (auto):
  Resolved to: tiktoken
  Token count: 12
  Is exact: True
```

### `kob tokenizer benchmark`

Run a tokenizer performance and adapter benchmark.

**Syntax:**
```bash
kob tokenizer benchmark [OPTIONS]
```

**Options:**
- `--text TEXT` - Optional sample text override
- `--iterations N` - Optional iteration count

**Examples:**
```bash
# Run the default benchmark
kob tokenizer benchmark

# Override sample text
kob tokenizer benchmark --text "Sample text to benchmark"

# Increase iterations
kob tokenizer benchmark --iterations 25
```

**Sample Output:**
```markdown
📊 Tokenizer Benchmark Results:
==================================================

🥇 heuristic - Grade: A
   Iterations: 10
   Sample tokens: 15
   Avg processing time: 0.02ms
```

## Configuration Commands

### `kob tokenizer config`

Show current configuration with environment overrides applied.

**Syntax:**
```bash
kob tokenizer config [OPTIONS]
```

**Options:**
- `--config PATH` - Configuration file path
- `--format FORMAT` - Output format (`json`, `toml`, `yaml`)

**Examples:**
```bash
# Show configuration in JSON
kob tokenizer config --format json

# Show configuration from specific file
kob tokenizer config --config my_config.toml

# Show configuration in TOML format
kob tokenizer config --format toml
```

**Sample Output (JSON):**
```json
{
  "adapter": "auto",
  "model": "text-embedding-3-small",
  "max_tokens": null,
  "fallback_chain": ["tiktoken", "huggingface", "heuristic"],
  "options": {},
  "heuristic": {
    "chars_per_token": 4.0
  },
  "tiktoken": {},
  "huggingface": {
    "use_fast": true,
    "trust_remote_code": false
  }
}
```

### `kob tokenizer validate`

Validate tokenizer configuration.

**Syntax:**
```bash
kob tokenizer validate [OPTIONS]
```

**Options:**
- `--config PATH` - Configuration file path

**Examples:**
```bash
# Validate default configuration
kob tokenizer validate

# Validate specific configuration file
kob tokenizer validate --config my_config.toml
```

**Sample Output:**
```
✓ Configuration is valid
  Adapter: auto
  Model: text-embedding-3-small
  Max tokens: auto
  Fallback chain: tiktoken → huggingface → heuristic
```

### `kob tokenizer create-example`

Create an example tokenizer configuration file.

**Syntax:**
```bash
kob tokenizer create-example [OPTIONS]
```

**Options:**
- `--output PATH` - Output file path (default: `tokenizer_config.toml`)
- `--force` - Overwrite existing file

**Examples:**
```bash
# Create example configuration
kob tokenizer create-example

# Create with custom name
kob tokenizer create-example --output my_config.toml

# Overwrite existing file
kob tokenizer create-example --output existing_config.toml --force
```

**Sample Output:**
```
✓ Created example tokenizer configuration: tokenizer_config.toml

Edit the file to customize your tokenizer settings.
Use 'kob tokenizer validate --config <path>' to validate your changes.
```

### `kob tokenizer migrate`

Migrate configuration from old format to new TOML format.

**Syntax:**
```bash
kob tokenizer migrate INPUT_PATH [OPTIONS]
```

**Arguments:**
- `INPUT_PATH` - Input configuration file (JSON or TOML)

**Options:**
- `--output PATH` - Output TOML file path (default: input path with `.toml` extension)
- `--force` - Overwrite existing output file

**Examples:**
```bash
# Migrate JSON to TOML
kob tokenizer migrate old_config.json

# Migrate with custom output path
kob tokenizer migrate old_config.json --output new_config.toml

# Force overwrite
kob tokenizer migrate old_config.json --output existing.toml --force
```

**Sample Output:**
```
✓ Migrated configuration from old_config.json to old_config.toml

Validate the migrated configuration with:
  kob tokenizer validate --config old_config.toml
```

## Diagnostic Commands

### `kob tokenizer adapter-status`

Run comprehensive tokenizer diagnostics.

**Syntax:**
```bash
kob tokenizer adapter-status [OPTIONS]
```

**Options:**
- `--config PATH` - Configuration file path
- `--model NAME` - Specific model to diagnose
- `--verbose` - Show detailed diagnostic information

**Examples:**
```bash
# Basic diagnostics
kob tokenizer adapter-status

# Diagnose specific model
kob tokenizer adapter-status --model gpt-4

# Verbose diagnostics
kob tokenizer adapter-status --verbose

# Diagnose with custom configuration
kob tokenizer adapter-status --config my_config.toml --verbose
```

### `kob tokenizer health`

Check health of a specific tokenizer adapter.

**Syntax:**
```bash
kob tokenizer health-check ADAPTER [OPTIONS]
```

**Arguments:**
- `ADAPTER` - Adapter name (`heuristic`, `tiktoken`, `huggingface`)

**Options:**
- `--model NAME` - Model name to test with (default: `test-model`)

**Examples:**
```bash
# Check tiktoken health
kob tokenizer health-check tiktoken

# Check with specific model
kob tokenizer health-check huggingface --model bert-base-uncased

# Check all adapters
for adapter in heuristic tiktoken huggingface; do
    echo "Checking $adapter:"
    kob tokenizer health-check $adapter
    echo
done
```

**Sample Output:**
```
✅ TIKTOKEN adapter is healthy
   Token count: 12
   Method: tiktoken
   Is exact: True
   Tokenizer ID: tiktoken:test-model:cl100k_base
   Max tokens: 8192
```

### `kob tokenizer dependencies`

Check status of tokenizer dependencies.

**Syntax:**
```bash
kob tokenizer dependencies [OPTIONS]
```

**Options:**
- `--verbose` - Show detailed dependency information
- `--refresh` - Force refresh of dependency cache

**Examples:**
```bash
# Basic dependency check
kob tokenizer dependencies

# Detailed dependency information
kob tokenizer dependencies --verbose

# Force refresh cache
kob tokenizer dependencies --refresh
```

**Sample Output:**
```
✅ Overall Health: HEALTHY
🐍 Python Version: 3.11.0 ✅

📦 Dependencies:
  ✅ tiktoken
      Version: 0.5.1
  ❌ transformers
      Error: No module named 'transformers'
      Installation:
        pip install transformers
        conda install transformers -c conda-forge

💡 Recommendations:
  • Install transformers for HuggingFace model support
  • Consider using tiktoken for OpenAI models

❌ Missing Dependencies: transformers
   Use 'kob tokenizer install-guide' for installation instructions
```

### `kob tokenizer adapter-status`

Show status of tokenizer adapters including dependency checks.

**Syntax:**
```bash
kob tokenizer adapter-status [OPTIONS]
```

**Options:**
- `--adapter NAME` - Show status for specific adapter only

**Examples:**
```bash
# Show all adapter status
kob tokenizer adapter-status

# Show specific adapter status
kob tokenizer adapter-status --adapter tiktoken
```

**Sample Output:**
```
🔧 Tokenizer Adapter Status:

  ✅ HEURISTIC
      Status: Available
      Dependencies: Ready

  ✅ TIKTOKEN
      Status: Available
      Dependencies: Ready

  ❌ HUGGINGFACE
      Status: Not available
      Error: No module named 'transformers'
      Missing deps: transformers

📊 Overall Health: DEGRADED
❌ Missing: transformers
```

## Performance Commands

### `kob tokenizer benchmark`

Benchmark tokenizer adapter performance and accuracy.

**Syntax:**
```bash
kob tokenizer benchmark [OPTIONS]
```

**Options:**
- `--text TEXT` - Text for benchmarking (default: sample text)
- `--iterations N` - Number of test iterations (default: 10)
- `--adapters LIST` - Comma-separated adapter names (default: all available)
- `--model NAME` - Model name to use (default: `text-embedding-3-small`)
- `--format FORMAT` - Output format (`markdown`, `json`, `csv`)

**Examples:**
```bash
# Basic benchmark
kob tokenizer benchmark

# Benchmark with custom text
kob tokenizer benchmark --text "$(cat large_document.txt)"

# Benchmark specific adapters
kob tokenizer benchmark --adapters tiktoken,heuristic --iterations 50

# JSON output for analysis
kob tokenizer benchmark --format json > benchmark_results.json

# CSV output for spreadsheet analysis
kob tokenizer benchmark --format csv > benchmark_results.csv
```

**Sample Output (Markdown):**
```markdown
# Tokenizer Adapter Benchmark Results

## Performance Summary
| Adapter | Avg Time (ms) | Tokens | Exact | Consistent | Status |
|---------|---------------|--------|-------|------------|--------|
| heuristic | 0.12 | 14 | ❌ | ✅ | ✅ |
| tiktoken | 2.45 | 12 | ✅ | ✅ | ✅ |

## Detailed Results
### HEURISTIC
- **Average Time:** 0.12 ms
- **Time Range:** 0.10 - 0.15 ms
- **Token Count:** 14
- **Exact Count:** No
- **Consistent:** Yes
- **Method:** heuristic
- **Tokenizer ID:** heuristic:text-embedding-3-small:chars_4.0

### TIKTOKEN
- **Average Time:** 2.45 ms
- **Time Range:** 2.20 - 2.80 ms
- **Token Count:** 12
- **Exact Count:** Yes
- **Consistent:** Yes
- **Method:** tiktoken
- **Tokenizer ID:** tiktoken:text-embedding-3-small:cl100k_base

## Performance Ranking
**By Speed (fastest first):**
1. heuristic (0.12 ms)
2. tiktoken (2.45 ms)

**By Accuracy (most accurate first):**
1. tiktoken (exact, consistent)
2. heuristic (consistent)
```

## Utility Commands

### `kob tokenizer env-vars`

Show available environment variables for tokenizer configuration.

**Syntax:**
```bash
kob tokenizer env-vars
```

**Sample Output:**
```
Tokenizer Configuration Environment Variables:

  KANO_TOKENIZER_ADAPTER
    Description: Override adapter selection (auto, heuristic, tiktoken, huggingface)
    Current value: not set

  KANO_TOKENIZER_MODEL
    Description: Override model name
    Current value: not set

  KANO_TOKENIZER_MAX_TOKENS
    Description: Override max tokens (integer)
    Current value: not set

  KANO_TOKENIZER_HEURISTIC_CHARS_PER_TOKEN
    Description: Override chars per token ratio (float)
    Current value: not set

  KANO_TOKENIZER_TIKTOKEN_ENCODING
    Description: Override TikToken encoding
    Current value: not set

  KANO_TOKENIZER_HUGGINGFACE_USE_FAST
    Description: Override use_fast setting (true/false)
    Current value: not set

  KANO_TOKENIZER_HUGGINGFACE_TRUST_REMOTE_CODE
    Description: Override trust_remote_code (true/false)
    Current value: not set

Example usage:
  export KANO_TOKENIZER_ADAPTER=heuristic
  export KANO_TOKENIZER_HEURISTIC_CHARS_PER_TOKEN=3.5
  kob tokenizer test
```

### `kob tokenizer install-guide`

Show installation guide for missing dependencies.

**Syntax:**
```bash
kob tokenizer install-guide
```

**Sample Output:**
```
# Tokenizer Dependencies Installation Guide

## Missing Dependencies
Based on your system check, the following dependencies are missing:

### transformers (for HuggingFace adapter)
**Installation Options:**
```bash
# Using pip (recommended)
pip install transformers

# Using conda
conda install transformers -c conda-forge

# With specific version
pip install "transformers>=4.21.0"
```

**Verification:**
```bash
python -c "import transformers; print('Transformers version:', transformers.__version__)"
kob tokenizer health-check huggingface
```

## Optional Dependencies

### sentence-transformers (for sentence embedding models)
```bash
pip install sentence-transformers
```

### torch (for GPU acceleration)
```bash
# CPU-only version
pip install torch --index-url https://download.pytorch.org/whl/cpu

# GPU version (CUDA 11.8)
pip install torch --index-url https://download.pytorch.org/whl/cu118
```

## Verification Commands
After installation, verify your setup:
```bash
kob tokenizer dependencies
kob tokenizer status
kob tokenizer test
```
```

### `kob tokenizer list-models`

List supported models and their token limits.

**Syntax:**
```bash
kob tokenizer list-models [OPTIONS]
```

**Options:**
- `--adapter NAME` - Show models for specific adapter only
- `--format FORMAT` - Output format (`markdown`, `json`, `csv`)

**Examples:**
```bash
# List all supported models
kob tokenizer list-models

# List OpenAI models only
kob tokenizer list-models --adapter tiktoken

# List HuggingFace models only
kob tokenizer list-models --adapter huggingface

# JSON output for scripting
kob tokenizer list-models --format json
```

**Sample Output:**
```markdown
# Supported Models

**Total Models:** 45

## OpenAI Models (15 models)

| Model | Max Tokens | Encoding | Recommended Adapter |
|-------|------------|----------|-------------------|
| gpt-4 | 8192 | cl100k_base | tiktoken |
| gpt-4-turbo | 128000 | cl100k_base | tiktoken |
| gpt-3.5-turbo | 4096 | cl100k_base | tiktoken |
| text-embedding-3-small | 8192 | cl100k_base | tiktoken |
| text-embedding-3-large | 8192 | cl100k_base | tiktoken |

## HuggingFace Models (25 models)

| Model | Max Tokens | Encoding | Recommended Adapter |
|-------|------------|----------|-------------------|
| bert-base-uncased | 512 | N/A | huggingface |
| sentence-transformers/all-MiniLM-L6-v2 | 512 | N/A | huggingface |
| sentence-transformers/all-mpnet-base-v2 | 512 | N/A | huggingface |

## Usage Notes
- **Max Tokens:** Maximum context length for the model
- **Encoding:** TikToken encoding used (for OpenAI models)
- **Recommended Adapter:** Best adapter for accurate tokenization

### Examples
```bash
# Use with embedding command
kob embedding build --tokenizer-model text-embedding-3-small

# Test tokenization
kob tokenizer test --model bert-base-uncased --adapter huggingface
```
```

### `kob tokenizer recommend`

Get adapter recommendation for a specific model and requirements.

**Syntax:**
```bash
kob tokenizer recommend MODEL [OPTIONS]
```

**Arguments:**
- `MODEL` - Model name to get recommendation for

**Options:**
- `--requirements` - Requirements as key=value pairs (e.g., `accuracy=high,speed=medium`)

**Examples:**
```bash
# Get recommendation for OpenAI model
kob tokenizer recommend gpt-4

# Get recommendation for HuggingFace model
kob tokenizer recommend bert-base-uncased

# Get recommendation with requirements
kob tokenizer recommend gpt-4 --requirements "accuracy=high,speed=medium"
```

**Sample Output:**
```markdown
# Adapter Recommendation for 'gpt-4'

**Recommended Adapter:** tiktoken

## Reasoning
- Model appears to be an OpenAI model
- TikToken provides exact tokenization for OpenAI models

## Available Alternatives
- ✅ **heuristic**
  - Fast approximation, good for development
- ❌ **huggingface**
  - Not available: No module named 'transformers'

## Usage Example
```bash
# Use recommended adapter in embedding command
kob embedding build --tokenizer-adapter tiktoken --tokenizer-model gpt-4

# Test the adapter
kob tokenizer test --text 'Sample text' --adapter tiktoken --model gpt-4
```
```

## Usage Patterns

### Basic Testing Workflow

```bash
# 1. Check system status
kob tokenizer status

# 2. Test basic functionality
kob tokenizer test

# 3. Compare adapters
kob tokenizer benchmark --text "Your sample text"

# 4. Validate configuration
kob tokenizer validate
```

### Configuration Workflow

```bash
# 1. Create example configuration
kob tokenizer create-example --output my_config.toml

# 2. Edit configuration file
# (edit my_config.toml)

# 3. Validate configuration
kob tokenizer validate --config my_config.toml

# 4. Test configuration
kob tokenizer test --config my_config.toml

# 5. Benchmark performance
kob tokenizer benchmark --config my_config.toml
```

### Troubleshooting Workflow

```bash
# 1. Check overall health
kob tokenizer status --verbose

# 2. Check dependencies
kob tokenizer dependencies --verbose

# 3. Check adapter health
kob tokenizer health-check tiktoken
kob tokenizer health-check huggingface
kob tokenizer health-check heuristic

# 4. Run diagnostics
kob tokenizer adapter-status --verbose

# 5. Get installation guide
kob tokenizer install-guide
```

### Performance Analysis Workflow

```bash
# 1. Benchmark current setup
kob tokenizer benchmark --format json > baseline.json

# 2. Test with different configurations
export KANO_TOKENIZER_ADAPTER=heuristic
kob tokenizer benchmark --format json > heuristic.json

export KANO_TOKENIZER_ADAPTER=tiktoken
kob tokenizer benchmark --format json > tiktoken.json

# 3. Compare results
# (analyze JSON files)

# 4. Choose optimal configuration
```

### Production Deployment Workflow

```bash
# 1. Create production configuration
kob tokenizer create-example --output production_config.toml
# (edit production_config.toml)

# 2. Validate configuration
kob tokenizer validate --config production_config.toml

# 3. Test with production-like data
kob tokenizer test --config production_config.toml --text "$(cat sample_production_data.txt)"

# 4. Benchmark performance
kob tokenizer benchmark --config production_config.toml --iterations 50

# 5. Check system health
kob tokenizer status --config production_config.toml --verbose

# 6. Deploy configuration
cp production_config.toml /etc/kano/tokenizer.toml
```

## Output Formats

### JSON Format

Most commands support `--format json` for machine-readable output:

```bash
kob tokenizer status --format json
kob tokenizer benchmark --format json
kob tokenizer config --format json
```

**Example JSON Output:**
```json
{
  "overall_health": "healthy",
  "python_version": "3.11.0",
  "python_compatible": true,
  "configuration": {
    "adapter": "auto",
    "model": "text-embedding-3-small",
    "max_tokens": null,
    "fallback_chain": ["tiktoken", "huggingface", "heuristic"]
  },
  "adapters": {
    "heuristic": {
      "available": true,
      "error": null
    },
    "tiktoken": {
      "available": true,
      "error": null
    },
    "huggingface": {
      "available": false,
      "error": "No module named 'transformers'"
    }
  }
}
```

### CSV Format

Benchmark and list commands support CSV output:

```bash
kob tokenizer benchmark --format csv
kob tokenizer list-models --format csv
```

### TOML/YAML Formats

Configuration commands support multiple formats:

```bash
kob tokenizer config --format toml
kob tokenizer config --format yaml
```

---

This CLI reference provides comprehensive documentation for all tokenizer adapter commands. Use `kob tokenizer <command> --help` for detailed help on any specific command.
