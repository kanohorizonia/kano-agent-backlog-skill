# Tokenizer Adapters Performance Tuning Guide

**Optimize tokenizer adapter performance for your specific use case**

This guide provides comprehensive performance tuning recommendations, benchmarking strategies, and optimization techniques for tokenizer adapters.

## Table of Contents

- [Performance Overview](#performance-overview)
- [Benchmarking Your Setup](#benchmarking-your-setup)
- [Adapter Selection for Performance](#adapter-selection-for-performance)
- [Configuration Optimization](#configuration-optimization)
- [System-Level Optimization](#system-level-optimization)
- [Use Case Specific Tuning](#use-case-specific-tuning)
- [Monitoring and Profiling](#monitoring-and-profiling)
- [Production Recommendations](#production-recommendations)

## Performance Overview

### Adapter Performance Characteristics

| Adapter | Speed | Memory | Accuracy | Startup | Best For |
|---------|-------|--------|----------|---------|----------|
| **Heuristic** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ | Development, high-throughput |
| **TikToken** | ⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | OpenAI models, balanced |
| **HuggingFace** | ⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐ | HF models, accuracy-critical |

### Performance Targets

**Typical Performance Expectations:**

| Text Size | Heuristic | TikToken | HuggingFace |
|-----------|-----------|----------|-------------|
| 1KB | <1ms | <5ms | <20ms |
| 10KB | <5ms | <20ms | <100ms |
| 100KB | <20ms | <100ms | <500ms |
| 1MB | <100ms | <500ms | <2s |

**Memory Usage:**

| Adapter | Base Memory | Per-Text Overhead |
|---------|-------------|-------------------|
| Heuristic | ~1MB | Minimal |
| TikToken | ~10MB | Low |
| HuggingFace | ~100MB-1GB | Medium |

## Benchmarking Your Setup

### Basic Performance Testing

**Quick Performance Check:**
```bash
# Test current setup
kob tokenizer benchmark

# Test with your typical content
kob tokenizer benchmark --text "$(cat typical_document.txt)"

# Compare adapters
kob tokenizer benchmark --adapters heuristic,tiktoken,huggingface
```

**Detailed Benchmarking:**
```bash
# Comprehensive benchmark with multiple iterations
kob tokenizer benchmark \
  --text "$(cat sample_document.txt)" \
  --iterations 50 \
  --format json > benchmark_results.json

# Analyze results
cat benchmark_results.json | jq '.results[] | {adapter, avg_time_ms, avg_tokens, consistent}'
```

### Custom Benchmarking Scripts

**Text Size Performance:**
```bash
#!/bin/bash
# benchmark_text_sizes.sh

echo "Benchmarking different text sizes..."

for size in 1000 5000 10000 50000 100000; do
    echo "Testing ${size} characters:"
    head -c $size large_document.txt > test_${size}.txt
    
    kob tokenizer benchmark \
      --text "$(cat test_${size}.txt)" \
      --iterations 10 \
      --format json | jq -r '.results[] | "\(.adapter): \(.avg_time_ms)ms"'
    
    rm test_${size}.txt
    echo
done
```

**Adapter Comparison:**
```bash
#!/bin/bash
# compare_adapters.sh

adapters=("heuristic" "tiktoken" "huggingface")
test_text="Your typical document content here. This should represent the kind of text you process most often in your application."

echo "Adapter Performance Comparison"
echo "=============================="

for adapter in "${adapters[@]}"; do
    echo "Testing $adapter:"
    
    # Check if adapter is available
    if kob tokenizer health-check $adapter >/dev/null 2>&1; then
        result=$(kob tokenizer benchmark \
          --adapters $adapter \
          --text "$test_text" \
          --iterations 20 \
          --format json)
        
        avg_time=$(echo $result | jq -r '.results[0].avg_time_ms')
        tokens=$(echo $result | jq -r '.results[0].avg_tokens')
        consistent=$(echo $result | jq -r '.results[0].consistent')
        
        echo "  Average time: ${avg_time}ms"
        echo "  Token count: $tokens"
        echo "  Consistent: $consistent"
    else
        echo "  Not available"
    fi
    echo
done
```

### Memory Profiling

**Monitor Memory Usage:**
```bash
# Install memory profiler
pip install memory-profiler psutil

# Create memory profiling script
cat > profile_memory.py << 'EOF'
from memory_profiler import profile
from kano_backlog_core.tokenizer import get_default_registry
import psutil
import os

@profile
def test_tokenizer_memory(adapter_name, model_name, text):
    """Profile memory usage of tokenizer adapter."""
    process = psutil.Process(os.getpid())
    initial_memory = process.memory_info().rss / 1024 / 1024  # MB
    
    registry = get_default_registry()
    adapter = registry.resolve(adapter_name, model_name=model_name)
    
    after_init_memory = process.memory_info().rss / 1024 / 1024  # MB
    
    # Tokenize multiple times
    for i in range(100):
        result = adapter.count_tokens(text)
    
    final_memory = process.memory_info().rss / 1024 / 1024  # MB
    
    print(f"Initial memory: {initial_memory:.1f}MB")
    print(f"After init: {after_init_memory:.1f}MB")
    print(f"Final memory: {final_memory:.1f}MB")
    print(f"Init overhead: {after_init_memory - initial_memory:.1f}MB")
    print(f"Processing overhead: {final_memory - after_init_memory:.1f}MB")

if __name__ == "__main__":
    test_text = "Your test text here" * 100  # Repeat for larger text
    test_tokenizer_memory("tiktoken", "gpt-4", test_text)
EOF

# Run memory profiling
python profile_memory.py
```

## Adapter Selection for Performance

### Speed-Optimized Configuration

**Maximum Speed (Development/Testing):**
```toml
[tokenizer]
adapter = "heuristic"
fallback_chain = ["heuristic"]  # Skip slower adapters

[tokenizer.heuristic]
chars_per_token = 4.0  # Tune for your content
```

**Balanced Speed/Accuracy:**
```toml
[tokenizer]
adapter = "tiktoken"
fallback_chain = ["tiktoken", "heuristic"]  # Fast fallback

[tokenizer.tiktoken]
# Let encoding be auto-detected for speed
```

**Accuracy-First (Production):**
```toml
[tokenizer]
adapter = "auto"
fallback_chain = ["tiktoken", "huggingface", "heuristic"]

[tokenizer.huggingface]
use_fast = true  # Enable fast tokenizers
```

### Adapter Selection Decision Tree

```
Start
  ↓
Need exact tokenization?
  ├─ No → Use Heuristic (fastest)
  └─ Yes
      ↓
      OpenAI model?
      ├─ Yes → Use TikToken
      └─ No
          ↓
          HuggingFace model?
          ├─ Yes → Use HuggingFace (with use_fast=true)
          └─ No → Use Heuristic with tuned chars_per_token
```

### Performance Testing Script

```python
#!/usr/bin/env python3
"""
Performance testing script for tokenizer adapters.
"""

import time
import statistics
from kano_backlog_core.tokenizer import get_default_registry

def benchmark_adapter(adapter_name, model_name, text, iterations=10):
    """Benchmark a specific adapter."""
    registry = get_default_registry()
    
    try:
        adapter = registry.resolve(adapter_name, model_name=model_name)
        
        # Warmup
        adapter.count_tokens("warmup")
        
        # Benchmark
        times = []
        token_counts = []
        
        for _ in range(iterations):
            start = time.perf_counter()
            result = adapter.count_tokens(text)
            end = time.perf_counter()
            
            times.append((end - start) * 1000)  # Convert to ms
            token_counts.append(result.count)
        
        return {
            'adapter': adapter_name,
            'avg_time_ms': statistics.mean(times),
            'median_time_ms': statistics.median(times),
            'min_time_ms': min(times),
            'max_time_ms': max(times),
            'std_time_ms': statistics.stdev(times) if len(times) > 1 else 0,
            'avg_tokens': statistics.mean(token_counts),
            'consistent': len(set(token_counts)) == 1,
            'is_exact': result.is_exact,
            'success': True
        }
    except Exception as e:
        return {
            'adapter': adapter_name,
            'success': False,
            'error': str(e)
        }

def main():
    # Test configuration
    test_text = "Your typical document content here. " * 50  # Adjust size
    model_name = "gpt-4"  # Adjust for your use case
    adapters = ["heuristic", "tiktoken", "huggingface"]
    iterations = 20
    
    print(f"Benchmarking tokenizer adapters")
    print(f"Text length: {len(test_text)} characters")
    print(f"Iterations: {iterations}")
    print(f"Model: {model_name}")
    print("-" * 60)
    
    results = []
    for adapter in adapters:
        print(f"Testing {adapter}...")
        result = benchmark_adapter(adapter, model_name, test_text, iterations)
        results.append(result)
        
        if result['success']:
            print(f"  Average: {result['avg_time_ms']:.2f}ms")
            print(f"  Range: {result['min_time_ms']:.2f}-{result['max_time_ms']:.2f}ms")
            print(f"  Tokens: {result['avg_tokens']:.0f}")
            print(f"  Exact: {result['is_exact']}")
            print(f"  Consistent: {result['consistent']}")
        else:
            print(f"  Failed: {result['error']}")
        print()
    
    # Performance ranking
    successful = [r for r in results if r['success']]
    if successful:
        print("Performance Ranking (by speed):")
        by_speed = sorted(successful, key=lambda x: x['avg_time_ms'])
        for i, result in enumerate(by_speed, 1):
            print(f"{i}. {result['adapter']}: {result['avg_time_ms']:.2f}ms")

if __name__ == "__main__":
    main()
```

## Configuration Optimization

### Heuristic Adapter Tuning

**Optimize chars_per_token for Your Content:**

```bash
# Test different ratios with your content
test_text="$(cat your_typical_document.txt)"

echo "Testing chars_per_token ratios:"
for ratio in 3.0 3.5 4.0 4.5 5.0; do
    export KANO_TOKENIZER_HEURISTIC_CHARS_PER_TOKEN=$ratio
    
    # Compare with exact adapter
    result=$(kob tokenizer benchmark --text "$test_text" --adapters heuristic,tiktoken --format json 2>/dev/null)
    
    if [ $? -eq 0 ]; then
        heuristic_tokens=$(echo $result | jq -r '.results[] | select(.adapter=="heuristic") | .count')
        tiktoken_tokens=$(echo $result | jq -r '.results[] | select(.adapter=="tiktoken") | .count')
        
        if [ "$tiktoken_tokens" != "null" ] && [ "$heuristic_tokens" != "null" ]; then
            accuracy=$(echo "scale=2; 100 - (($heuristic_tokens - $tiktoken_tokens) / $tiktoken_tokens * 100)" | bc -l 2>/dev/null || echo "N/A")
            echo "  Ratio $ratio: $heuristic_tokens tokens (accuracy: ${accuracy}%)"
        fi
    fi
done
```

**Language-Specific Tuning:**

```toml
# For English technical documentation
[tokenizer.heuristic]
chars_per_token = 4.2

# For code-heavy content
[tokenizer.heuristic]
chars_per_token = 3.8

# For academic papers (lots of punctuation)
[tokenizer.heuristic]
chars_per_token = 4.5

# For CJK-heavy content
[tokenizer.heuristic]
chars_per_token = 2.0  # CJK characters are typically 1 token each
```

### TikToken Optimization

**Encoding Selection:**
```bash
# Test different encodings for your model
encodings=("cl100k_base" "p50k_base")

for encoding in "${encodings[@]}"; do
    export KANO_TOKENIZER_TIKTOKEN_ENCODING=$encoding
    echo "Testing encoding: $encoding"
    
    time kob tokenizer test --adapter tiktoken --text "$(cat test_document.txt)"
done

# Let system auto-detect (usually optimal)
unset KANO_TOKENIZER_TIKTOKEN_ENCODING
```

### HuggingFace Optimization

**Fast Tokenizer Configuration:**
```toml
[tokenizer.huggingface]
use_fast = true  # Significant speed improvement
trust_remote_code = false  # Security best practice
```

**Model Selection for Performance:**
```bash
# Compare model performance
models=(
    "distilbert-base-uncased"  # Smaller, faster
    "bert-base-uncased"        # Standard
    "bert-large-uncased"       # Larger, slower
)

for model in "${models[@]}"; do
    echo "Testing model: $model"
    kob tokenizer benchmark \
      --adapters huggingface \
      --model "$model" \
      --iterations 5
done
```

## System-Level Optimization

### Python Environment Optimization

**Python Version:**
```bash
# Use Python 3.11+ for better performance
python --version

# If using older Python, consider upgrading
pyenv install 3.11.0
pyenv local 3.11.0
```

**Package Optimization:**
```bash
# Keep packages updated
pip install --upgrade tiktoken transformers

# Use optimized builds if available
pip install --upgrade --force-reinstall tiktoken

# For HuggingFace, consider torch optimizations
pip install torch --index-url https://download.pytorch.org/whl/cpu  # CPU-only
```

### Memory Optimization

**HuggingFace Cache Management:**
```bash
# Set cache location to fast storage
export HF_HOME=/path/to/fast/ssd/cache

# Limit cache size
export HF_DATASETS_CACHE=/path/to/cache
export TRANSFORMERS_CACHE=/path/to/cache

# Pre-download models to avoid runtime delays
python -c "
from transformers import AutoTokenizer
models = ['bert-base-uncased', 'distilbert-base-uncased']
for model in models:
    print(f'Downloading {model}...')
    AutoTokenizer.from_pretrained(model)
print('All models cached')
"
```

**Memory Limits:**
```python
# Monitor and limit memory usage
import resource
import psutil

def set_memory_limit(max_memory_mb):
    """Set maximum memory usage."""
    max_memory_bytes = max_memory_mb * 1024 * 1024
    resource.setrlimit(resource.RLIMIT_AS, (max_memory_bytes, max_memory_bytes))

def check_memory_usage():
    """Check current memory usage."""
    process = psutil.Process()
    memory_mb = process.memory_info().rss / 1024 / 1024
    return memory_mb

# Set 1GB limit
set_memory_limit(1024)
```

### Disk I/O Optimization

**SSD Configuration:**
```bash
# Use SSD for caches
export HF_HOME=/mnt/ssd/huggingface_cache
export PIP_CACHE_DIR=/mnt/ssd/pip_cache

# Pre-warm caches
kob tokenizer test --adapter huggingface --model bert-base-uncased
```

**Network Optimization:**
```bash
# Use CDN/mirror if available
export HF_ENDPOINT=https://hf-mirror.com

# Parallel downloads
export HF_HUB_ENABLE_HF_TRANSFER=1
pip install hf-transfer
```

## Use Case Specific Tuning

### High-Throughput Processing

**Configuration for Maximum Throughput:**
```toml
[tokenizer]
adapter = "heuristic"
fallback_chain = ["heuristic"]

[tokenizer.heuristic]
chars_per_token = 4.0  # Tune based on accuracy requirements
```

**Batch Processing Optimization:**
```python
def process_documents_batch(documents, adapter):
    """Process multiple documents efficiently."""
    # Pre-warm adapter
    adapter.count_tokens("warmup")
    
    results = []
    for doc in documents:
        # Process without re-initializing adapter
        result = adapter.count_tokens(doc)
        results.append(result)
    
    return results

# Usage
from kano_backlog_core.tokenizer import get_default_registry

registry = get_default_registry()
adapter = registry.resolve("heuristic", model_name="default")

# Process batch
documents = ["doc1", "doc2", "doc3"]  # Your documents
results = process_documents_batch(documents, adapter)
```

### Real-Time Processing

**Low-Latency Configuration:**
```toml
[tokenizer]
adapter = "heuristic"
fallback_chain = ["heuristic"]

[tokenizer.heuristic]
chars_per_token = 4.0
```

**Pre-Initialization:**
```python
# Initialize adapters at startup
class TokenizerService:
    def __init__(self):
        from kano_backlog_core.tokenizer import get_default_registry
        self.registry = get_default_registry()
        
        # Pre-initialize common adapters
        self.heuristic = self.registry.resolve("heuristic", model_name="default")
        self.tiktoken = None
        
        try:
            self.tiktoken = self.registry.resolve("tiktoken", model_name="gpt-4")
        except:
            pass  # TikToken not available
    
    def count_tokens(self, text, prefer_exact=False):
        """Count tokens with pre-initialized adapters."""
        if prefer_exact and self.tiktoken:
            return self.tiktoken.count_tokens(text)
        else:
            return self.heuristic.count_tokens(text)

# Initialize once at startup
tokenizer_service = TokenizerService()
```

### Accuracy-Critical Applications

**Configuration for Maximum Accuracy:**
```toml
[tokenizer]
adapter = "auto"
fallback_chain = ["tiktoken", "huggingface", "heuristic"]

[tokenizer.huggingface]
use_fast = true
trust_remote_code = false
```

**Accuracy Validation:**
```python
def validate_tokenization_accuracy(text, model_name):
    """Validate tokenization accuracy across adapters."""
    from kano_backlog_core.tokenizer import get_default_registry
    
    registry = get_default_registry()
    results = {}
    
    # Test available adapters
    for adapter_name in ["tiktoken", "huggingface", "heuristic"]:
        try:
            adapter = registry.resolve(adapter_name, model_name=model_name)
            result = adapter.count_tokens(text)
            results[adapter_name] = {
                'count': result.count,
                'is_exact': result.is_exact,
                'method': result.method
            }
        except:
            continue
    
    # Compare results
    exact_results = {k: v for k, v in results.items() if v['is_exact']}
    if len(exact_results) > 1:
        counts = [v['count'] for v in exact_results.values()]
        if len(set(counts)) > 1:
            print(f"Warning: Exact adapters disagree on token count: {exact_results}")
    
    return results
```

### Resource-Constrained Environments

**Minimal Resource Configuration:**
```toml
[tokenizer]
adapter = "heuristic"
fallback_chain = ["heuristic"]

[tokenizer.heuristic]
chars_per_token = 4.0
```

**Memory-Efficient Processing:**
```python
def process_large_document_chunked(file_path, chunk_size=10000):
    """Process large documents in chunks to limit memory usage."""
    from kano_backlog_core.tokenizer import get_default_registry
    
    registry = get_default_registry()
    adapter = registry.resolve("heuristic", model_name="default")
    
    total_tokens = 0
    with open(file_path, 'r') as f:
        while True:
            chunk = f.read(chunk_size)
            if not chunk:
                break
            
            result = adapter.count_tokens(chunk)
            total_tokens += result.count
    
    return total_tokens
```

## Monitoring and Profiling

### Performance Monitoring

**Built-in Metrics:**
```bash
# Regular performance checks
kob tokenizer benchmark --format json > daily_benchmark.json

# Monitor over time
echo "$(date): $(kob tokenizer benchmark --format json | jq '.results[0].avg_time_ms')" >> performance_log.txt
```

**Custom Monitoring:**
```python
import time
import json
from datetime import datetime
from kano_backlog_core.tokenizer import get_default_registry

class TokenizerMonitor:
    def __init__(self):
        self.metrics = []
    
    def time_tokenization(self, adapter_name, model_name, text):
        """Time a tokenization operation and record metrics."""
        registry = get_default_registry()
        adapter = registry.resolve(adapter_name, model_name=model_name)
        
        start_time = time.perf_counter()
        result = adapter.count_tokens(text)
        end_time = time.perf_counter()
        
        metric = {
            'timestamp': datetime.now().isoformat(),
            'adapter': adapter_name,
            'model': model_name,
            'text_length': len(text),
            'token_count': result.count,
            'duration_ms': (end_time - start_time) * 1000,
            'is_exact': result.is_exact
        }
        
        self.metrics.append(metric)
        return result
    
    def get_performance_summary(self):
        """Get performance summary statistics."""
        if not self.metrics:
            return {}
        
        by_adapter = {}
        for metric in self.metrics:
            adapter = metric['adapter']
            if adapter not in by_adapter:
                by_adapter[adapter] = []
            by_adapter[adapter].append(metric['duration_ms'])
        
        summary = {}
        for adapter, times in by_adapter.items():
            summary[adapter] = {
                'count': len(times),
                'avg_time_ms': sum(times) / len(times),
                'min_time_ms': min(times),
                'max_time_ms': max(times)
            }
        
        return summary
    
    def save_metrics(self, filename):
        """Save metrics to file."""
        with open(filename, 'w') as f:
            json.dump(self.metrics, f, indent=2)

# Usage
monitor = TokenizerMonitor()
monitor.time_tokenization("tiktoken", "gpt-4", "Sample text")
print(monitor.get_performance_summary())
```

### Profiling Tools

**CPU Profiling:**
```bash
# Install profiling tools
pip install cProfile line_profiler py-spy

# Profile tokenization
python -m cProfile -o tokenizer.prof -c "
from kano_backlog_core.tokenizer import get_default_registry
registry = get_default_registry()
adapter = registry.resolve('tiktoken', model_name='gpt-4')
for i in range(1000):
    adapter.count_tokens('Sample text for profiling')
"

# Analyze profile
python -c "
import pstats
stats = pstats.Stats('tokenizer.prof')
stats.sort_stats('cumulative').print_stats(20)
"
```

**Memory Profiling:**
```bash
# Install memory profiler
pip install memory-profiler

# Profile memory usage
python -m memory_profiler profile_memory.py
```

**Real-time Profiling:**
```bash
# Install py-spy for real-time profiling
pip install py-spy

# Profile running process
kob tokenizer benchmark --iterations 1000 &
PID=$!
py-spy record -o profile.svg -p $PID
```

## Production Recommendations

### Production Configuration Template

```toml
# production_tokenizer_config.toml
[tokenizer]
# Use auto for intelligent fallback
adapter = "auto"

# Set production model
model = "text-embedding-3-small"  # Adjust for your use case

# Comprehensive fallback chain
fallback_chain = ["tiktoken", "huggingface", "heuristic"]

# Production-tuned heuristic settings
[tokenizer.heuristic]
chars_per_token = 4.0  # Tune based on your content analysis

# TikToken settings
[tokenizer.tiktoken]
# Let encoding be auto-detected for reliability

# HuggingFace settings
[tokenizer.huggingface]
use_fast = true           # Enable fast tokenizers
trust_remote_code = false # Security best practice
```

### Deployment Checklist

**Pre-Deployment Testing:**
```bash
# 1. Validate configuration
kob tokenizer validate --config production_config.toml

# 2. Test all adapters
kob tokenizer test --config production_config.toml

# 3. Benchmark performance
kob tokenizer benchmark --config production_config.toml --iterations 50

# 4. Check dependencies
kob tokenizer dependencies --verbose

# 5. Test with production-like data
kob tokenizer benchmark --text "$(cat production_sample.txt)" --iterations 20
```

**Production Monitoring:**
```bash
# Health check script
#!/bin/bash
# health_check.sh

echo "Tokenizer Health Check - $(date)"
echo "================================"

# System status
kob tokenizer status --format json > /tmp/tokenizer_status.json

# Check if any adapters failed
failed_adapters=$(cat /tmp/tokenizer_status.json | jq -r '.adapters | to_entries[] | select(.value.available == false) | .key')

if [ -n "$failed_adapters" ]; then
    echo "WARNING: Failed adapters: $failed_adapters"
    exit 1
else
    echo "All adapters healthy"
fi

# Performance check
avg_time=$(kob tokenizer benchmark --iterations 5 --format json | jq -r '.results[0].avg_time_ms')
if (( $(echo "$avg_time > 100" | bc -l) )); then
    echo "WARNING: Performance degraded (${avg_time}ms)"
    exit 1
else
    echo "Performance OK (${avg_time}ms)"
fi

echo "Health check passed"
```

### Scaling Considerations

**Horizontal Scaling:**
```python
# Multi-process tokenization
from multiprocessing import Pool
from kano_backlog_core.tokenizer import get_default_registry

def tokenize_document(doc_text):
    """Tokenize a single document (for multiprocessing)."""
    registry = get_default_registry()
    adapter = registry.resolve("tiktoken", model_name="gpt-4")
    return adapter.count_tokens(doc_text)

def process_documents_parallel(documents, num_processes=4):
    """Process documents in parallel."""
    with Pool(num_processes) as pool:
        results = pool.map(tokenize_document, documents)
    return results

# Usage
documents = ["doc1", "doc2", "doc3"]  # Your documents
results = process_documents_parallel(documents)
```

**Load Balancing:**
```python
# Round-robin adapter selection for load balancing
class LoadBalancedTokenizer:
    def __init__(self):
        from kano_backlog_core.tokenizer import get_default_registry
        self.registry = get_default_registry()
        self.adapters = ["tiktoken", "heuristic"]  # Available adapters
        self.current = 0
    
    def count_tokens(self, text, model_name="gpt-4"):
        """Count tokens with load balancing."""
        adapter_name = self.adapters[self.current]
        self.current = (self.current + 1) % len(self.adapters)
        
        try:
            adapter = self.registry.resolve(adapter_name, model_name=model_name)
            return adapter.count_tokens(text)
        except:
            # Fallback to heuristic
            adapter = self.registry.resolve("heuristic", model_name=model_name)
            return adapter.count_tokens(text)
```

### Performance Alerting

**Performance Degradation Detection:**
```python
import json
import time
from datetime import datetime, timedelta

class PerformanceAlert:
    def __init__(self, baseline_ms=50, threshold_factor=2.0):
        self.baseline_ms = baseline_ms
        self.threshold_factor = threshold_factor
        self.recent_times = []
    
    def record_time(self, duration_ms):
        """Record a tokenization time."""
        self.recent_times.append({
            'time': datetime.now(),
            'duration_ms': duration_ms
        })
        
        # Keep only recent times (last hour)
        cutoff = datetime.now() - timedelta(hours=1)
        self.recent_times = [t for t in self.recent_times if t['time'] > cutoff]
        
        # Check for performance degradation
        if len(self.recent_times) >= 10:
            recent_avg = sum(t['duration_ms'] for t in self.recent_times[-10:]) / 10
            if recent_avg > self.baseline_ms * self.threshold_factor:
                self.alert_performance_degradation(recent_avg)
    
    def alert_performance_degradation(self, current_avg):
        """Alert on performance degradation."""
        print(f"ALERT: Performance degraded!")
        print(f"Baseline: {self.baseline_ms}ms")
        print(f"Current average: {current_avg:.1f}ms")
        print(f"Degradation factor: {current_avg / self.baseline_ms:.1f}x")
        
        # In production, send to monitoring system
        # send_alert_to_monitoring_system(...)

# Usage
alert_system = PerformanceAlert(baseline_ms=20, threshold_factor=3.0)

# Record times during operation
start = time.perf_counter()
# ... tokenization operation ...
end = time.perf_counter()
alert_system.record_time((end - start) * 1000)
```

---

This performance tuning guide provides comprehensive strategies for optimizing tokenizer adapter performance across different use cases and environments. Regular benchmarking and monitoring will help maintain optimal performance in production systems.
