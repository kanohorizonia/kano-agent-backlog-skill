# Tokenizer Adapters Troubleshooting Guide

**Comprehensive troubleshooting for tokenizer adapter issues**

This guide provides detailed solutions for common tokenizer adapter problems, error messages, and configuration issues.

## Table of Contents

- [Quick Diagnosis](#quick-diagnosis)
- [Common Error Messages](#common-error-messages)
- [Dependency Issues](#dependency-issues)
- [Configuration Problems](#configuration-problems)
- [Performance Issues](#performance-issues)
- [Model-Specific Issues](#model-specific-issues)
- [Environment Problems](#environment-problems)
- [Advanced Troubleshooting](#advanced-troubleshooting)

## Quick Diagnosis

### Step 1: Check System Health
```bash
# Get overall system status
kob tokenizer status

# Check what's broken
kob tokenizer adapter-status --verbose

# Check dependencies
kob tokenizer dependencies
```

### Step 2: Test Basic Functionality
```bash
# Test with simple text
kob tokenizer test --text "Hello world"

# Test specific adapter
kob tokenizer test --adapter heuristic --text "Hello world"

# Compare adapters
kob tokenizer benchmark --text "Hello world"
```

### Step 3: Check Configuration
```bash
# Validate configuration
kob tokenizer validate

# Show effective configuration
kob tokenizer config --format json

# Check environment variables
kob tokenizer env-vars
```

## Common Error Messages

### 1. "No tokenizer adapter available"

**Full Error:**
```
FallbackChainExhaustedError: No tokenizer available. Errors: [tiktoken: No module named 'tiktoken', huggingface: No module named 'transformers', heuristic: <error>]
```

**Cause:** All adapters in the fallback chain failed to initialize.

**Solutions:**

**Quick Fix - Use Heuristic:**
```bash
export KANO_TOKENIZER_ADAPTER=heuristic
kob tokenizer test
```

**Install Dependencies:**
```bash
# For OpenAI models
pip install tiktoken

# For HuggingFace models  
pip install transformers

# Verify installation
kob tokenizer dependencies
```

**Check Python Environment:**
```bash
# Ensure correct environment
which python
pip list | grep -E "(tiktoken|transformers)"

# If using conda
conda list | grep -E "(tiktoken|transformers)"
```

### 2. "tiktoken package required"

**Full Error:**
```
ImportError: tiktoken package required for TiktokenAdapter. Install with: pip install tiktoken
```

**Cause:** TikToken dependency not installed.

**Solutions:**

**Install TikToken:**
```bash
pip install tiktoken

# Verify installation
python -c "import tiktoken; print('TikToken version:', tiktoken.__version__)"

# Test adapter
kob tokenizer health-check tiktoken
```

**Alternative Installation Methods:**
```bash
# Using conda
conda install tiktoken -c conda-forge

# Using pip with specific version
pip install "tiktoken>=0.5.0"

# In requirements.txt
echo "tiktoken>=0.5.0" >> requirements.txt
pip install -r requirements.txt
```

**If Installation Fails:**
```bash
# Check pip version
pip --version

# Upgrade pip
pip install --upgrade pip

# Try with --user flag
pip install --user tiktoken

# Check for conflicts
pip check
```

### 3. "transformers package required"

**Full Error:**
```
ImportError: transformers package required for HuggingFaceAdapter
```

**Cause:** HuggingFace transformers dependency not installed.

**Solutions:**

**Install Transformers:**
```bash
pip install transformers

# For sentence-transformers models
pip install sentence-transformers

# Verify installation
python -c "import transformers; print('Transformers version:', transformers.__version__)"
```

**Handle Large Installation:**
```bash
# Transformers is large, ensure enough space
df -h

# Install with progress bar
pip install transformers --progress-bar pretty

# Install minimal version (if available)
pip install transformers[torch]
```

### 4. "Can't load tokenizer for model"

**Full Error:**
```
OSError: Can't load tokenizer for 'unknown-model'. Make sure that 'unknown-model' is a correct model identifier
```

**Cause:** Invalid or unsupported model name.

**Solutions:**

**Check Supported Models:**
```bash
# List all supported models
kob tokenizer list-models

# Check specific adapter models
kob tokenizer list-models --adapter huggingface
kob tokenizer list-models --adapter tiktoken
```

**Use Valid Model Names:**
```bash
# For HuggingFace models
kob tokenizer test --adapter huggingface --model bert-base-uncased

# For OpenAI models
kob tokenizer test --adapter tiktoken --model gpt-4
```

**Get Model Recommendations:**
```bash
# Get recommendation for your use case
kob tokenizer recommend bert-base-uncased
kob tokenizer recommend gpt-4
```

### 5. "Configuration validation failed"

**Full Error:**
```
ConfigError: heuristic.chars_per_token must be a positive number
```

**Cause:** Invalid configuration values.

**Solutions:**

**Fix Configuration File:**
```toml
[tokenizer.heuristic]
chars_per_token = 4.0  # Must be positive number, not string

[tokenizer.huggingface]
use_fast = true        # Must be boolean, not "true"
trust_remote_code = false  # Must be boolean, not "false"
```

**Validate Configuration:**
```bash
# Check configuration syntax
kob tokenizer validate --config your_config.toml

# Show parsed configuration
kob tokenizer config --config your_config.toml --format json
```

**Fix Environment Variables:**
```bash
# Correct format
export KANO_TOKENIZER_HEURISTIC_CHARS_PER_TOKEN=4.0
export KANO_TOKENIZER_HUGGINGFACE_USE_FAST=true

# Not these formats
# export KANO_TOKENIZER_HEURISTIC_CHARS_PER_TOKEN="4.0"
# export KANO_TOKENIZER_HUGGINGFACE_USE_FAST="True"
```

### 6. "Tokenization failed"

**Full Error:**
```
TokenizationFailedError: Tokenization failed for adapter 'tiktoken' with model 'gpt-4': <specific error>
```

**Cause:** Runtime error during tokenization.

**Solutions:**

**Check Text Content:**
```bash
# Test with simple text first
kob tokenizer test --adapter tiktoken --text "Hello world"

# Check for problematic characters
python -c "
text = '''your problematic text here'''
print('Text length:', len(text))
print('Non-ASCII chars:', [c for c in text if ord(c) > 127])
print('Control chars:', [c for c in text if ord(c) < 32])
"
```

**Try Different Adapters:**
```bash
# Compare results
kob tokenizer benchmark --text "your problematic text"

# Use fallback
export KANO_TOKENIZER_ADAPTER=heuristic
kob tokenizer test --text "your problematic text"
```

## Dependency Issues

### TikToken Installation Problems

**Issue: Compilation Errors**
```bash
# Error during pip install tiktoken
# Building wheel for tiktoken (pyproject.toml) ... error
```

**Solutions:**
```bash
# Install build dependencies
pip install --upgrade pip setuptools wheel

# Install Rust (required for tiktoken)
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source ~/.cargo/env

# Try installation again
pip install tiktoken

# Alternative: use pre-built wheels
pip install --only-binary=all tiktoken
```

**Issue: Version Conflicts**
```bash
# ERROR: pip's dependency resolver does not currently take into account all the packages that are installed
```

**Solutions:**
```bash
# Check for conflicts
pip check

# Create clean environment
python -m venv tokenizer_env
source tokenizer_env/bin/activate  # On Windows: tokenizer_env\Scripts\activate
pip install tiktoken

# Or use conda
conda create -n tokenizer_env python=3.11
conda activate tokenizer_env
conda install tiktoken -c conda-forge
```

### HuggingFace Transformers Issues

**Issue: Large Download Size**
```bash
# Downloading transformers models takes too long or fails
```

**Solutions:**
```bash
# Set cache directory with more space
export HF_HOME=/path/to/large/disk/huggingface_cache

# Use offline mode after initial download
export TRANSFORMERS_OFFLINE=1

# Download specific model manually
python -c "
from transformers import AutoTokenizer
tokenizer = AutoTokenizer.from_pretrained('bert-base-uncased')
print('Model downloaded successfully')
"
```

**Issue: Network/Firewall Problems**
```bash
# HTTPSConnectionPool: Max retries exceeded
```

**Solutions:**
```bash
# Use proxy if needed
export HTTP_PROXY=http://your-proxy:port
export HTTPS_PROXY=http://your-proxy:port

# Use mirror (if available)
export HF_ENDPOINT=https://hf-mirror.com

# Download manually and use local path
# Download model files to local directory, then:
kob tokenizer test --adapter huggingface --model /path/to/local/model
```

### Python Environment Issues

**Issue: Wrong Python Version**
```bash
# Python 3.7 or older not supported
```

**Solutions:**
```bash
# Check Python version
python --version

# Install Python 3.8+
# On Ubuntu/Debian:
sudo apt update
sudo apt install python3.11

# On macOS with Homebrew:
brew install python@3.11

# On Windows: Download from python.org

# Use pyenv for version management
pyenv install 3.11.0
pyenv local 3.11.0
```

**Issue: Virtual Environment Problems**
```bash
# Packages installed but not found
```

**Solutions:**
```bash
# Ensure you're in the right environment
which python
which pip

# Activate virtual environment
source venv/bin/activate  # On Windows: venv\Scripts\activate

# Reinstall packages in current environment
pip install --force-reinstall tiktoken transformers

# Check installation location
pip show tiktoken
pip show transformers
```

## Configuration Problems

### TOML Syntax Errors

**Issue: Invalid TOML Format**
```bash
# toml.decoder.TomlDecodeError: Invalid value
```

**Solutions:**
```bash
# Validate TOML syntax
python -c "
import tomllib
with open('your_config.toml', 'rb') as f:
    data = tomllib.load(f)
print('TOML is valid')
"

# Common TOML mistakes:
# Wrong: chars_per_token = "4.0"  (string instead of number)
# Right: chars_per_token = 4.0

# Wrong: use_fast = "true"  (string instead of boolean)  
# Right: use_fast = true

# Wrong: fallback_chain = "tiktoken,heuristic"  (string instead of array)
# Right: fallback_chain = ["tiktoken", "heuristic"]
```

### Environment Variable Issues

**Issue: Environment Variables Not Working**
```bash
# Set KANO_TOKENIZER_ADAPTER=heuristic but still using tiktoken
```

**Solutions:**
```bash
# Check if variables are set
env | grep KANO_TOKENIZER

# Ensure proper export
export KANO_TOKENIZER_ADAPTER=heuristic  # Not just KANO_TOKENIZER_ADAPTER=heuristic

# Check variable precedence
kob tokenizer config --format json | jq '.adapter'

# Clear conflicting variables
unset KANO_TOKENIZER_ADAPTER
export KANO_TOKENIZER_ADAPTER=heuristic

# Test immediately
kob tokenizer test
```

**Issue: Boolean Environment Variables**
```bash
# Boolean values not recognized
```

**Solutions:**
```bash
# Correct boolean formats
export KANO_TOKENIZER_HUGGINGFACE_USE_FAST=true     # lowercase
export KANO_TOKENIZER_HUGGINGFACE_USE_FAST=false    # lowercase
export KANO_TOKENIZER_HUGGINGFACE_USE_FAST=1        # numeric
export KANO_TOKENIZER_HUGGINGFACE_USE_FAST=0        # numeric

# Not these formats:
# export KANO_TOKENIZER_HUGGINGFACE_USE_FAST=True   # Wrong case
# export KANO_TOKENIZER_HUGGINGFACE_USE_FAST="true" # Quoted
```

### Configuration File Location

**Issue: Configuration File Not Found**
```bash
# Config file not being loaded
```

**Solutions:**
```bash
# Specify config file explicitly
kob tokenizer test --config /path/to/your/config.toml

# Check default locations
ls -la tokenizer_config.toml
ls -la config.toml
ls -la ~/.config/kano/tokenizer.toml

# Create config in current directory
kob tokenizer create-example --output tokenizer_config.toml

# Verify config is loaded
kob tokenizer config --config tokenizer_config.toml
```

## Performance Issues

### Slow Tokenization

**Issue: Tokenization Takes Too Long**

**Diagnosis:**
```bash
# Benchmark current performance
kob tokenizer benchmark --iterations 10

# Test with different adapters
kob tokenizer benchmark --adapters heuristic,tiktoken --iterations 10

# Profile with large text
time kob tokenizer test --text "$(cat large_file.txt)"
```

**Solutions:**

**Use Faster Adapter:**
```bash
# Heuristic is fastest
export KANO_TOKENIZER_ADAPTER=heuristic

# Enable fast tokenizers for HuggingFace
export KANO_TOKENIZER_HUGGINGFACE_USE_FAST=true
```

**Optimize Configuration:**
```toml
[tokenizer]
adapter = "heuristic"  # Fastest option
fallback_chain = ["heuristic"]  # Skip slow adapters

[tokenizer.heuristic]
chars_per_token = 4.0  # Tune for your content
```

**System Optimization:**
```bash
# Check system resources
top
free -h

# Close unnecessary applications
# Ensure adequate RAM for large models

# Use SSD for HuggingFace cache
export HF_HOME=/path/to/ssd/cache
```

### High Memory Usage

**Issue: Tokenization Uses Too Much Memory**

**Diagnosis:**
```bash
# Monitor memory during tokenization
kob tokenizer test --text "$(cat large_file.txt)" &
PID=$!
while kill -0 $PID 2>/dev/null; do
    ps -o pid,vsz,rss,comm -p $PID
    sleep 1
done
```

**Solutions:**

**Use Memory-Efficient Adapters:**
```bash
# Heuristic uses minimal memory
export KANO_TOKENIZER_ADAPTER=heuristic

# For HuggingFace, use smaller models
export KANO_TOKENIZER_MODEL=distilbert-base-uncased  # Instead of bert-large
```

**Process Large Files in Chunks:**
```python
# Instead of processing entire file at once
def process_large_file(file_path, chunk_size=10000):
    with open(file_path, 'r') as f:
        while True:
            chunk = f.read(chunk_size)
            if not chunk:
                break
            # Process chunk
            result = adapter.count_tokens(chunk)
            yield result
```

### Startup Time Issues

**Issue: Slow Adapter Initialization**

**Solutions:**

**Use Lightweight Adapters:**
```toml
[tokenizer]
adapter = "heuristic"  # Instant startup
fallback_chain = ["heuristic", "tiktoken"]  # Heuristic first
```

**Pre-warm Adapters:**
```python
# Pre-initialize adapters at application startup
from kano_backlog_core.tokenizer import get_default_registry

registry = get_default_registry()
# This will initialize and cache adapters
adapter = registry.resolve("tiktoken", model_name="gpt-4")
adapter.count_tokens("warmup")  # Prime the adapter
```

## Model-Specific Issues

### OpenAI Model Issues

**Issue: Wrong Encoding for OpenAI Model**
```bash
# Token counts don't match OpenAI API
```

**Solutions:**
```bash
# Check model encoding
kob tokenizer list-models --adapter tiktoken | grep your-model

# Use correct encoding
export KANO_TOKENIZER_TIKTOKEN_ENCODING=cl100k_base  # For GPT-4, GPT-3.5-turbo
export KANO_TOKENIZER_TIKTOKEN_ENCODING=p50k_base    # For text-davinci-003

# Let system auto-detect
unset KANO_TOKENIZER_TIKTOKEN_ENCODING
kob tokenizer test --adapter tiktoken --model gpt-4
```

**Issue: Legacy Model Support**
```bash
# Old OpenAI models not recognized
```

**Solutions:**
```bash
# Check supported models
kob tokenizer list-models --adapter tiktoken

# Use manual encoding for unsupported models
export KANO_TOKENIZER_TIKTOKEN_ENCODING=p50k_base
kob tokenizer test --adapter tiktoken --model your-legacy-model
```

### HuggingFace Model Issues

**Issue: Model Not Found**
```bash
# OSError: Can't load tokenizer for 'custom-model'
```

**Solutions:**
```bash
# Check model exists on HuggingFace Hub
curl -s "https://huggingface.co/api/models/your-model-name" | jq .

# Use correct model identifier
kob tokenizer test --adapter huggingface --model "organization/model-name"

# For local models, use full path
kob tokenizer test --adapter huggingface --model "/path/to/local/model"
```

**Issue: Slow Model Loading**
```bash
# First-time model download is slow
```

**Solutions:**
```bash
# Pre-download model
python -c "
from transformers import AutoTokenizer
tokenizer = AutoTokenizer.from_pretrained('your-model-name')
print('Model cached successfully')
"

# Use smaller, faster models
# Instead of: sentence-transformers/all-mpnet-base-v2
# Use: sentence-transformers/all-MiniLM-L6-v2

# Check model size before downloading
curl -s "https://huggingface.co/api/models/your-model-name" | jq '.siblings[] | select(.rfilename | endswith(".bin")) | .size'
```

### Custom Model Issues

**Issue: Unsupported Model**
```bash
# Model not in supported list
```

**Solutions:**
```bash
# Use heuristic adapter for unknown models
export KANO_TOKENIZER_ADAPTER=heuristic
kob tokenizer test --model your-custom-model

# Add custom model to configuration
[tokenizer]
model = "your-custom-model"
max_tokens = 8192  # Set appropriate limit

[tokenizer.heuristic]
chars_per_token = 4.0  # Tune for your model's tokenization
```

## Environment Problems

### Docker/Container Issues

**Issue: Dependencies Not Available in Container**

**Solutions:**
```dockerfile
# In Dockerfile, install dependencies
RUN pip install tiktoken transformers

# Or use multi-stage build
FROM python:3.11-slim as builder
RUN pip install tiktoken transformers
FROM python:3.11-slim
COPY --from=builder /usr/local/lib/python3.11/site-packages /usr/local/lib/python3.11/site-packages
```

**Issue: Network Access in Container**
```bash
# Can't download HuggingFace models
```

**Solutions:**
```dockerfile
# Pre-download models during build
RUN python -c "from transformers import AutoTokenizer; AutoTokenizer.from_pretrained('bert-base-uncased')"

# Or mount cache directory
docker run -v ~/.cache/huggingface:/root/.cache/huggingface your-image
```

### CI/CD Issues

**Issue: Tests Fail in CI Environment**

**Solutions:**
```yaml
# In GitHub Actions
- name: Install dependencies
  run: |
    pip install tiktoken transformers
    
- name: Cache HuggingFace models
  uses: actions/cache@v3
  with:
    path: ~/.cache/huggingface
    key: huggingface-${{ hashFiles('requirements.txt') }}

- name: Test tokenizers
  run: |
    export KANO_TOKENIZER_ADAPTER=heuristic  # Fallback for CI
    kob tokenizer test
```

### Windows-Specific Issues

**Issue: Path Separators**
```bash
# Config file paths with backslashes
```

**Solutions:**
```bash
# Use forward slashes or raw strings
kob tokenizer validate --config "C:/path/to/config.toml"

# Or use environment variable
set KANO_TOKENIZER_CONFIG=C:\path\to\config.toml
kob tokenizer validate
```

**Issue: PowerShell Environment Variables**
```powershell
# Environment variables not persisting
```

**Solutions:**
```powershell
# Set environment variables in PowerShell
$env:KANO_TOKENIZER_ADAPTER = "heuristic"
kob tokenizer test

# Or use permanent setting
[Environment]::SetEnvironmentVariable("KANO_TOKENIZER_ADAPTER", "heuristic", "User")
```

## Advanced Troubleshooting

### Debug Mode

**Enable Verbose Logging:**
```bash
# Set log level
export PYTHONPATH=.
python -c "
import logging
logging.basicConfig(level=logging.DEBUG)
from kano_backlog_core.tokenizer import get_default_registry
registry = get_default_registry()
adapter = registry.resolve('tiktoken', model_name='gpt-4')
result = adapter.count_tokens('test')
print(result)
"
```

### Network Debugging

**Issue: Network-Related Failures**

**Diagnosis:**
```bash
# Test network connectivity
curl -I https://huggingface.co
curl -I https://pypi.org

# Check DNS resolution
nslookup huggingface.co
nslookup pypi.org

# Test with proxy
export HTTP_PROXY=http://proxy:port
export HTTPS_PROXY=http://proxy:port
kob tokenizer test --adapter huggingface
```

### Memory Debugging

**Issue: Memory Leaks or High Usage**

**Diagnosis:**
```bash
# Monitor memory usage
pip install memory-profiler

# Profile tokenization
python -c "
from memory_profiler import profile
from kano_backlog_core.tokenizer import get_default_registry

@profile
def test_tokenization():
    registry = get_default_registry()
    adapter = registry.resolve('huggingface', model_name='bert-base-uncased')
    for i in range(100):
        result = adapter.count_tokens(f'Test text {i}')
    return result

test_tokenization()
"
```

### Performance Profiling

**Issue: Need Detailed Performance Analysis**

**Tools:**
```bash
# Install profiling tools
pip install cProfile line_profiler

# Profile tokenization
python -m cProfile -o tokenizer_profile.prof -c "
from kano_backlog_core.tokenizer import get_default_registry
registry = get_default_registry()
adapter = registry.resolve('tiktoken', model_name='gpt-4')
for i in range(1000):
    adapter.count_tokens('Sample text for profiling')
"

# Analyze profile
python -c "
import pstats
stats = pstats.Stats('tokenizer_profile.prof')
stats.sort_stats('cumulative').print_stats(20)
"
```

### Recovery Procedures

**Complete Reset:**
```bash
# Clear all caches
rm -rf ~/.cache/huggingface/
rm -rf ~/.cache/pip/

# Reset environment
unset $(env | grep KANO_TOKENIZER | cut -d= -f1)

# Reinstall dependencies
pip uninstall tiktoken transformers -y
pip install tiktoken transformers

# Test basic functionality
kob tokenizer test --adapter heuristic
```

**Factory Reset Configuration:**
```bash
# Remove custom configuration
rm -f tokenizer_config.toml
rm -f ~/.config/kano/tokenizer.toml

# Create fresh configuration
kob tokenizer create-example --output tokenizer_config.toml --force

# Validate fresh configuration
kob tokenizer validate --config tokenizer_config.toml
```

### Getting Support

**Collect Diagnostic Information:**
```bash
# Create support bundle
mkdir tokenizer_debug
cd tokenizer_debug

# System information
kob tokenizer status --format json > system_status.json
kob tokenizer dependencies --verbose > dependencies.txt
kob tokenizer config --format json > config.json

# Test results
kob tokenizer test --verbose > test_results.txt 2>&1
kob tokenizer benchmark --format json > benchmark.json 2>&1

# Environment
env | grep -E "(KANO|PYTHON|PATH)" > environment.txt
python --version > python_version.txt
pip list > pip_packages.txt

# Create archive
cd ..
tar -czf tokenizer_debug.tar.gz tokenizer_debug/
echo "Debug information collected in tokenizer_debug.tar.gz"
```

**Common Support Questions:**
1. What operating system and Python version are you using?
2. What tokenizer adapter and model are you trying to use?
3. What is the exact error message?
4. Can you reproduce the issue with the heuristic adapter?
5. Have you tried the diagnostic commands?

---

This troubleshooting guide covers the most common issues encountered with tokenizer adapters. For additional help, run the diagnostic commands and collect the information as shown in the "Getting Support" section.
