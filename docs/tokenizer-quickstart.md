# Tokenizer Adapters Quick Start Guide

**Get started with tokenizer adapters in 5 minutes**

This guide gets you up and running with tokenizer adapters quickly, covering the most common use cases.

## 1. Check System Status (30 seconds)

```bash
# Check overall health
kob tokenizer status

# Check which adapters are available
kob tokenizer adapter-status
```

**Expected Output:**
```
✅ Overall Health: HEALTHY
🐍 Python Version: 3.11.0 ✅

## Configuration
- Adapter: auto
- Model: text-embedding-3-small
- Max Tokens: auto
- Fallback Chain: tiktoken → huggingface → heuristic

## Adapter Status
✅ HEURISTIC
- Status: Available
- Dependencies: Ready

✅ TIKTOKEN  
- Status: Available
- Dependencies: Ready

❌ HUGGINGFACE
- Status: Not available
- Error: No module named 'transformers'
```

## 2. Install Missing Dependencies (1-2 minutes)

Based on your status check, install missing dependencies:

```bash
# For OpenAI models (recommended)
pip install tiktoken

# For HuggingFace models (optional)
pip install transformers

# Verify installation
kob tokenizer dependencies
```

## 3. Test Basic Functionality (30 seconds)

```bash
# Test with sample text
kob tokenizer test

# Test specific adapter
kob tokenizer test --adapter tiktoken --model gpt-4

# Compare adapters
kob tokenizer benchmark --text "This is a sample text for tokenization testing."
```

**Expected Output:**
```
Testing tokenizers with text: 'This is a sample text for tokenization testing.'
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
```

## 4. Choose Your Configuration (1 minute)

### Option A: OpenAI Models (Recommended)

```bash
# Create configuration for OpenAI models
kob tokenizer create-example --output openai_config.toml

# Edit the file to use tiktoken
cat > openai_config.toml << 'EOF'
[tokenizer]
adapter = "tiktoken"
model = "text-embedding-3-small"
fallback_chain = ["tiktoken", "heuristic"]

[tokenizer.tiktoken]
# encoding auto-detected

[tokenizer.heuristic]
chars_per_token = 4.0
EOF

# Test configuration
kob tokenizer validate --config openai_config.toml
kob tokenizer test --config openai_config.toml
```

### Option B: HuggingFace Models

```bash
# Create configuration for HuggingFace models
cat > huggingface_config.toml << 'EOF'
[tokenizer]
adapter = "huggingface"
model = "sentence-transformers/all-MiniLM-L6-v2"
fallback_chain = ["huggingface", "heuristic"]

[tokenizer.huggingface]
use_fast = true
trust_remote_code = false

[tokenizer.heuristic]
chars_per_token = 4.0
EOF

# Test configuration
kob tokenizer validate --config huggingface_config.toml
kob tokenizer test --config huggingface_config.toml
```

### Option C: Development/Fast Setup

```bash
# Use heuristic adapter (no dependencies required)
export KANO_TOKENIZER_ADAPTER=heuristic
kob tokenizer test
```

## 5. Integration with Embedding Pipeline (30 seconds)

```bash
# Use tokenizer in embedding commands
kob embedding build --tokenizer-adapter tiktoken --tokenizer-model gpt-4

# Or set environment variables
export KANO_TOKENIZER_ADAPTER=tiktoken
export KANO_TOKENIZER_MODEL=gpt-4
kob embedding build
```

## Common Use Cases

### Use Case 1: OpenAI API Integration

**Goal:** Accurate token counting for OpenAI API cost estimation.

**Setup:**
```bash
pip install tiktoken
export KANO_TOKENIZER_ADAPTER=tiktoken
export KANO_TOKENIZER_MODEL=gpt-4
```

**Test:**
```bash
kob tokenizer test --text "Your API request text here"
```

### Use Case 2: HuggingFace Model Processing

**Goal:** Exact tokenization for HuggingFace embedding models.

**Setup:**
```bash
pip install transformers
export KANO_TOKENIZER_ADAPTER=huggingface
export KANO_TOKENIZER_MODEL=sentence-transformers/all-MiniLM-L6-v2
```

**Test:**
```bash
kob tokenizer test --text "Your document text here"
```

### Use Case 3: Development/Testing

**Goal:** Fast tokenization without external dependencies.

**Setup:**
```bash
export KANO_TOKENIZER_ADAPTER=heuristic
export KANO_TOKENIZER_HEURISTIC_CHARS_PER_TOKEN=4.0
```

**Test:**
```bash
kob tokenizer test --text "Development test text"
```

### Use Case 4: Production Multi-Model

**Goal:** Reliable tokenization with fallback for multiple model types.

**Setup:**
```bash
pip install tiktoken transformers

cat > production_config.toml << 'EOF'
[tokenizer]
adapter = "auto"
model = "text-embedding-3-small"
fallback_chain = ["tiktoken", "huggingface", "heuristic"]

[tokenizer.heuristic]
chars_per_token = 4.0

[tokenizer.tiktoken]
# encoding auto-detected

[tokenizer.huggingface]
use_fast = true
trust_remote_code = false
EOF
```

**Test:**
```bash
kob tokenizer validate --config production_config.toml
kob tokenizer test --config production_config.toml
```

## Quick Troubleshooting

### Problem: "No tokenizer adapter available"

**Solution:**
```bash
# Use heuristic adapter as fallback
export KANO_TOKENIZER_ADAPTER=heuristic
kob tokenizer test
```

### Problem: "tiktoken package required"

**Solution:**
```bash
pip install tiktoken
kob tokenizer test --adapter tiktoken
```

### Problem: "transformers package required"

**Solution:**
```bash
pip install transformers
kob tokenizer test --adapter huggingface
```

### Problem: Token counts seem inaccurate

**Solution:**
```bash
# Compare adapters to see differences
kob tokenizer benchmark --text "Your text here"

# Use exact adapter for your model type
kob tokenizer recommend your-model-name
```

### Problem: Slow performance

**Solution:**
```bash
# Use heuristic adapter for speed
export KANO_TOKENIZER_ADAPTER=heuristic

# Or enable fast tokenizers for HuggingFace
export KANO_TOKENIZER_HUGGINGFACE_USE_FAST=true
```

## Next Steps

### Learn More

- **[Complete Documentation](tokenizer-adapters.md)** - Comprehensive user guide
- **[Configuration Reference](tokenizer-configuration.md)** - All configuration options
- **[Troubleshooting Guide](tokenizer-troubleshooting.md)** - Detailed problem solving
- **[Performance Tuning](tokenizer-performance.md)** - Optimization strategies

### Advanced Usage

```bash
# Benchmark your setup
kob tokenizer benchmark --text "$(cat your_typical_document.txt)"

# Get model recommendations
kob tokenizer recommend gpt-4
kob tokenizer recommend bert-base-uncased

# Monitor system health
kob tokenizer status --verbose

# Create custom configuration
kob tokenizer create-example --output my_config.toml
# Edit my_config.toml for your needs
kob tokenizer validate --config my_config.toml
```

### Production Deployment

1. **Choose Configuration:** Create appropriate config file for your environment
2. **Validate Setup:** Run `kob tokenizer status` and `kob tokenizer test`
3. **Performance Test:** Run `kob tokenizer benchmark` with your typical content
4. **Monitor Health:** Set up regular `kob tokenizer status` checks
5. **Update Dependencies:** Keep `tiktoken` and `transformers` packages updated

---

**You're now ready to use tokenizer adapters!** 

For detailed information on any topic, see the complete documentation linked above.
