# Tokenizer Adapters Configuration Reference

**Complete configuration reference for tokenizer adapters**

This guide provides comprehensive configuration options, examples, and best practices for configuring tokenizer adapters.

## Table of Contents

- [Configuration Overview](#configuration-overview)
- [Configuration File Format](#configuration-file-format)
- [Configuration Options](#configuration-options)
- [Environment Variables](#environment-variables)
- [Configuration Examples](#configuration-examples)
- [Migration Guide](#migration-guide)
- [Validation and Testing](#validation-and-testing)

## Configuration Overview

### Configuration Hierarchy

Tokenizer adapter configuration follows this precedence order (highest to lowest):

1. **Environment Variables** - Runtime overrides
2. **Configuration File** - Explicit TOML/JSON configuration
3. **Default Values** - Built-in sensible defaults

### Configuration Sources

```bash
# 1. Environment variables (highest precedence)
export KANO_TOKENIZER_ADAPTER=tiktoken

# 2. Configuration file
kob tokenizer test --config my_config.toml

# 3. Default configuration (lowest precedence)
kob tokenizer test  # Uses built-in defaults
```

### Configuration File Locations

The system searches for configuration files in this order:

1. Explicitly specified: `--config path/to/config.toml`
2. Current directory: `./tokenizer_config.toml`
3. Current directory: `./config.toml` (tokenizer section)
4. User config: `~/.config/kano/tokenizer.toml`

## Configuration File Format

### TOML Format (Recommended)

```toml
# tokenizer_config.toml
[tokenizer]
# Main configuration
adapter = "auto"
model = "text-embedding-3-small"
max_tokens = 8192
fallback_chain = ["tiktoken", "huggingface", "heuristic"]

# Adapter-specific configurations
[tokenizer.heuristic]
chars_per_token = 4.0

[tokenizer.tiktoken]
encoding = "cl100k_base"

[tokenizer.huggingface]
use_fast = true
trust_remote_code = false
```

### JSON Format (Legacy)

```json
{
  "tokenizer": {
    "adapter": "auto",
    "model": "text-embedding-3-small",
    "max_tokens": 8192,
    "fallback_chain": ["tiktoken", "huggingface", "heuristic"],
    "heuristic": {
      "chars_per_token": 4.0
    },
    "tiktoken": {
      "encoding": "cl100k_base"
    },
    "huggingface": {
      "use_fast": true,
      "trust_remote_code": false
    }
  }
}
```

### Embedded Configuration

Configuration can be embedded in larger configuration files:

```toml
# config.toml (application configuration)
[app]
name = "My Application"
version = "1.0.0"

[database]
url = "sqlite:///app.db"

# Tokenizer configuration section
[tokenizer]
adapter = "tiktoken"
model = "gpt-4"

[tokenizer.tiktoken]
encoding = "cl100k_base"
```

## Configuration Options

### Main Configuration Section

#### `adapter` (string)

**Description:** Primary tokenizer adapter to use.

**Values:**
- `"auto"` - Use fallback chain (recommended)
- `"heuristic"` - Fast approximation adapter
- `"tiktoken"` - OpenAI TikToken adapter
- `"huggingface"` - HuggingFace transformers adapter

**Default:** `"auto"`

**Examples:**
```toml
# Use specific adapter
adapter = "tiktoken"

# Use fallback chain
adapter = "auto"
```

#### `model` (string)

**Description:** Model name for tokenization and token limit resolution.

**Default:** `"text-embedding-3-small"`

**Examples:**
```toml
# OpenAI models
model = "gpt-4"
model = "gpt-3.5-turbo"
model = "text-embedding-3-large"

# HuggingFace models
model = "bert-base-uncased"
model = "sentence-transformers/all-MiniLM-L6-v2"

# Custom models
model = "my-custom-model"
```

#### `max_tokens` (integer, optional)

**Description:** Override the model's maximum token limit.

**Default:** `null` (use model's default limit)

**Examples:**
```toml
# Override token limit
max_tokens = 4096

# Use model default
# max_tokens = null  (or omit)
```

#### `fallback_chain` (array of strings)

**Description:** Ordered list of adapters to try when primary adapter fails.

**Default:** `["tiktoken", "huggingface", "heuristic"]`

**Examples:**
```toml
# Standard fallback chain
fallback_chain = ["tiktoken", "huggingface", "heuristic"]

# Speed-optimized chain
fallback_chain = ["heuristic"]

# Accuracy-first chain
fallback_chain = ["tiktoken", "huggingface"]

# Custom order
fallback_chain = ["huggingface", "tiktoken", "heuristic"]
```

### Heuristic Adapter Configuration

#### `[tokenizer.heuristic]`

**Description:** Configuration for the heuristic (approximation) adapter.

#### `chars_per_token` (float)

**Description:** Average number of characters per token for estimation.

**Default:** `4.0`

**Range:** `> 0.0` (must be positive)

**Language-Specific Recommendations:**
```toml
# English technical content
[tokenizer.heuristic]
chars_per_token = 4.2

# Code-heavy content
[tokenizer.heuristic]
chars_per_token = 3.8

# Academic papers (punctuation-heavy)
[tokenizer.heuristic]
chars_per_token = 4.5

# CJK-heavy content
[tokenizer.heuristic]
chars_per_token = 2.0

# Mixed content
[tokenizer.heuristic]
chars_per_token = 4.0  # Good general-purpose value
```

### TikToken Adapter Configuration

#### `[tokenizer.tiktoken]`

**Description:** Configuration for the TikToken (OpenAI) adapter.

#### `encoding` (string, optional)

**Description:** Specific TikToken encoding to use.

**Default:** `null` (auto-detect based on model)

**Available Encodings:**
- `"cl100k_base"` - GPT-4, GPT-3.5-turbo, text-embedding-3-*
- `"p50k_base"` - text-davinci-003, code-davinci-002
- `"r50k_base"` - GPT-3 models (davinci, curie, babbage, ada)
- `"gpt2"` - GPT-2 models

**Examples:**
```toml
# Auto-detect encoding (recommended)
[tokenizer.tiktoken]
# encoding not specified

# Explicit encoding
[tokenizer.tiktoken]
encoding = "cl100k_base"

# Legacy model encoding
[tokenizer.tiktoken]
encoding = "p50k_base"
```

### HuggingFace Adapter Configuration

#### `[tokenizer.huggingface]`

**Description:** Configuration for the HuggingFace transformers adapter.

#### `use_fast` (boolean)

**Description:** Use fast tokenizer implementation when available.

**Default:** `true`

**Benefits of Fast Tokenizers:**
- Significantly faster tokenization
- Better memory efficiency
- Consistent with HuggingFace model training

**Examples:**
```toml
# Use fast tokenizers (recommended)
[tokenizer.huggingface]
use_fast = true

# Use slow tokenizers (compatibility)
[tokenizer.huggingface]
use_fast = false
```

#### `trust_remote_code` (boolean)

**Description:** Allow execution of remote code from model repositories.

**Default:** `false`

**Security Note:** Setting this to `true` allows arbitrary code execution from model repositories. Only enable for trusted models.

**Examples:**
```toml
# Secure setting (recommended)
[tokenizer.huggingface]
trust_remote_code = false

# Allow remote code (use with caution)
[tokenizer.huggingface]
trust_remote_code = true
```

## Environment Variables

### Main Configuration Variables

#### `KANO_TOKENIZER_ADAPTER`

**Description:** Override adapter selection.

**Values:** `auto`, `heuristic`, `tiktoken`, `huggingface`

**Example:**
```bash
export KANO_TOKENIZER_ADAPTER=tiktoken
```

#### `KANO_TOKENIZER_MODEL`

**Description:** Override model name.

**Example:**
```bash
export KANO_TOKENIZER_MODEL=gpt-4
export KANO_TOKENIZER_MODEL=bert-base-uncased
```

#### `KANO_TOKENIZER_MAX_TOKENS`

**Description:** Override maximum token limit.

**Example:**
```bash
export KANO_TOKENIZER_MAX_TOKENS=4096
```

### Adapter-Specific Variables

#### Heuristic Adapter

```bash
# Override chars per token ratio
export KANO_TOKENIZER_HEURISTIC_CHARS_PER_TOKEN=4.2
```

#### TikToken Adapter

```bash
# Override encoding
export KANO_TOKENIZER_TIKTOKEN_ENCODING=cl100k_base
```

#### HuggingFace Adapter

```bash
# Override fast tokenizer setting
export KANO_TOKENIZER_HUGGINGFACE_USE_FAST=true
export KANO_TOKENIZER_HUGGINGFACE_USE_FAST=false

# Override trust remote code setting
export KANO_TOKENIZER_HUGGINGFACE_TRUST_REMOTE_CODE=false
export KANO_TOKENIZER_HUGGINGFACE_TRUST_REMOTE_CODE=true
```

### Environment Variable Format

**Boolean Values:**
```bash
# Accepted true values
export KANO_TOKENIZER_HUGGINGFACE_USE_FAST=true
export KANO_TOKENIZER_HUGGINGFACE_USE_FAST=1
export KANO_TOKENIZER_HUGGINGFACE_USE_FAST=yes
export KANO_TOKENIZER_HUGGINGFACE_USE_FAST=on

# Accepted false values
export KANO_TOKENIZER_HUGGINGFACE_USE_FAST=false
export KANO_TOKENIZER_HUGGINGFACE_USE_FAST=0
export KANO_TOKENIZER_HUGGINGFACE_USE_FAST=no
export KANO_TOKENIZER_HUGGINGFACE_USE_FAST=off
```

**Numeric Values:**
```bash
# Integer values
export KANO_TOKENIZER_MAX_TOKENS=4096

# Float values
export KANO_TOKENIZER_HEURISTIC_CHARS_PER_TOKEN=4.2
```

## Configuration Examples

### Development Configuration

**Fast, dependency-free setup for development:**

```toml
# dev_config.toml
[tokenizer]
adapter = "heuristic"
model = "development-model"
fallback_chain = ["heuristic"]

[tokenizer.heuristic]
chars_per_token = 4.0
```

**Usage:**
```bash
export KANO_TOKENIZER_CONFIG=dev_config.toml
kob tokenizer test
```

### Production OpenAI Configuration

**Optimized for OpenAI models in production:**

```toml
# production_openai_config.toml
[tokenizer]
adapter = "tiktoken"
model = "text-embedding-3-small"
fallback_chain = ["tiktoken", "heuristic"]

[tokenizer.tiktoken]
# Let encoding be auto-detected

[tokenizer.heuristic]
chars_per_token = 4.0  # Fallback configuration
```

**Environment overrides:**
```bash
# Override for different models
export KANO_TOKENIZER_MODEL=gpt-4
export KANO_TOKENIZER_MAX_TOKENS=8192
```

### Production HuggingFace Configuration

**Optimized for HuggingFace models:**

```toml
# production_hf_config.toml
[tokenizer]
adapter = "huggingface"
model = "sentence-transformers/all-MiniLM-L6-v2"
fallback_chain = ["huggingface", "heuristic"]

[tokenizer.huggingface]
use_fast = true
trust_remote_code = false

[tokenizer.heuristic]
chars_per_token = 4.0
```

### Multi-Environment Configuration

**Flexible configuration for multiple environments:**

```toml
# multi_env_config.toml
[tokenizer]
adapter = "auto"
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

**Environment-specific overrides:**
```bash
# Development
export KANO_TOKENIZER_ADAPTER=heuristic

# Staging
export KANO_TOKENIZER_ADAPTER=tiktoken

# Production
export KANO_TOKENIZER_ADAPTER=auto
```

### High-Performance Configuration

**Optimized for maximum performance:**

```toml
# performance_config.toml
[tokenizer]
adapter = "heuristic"
model = "performance-model"
fallback_chain = ["heuristic"]

[tokenizer.heuristic]
chars_per_token = 4.0
```

### High-Accuracy Configuration

**Optimized for maximum accuracy:**

```toml
# accuracy_config.toml
[tokenizer]
adapter = "auto"
model = "gpt-4"
fallback_chain = ["tiktoken", "huggingface", "heuristic"]

[tokenizer.tiktoken]
# Use auto-detection for best accuracy

[tokenizer.huggingface]
use_fast = true  # Fast tokenizers are still accurate
trust_remote_code = false
```

### Language-Specific Configurations

#### English Technical Content
```toml
[tokenizer]
adapter = "auto"
model = "text-embedding-3-small"

[tokenizer.heuristic]
chars_per_token = 4.2  # Optimized for technical English
```

#### Code-Heavy Content
```toml
[tokenizer]
adapter = "auto"
model = "code-davinci-002"

[tokenizer.heuristic]
chars_per_token = 3.8  # Code has more punctuation/symbols
```

#### Academic Papers
```toml
[tokenizer]
adapter = "auto"
model = "gpt-4"

[tokenizer.heuristic]
chars_per_token = 4.5  # Academic text has more punctuation
```

#### Multilingual Content
```toml
[tokenizer]
adapter = "auto"
model = "sentence-transformers/paraphrase-multilingual-MiniLM-L12-v2"

[tokenizer.heuristic]
chars_per_token = 3.5  # Mixed languages, more conservative estimate
```

### Docker Configuration

**Configuration for containerized environments:**

```toml
# docker_config.toml
[tokenizer]
adapter = "auto"
model = "text-embedding-3-small"
fallback_chain = ["heuristic", "tiktoken", "huggingface"]  # Heuristic first for reliability

[tokenizer.heuristic]
chars_per_token = 4.0

[tokenizer.tiktoken]
# encoding auto-detected

[tokenizer.huggingface]
use_fast = true
trust_remote_code = false
```

**Dockerfile:**
```dockerfile
FROM python:3.11-slim

# Install dependencies
RUN pip install tiktoken transformers

# Copy configuration
COPY docker_config.toml /app/tokenizer_config.toml

# Set environment
ENV KANO_TOKENIZER_CONFIG=/app/tokenizer_config.toml

# Your application
COPY . /app
WORKDIR /app
```

### CI/CD Configuration

**Configuration for continuous integration:**

```toml
# ci_config.toml
[tokenizer]
adapter = "heuristic"  # Reliable in CI environments
model = "ci-test-model"
fallback_chain = ["heuristic"]

[tokenizer.heuristic]
chars_per_token = 4.0
```

**GitHub Actions:**
```yaml
- name: Test tokenizers
  env:
    KANO_TOKENIZER_CONFIG: ci_config.toml
  run: |
    kob tokenizer test
    kob tokenizer validate
```

## Migration Guide

### From JSON to TOML

**Old JSON Configuration:**
```json
{
  "tokenizer": {
    "adapter": "tiktoken",
    "model": "gpt-4",
    "options": {
      "heuristic": {
        "chars_per_token": 4.0
      },
      "tiktoken": {
        "encoding": "cl100k_base"
      }
    }
  }
}
```

**New TOML Configuration:**
```toml
[tokenizer]
adapter = "tiktoken"
model = "gpt-4"

[tokenizer.heuristic]
chars_per_token = 4.0

[tokenizer.tiktoken]
encoding = "cl100k_base"
```

**Migration Command:**
```bash
kob tokenizer migrate old_config.json --output new_config.toml
```

### From Environment-Only to Configuration File

**Old Environment Variables:**
```bash
export KANO_TOKENIZER_ADAPTER=tiktoken
export KANO_TOKENIZER_MODEL=gpt-4
export KANO_TOKENIZER_HEURISTIC_CHARS_PER_TOKEN=4.2
```

**New Configuration File:**
```toml
[tokenizer]
adapter = "tiktoken"
model = "gpt-4"

[tokenizer.heuristic]
chars_per_token = 4.2
```

**Migration Steps:**
1. Create configuration file with current settings
2. Test configuration: `kob tokenizer validate --config new_config.toml`
3. Update deployment to use configuration file
4. Remove environment variables (optional)

### From Legacy Pipeline Configuration

**Old Pipeline Configuration:**
```toml
[pipeline]
tokenizer_adapter = "tiktoken"
tokenizer_model = "gpt-4"
chars_per_token = 4.0
```

**New Tokenizer Configuration:**
```toml
[tokenizer]
adapter = "tiktoken"
model = "gpt-4"

[tokenizer.heuristic]
chars_per_token = 4.0
```

## Validation and Testing

### Configuration Validation

**Validate Configuration File:**
```bash
# Validate TOML syntax and values
kob tokenizer validate --config your_config.toml

# Show parsed configuration
kob tokenizer config --config your_config.toml --format json
```

**Common Validation Errors:**

1. **Invalid TOML Syntax:**
```toml
# Wrong: string instead of number
chars_per_token = "4.0"

# Right: numeric value
chars_per_token = 4.0
```

2. **Invalid Boolean Values:**
```toml
# Wrong: string instead of boolean
use_fast = "true"

# Right: boolean value
use_fast = true
```

3. **Invalid Array Format:**
```toml
# Wrong: string instead of array
fallback_chain = "tiktoken,heuristic"

# Right: array format
fallback_chain = ["tiktoken", "heuristic"]
```

### Configuration Testing

**Test Configuration:**
```bash
# Test with sample text
kob tokenizer test --config your_config.toml

# Test all adapters in fallback chain
kob tokenizer test --config your_config.toml --verbose

# Benchmark performance
kob tokenizer benchmark --config your_config.toml
```

**Test Environment Variable Overrides:**
```bash
# Test override
export KANO_TOKENIZER_ADAPTER=heuristic
kob tokenizer test --config your_config.toml

# Verify override took effect
kob tokenizer config --config your_config.toml --format json | jq '.adapter'
```

### Configuration Debugging

**Debug Configuration Loading:**
```bash
# Show effective configuration
kob tokenizer config --format json

# Show configuration with specific file
kob tokenizer config --config your_config.toml --format json

# Show environment variables
kob tokenizer env-vars
```

**Debug Adapter Resolution:**
```bash
# Show which adapter will be used
kob tokenizer adapter-status --config your_config.toml

# Test adapter availability
kob tokenizer adapter-status
```

### Configuration Best Practices

1. **Use TOML Format:** More readable and maintainable than JSON
2. **Version Control:** Keep configuration files in version control
3. **Environment Separation:** Use different configs for dev/staging/prod
4. **Validation:** Always validate configuration before deployment
5. **Documentation:** Document any custom configuration choices
6. **Testing:** Test configuration changes thoroughly
7. **Fallback Chain:** Always include heuristic adapter as final fallback
8. **Security:** Never set `trust_remote_code = true` in production

### Configuration Templates

**Create Configuration Template:**
```bash
# Create example configuration
kob tokenizer create-example --output template_config.toml

# Customize for your needs
cp template_config.toml my_config.toml
# Edit my_config.toml

# Validate customized configuration
kob tokenizer validate --config my_config.toml
```

---

This configuration reference provides comprehensive guidance for configuring tokenizer adapters across different environments and use cases. Regular validation and testing ensure reliable operation in production systems.
