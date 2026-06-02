"""Comprehensive performance benchmarks for tokenizer adapters and chunking pipeline.

This module implements task 4.4 requirements for performance benchmarks and tests:
- Tokenization performance across different text sizes (1KB, 10KB, 100KB)
- Comparison benchmarks between different tokenizer adapters
- Chunking pipeline performance end-to-end
- Memory usage profiling for large documents
- Performance target validation
- Performance regression detection
- Performance reports and metrics generation

Performance Targets (from design):
- Tokenization: < 100ms for 10KB documents
- Chunking: < 500ms for 100KB documents  
- Memory usage: Linear scaling with document size
- Consistent performance across text types
"""

import os
import time
import gc
import json
import statistics
from typing import List, Dict, Any, Optional, Tuple, Type, Iterator
from pathlib import Path
from dataclasses import dataclass, asdict
from contextlib import contextmanager

import pytest

try:
    psutil: Any
    import importlib
    psutil = importlib.import_module("psutil")
    PSUTIL_AVAILABLE = True
except ImportError:
    PSUTIL_AVAILABLE = False
    psutil = None

from kano_backlog_core.tokenizer import (
    TokenizerRegistry,
    HeuristicTokenizer,
    TokenCount,
    resolve_model_max_tokens,
)
from kano_backlog_core.chunking import (
    ChunkingOptions,
    Chunk,
    chunk_text_with_tokenizer,
    chunk_text,
    normalize_text,
)
from kano_backlog_core.token_budget import (
    TokenBudgetManager,
    budget_chunks,
    TokenBudgetPolicy,
)

RUN_PERF_TESTS = os.environ.get("KANO_RUN_PERF_TESTS", "").lower() in ("1", "true", "yes", "on")
pytestmark = pytest.mark.skipif(
    not RUN_PERF_TESTS,
    reason="Performance benchmarks are opt-in; set KANO_RUN_PERF_TESTS=1 to run.",
)


@dataclass
class PerformanceMetrics:
    """Performance metrics for a single benchmark run."""
    operation: str
    adapter_type: str
    text_size_kb: float
    text_size_chars: int
    processing_time_ms: float
    memory_used_mb: float
    tokens_processed: int
    chunks_produced: int
    throughput_chars_per_sec: float
    throughput_tokens_per_sec: float
    target_met: bool
    target_threshold_ms: float
    error: Optional[str] = None


@dataclass
class BenchmarkReport:
    """Complete benchmark report with all metrics and analysis."""
    timestamp: str
    python_version: str
    system_info: Dict[str, Any]
    performance_targets: Dict[str, float]
    tokenization_benchmarks: List[PerformanceMetrics]
    chunking_benchmarks: List[PerformanceMetrics]
    memory_benchmarks: List[PerformanceMetrics]
    adapter_comparison: Dict[str, Dict[str, float]]
    regression_analysis: Dict[str, Any]
    summary: Dict[str, Any]


RegressionRecord = Dict[str, object]
RegressionSummary = Dict[str, object]
RegressionAnalysis = Dict[str, object]
AdapterConfig = Tuple[str, Type[Any], Dict[str, float]]


class PerformanceBenchmarkSuite:
    """Comprehensive performance benchmark suite for tokenizer adapters."""
    
    def __init__(self) -> None:
        self.registry = TokenizerRegistry()
        self.performance_targets = {
            "tokenization_10kb_ms": 100.0,  # < 100ms for 10KB documents
            "chunking_100kb_ms": 500.0,     # < 500ms for 100KB documents
            "memory_scaling_factor": 2.0,    # Linear scaling tolerance
        }
        self.test_documents = self._generate_test_documents()
        
    def _generate_test_documents(self) -> Dict[str, str]:
        """Generate test documents of various sizes and types."""
        documents = {}
        
        # Base content patterns
        english_text = """
        The tokenizer adapter system provides a flexible architecture for accurate token counting
        across different model providers. This implementation supports OpenAI's tiktoken library
        for GPT models, HuggingFace transformers for BERT and other models, and a heuristic
        fallback for environments where external dependencies are not available.
        
        The chunking engine integrates with tokenizer adapters to provide token-aware document
        segmentation with deterministic boundary selection. The system uses a hierarchical
        approach: paragraph boundaries are preferred, followed by sentence boundaries, with
        hard cuts as a last resort when no natural boundaries are available.
        
        Performance optimization is achieved through efficient algorithms for text normalization,
        boundary detection, and token budget management. The implementation maintains linear
        memory scaling and provides consistent performance across different text types and sizes.
        """
        
        cjk_text = """
        トークナイザーアダプターシステムは、異なるモデルプロバイダー間で正確なトークンカウントを
        提供する柔軟なアーキテクチャを提供します。この実装は、GPTモデル用のOpenAIのtiktokenライブラリ、
        BERTおよびその他のモデル用のHuggingFace transformers、および外部依存関係が利用できない
        環境用のヒューリスティックフォールバックをサポートしています。
        
        チャンキングエンジンは、トークナイザーアダプターと統合して、決定論的境界選択による
        トークン対応ドキュメントセグメンテーションを提供します。システムは階層的アプローチを使用します：
        段落境界が優先され、次に文境界、自然な境界が利用できない場合の最後の手段としてハードカットが使用されます。
        
        パフォーマンスの最適化は、テキスト正規化、境界検出、およびトークン予算管理の効率的な
        アルゴリズムによって達成されます。実装は線形メモリスケーリングを維持し、異なるテキストタイプと
        サイズ間で一貫したパフォーマンスを提供します。
        """
        
        mixed_content = f"""
        # Performance Benchmark Document
        
        ## English Section
        {english_text}
        
        ## 日本語セクション  
        {cjk_text}
        
        ## Technical Specifications
        
        - **Tokenization Target**: < 100ms for 10KB documents
        - **Chunking Target**: < 500ms for 100KB documents  
        - **Memory Scaling**: Linear with document size
        - **Supported Adapters**: heuristic, tiktoken, huggingface
        
        ### Code Examples
        
        ```python
        from kano_backlog_core.tokenizer import TokenizerRegistry
        
        registry = TokenizerRegistry()
        adapter = registry.resolve("heuristic", "test-model")
        token_count = adapter.count_tokens("Hello, world!")
        ```
        
        ### Mathematical Formulas
        
        Token estimation: tokens ≈ characters / chars_per_token
        Memory usage: O(n) where n = document_size
        Processing time: O(n * log(boundaries))
        
        ### Unicode Test Cases
        
        Emoji: 🚀 💻 🎉 ⭐ 🌈
        Math: ∑ ∫ ∞ ≈ ≠ ≤ ≥ ± √
        Currency: $ € £ ¥ ₹ ₽ ₿
        Greek: α β γ δ ε ζ η θ
        """
        
        # Generate documents of different sizes
        documents["1kb_english"] = (english_text * 3)[:1024]
        documents["1kb_cjk"] = (cjk_text * 2)[:1024] 
        documents["1kb_mixed"] = mixed_content[:1024]
        
        documents["10kb_english"] = (english_text * 30)[:10240]
        documents["10kb_cjk"] = (cjk_text * 15)[:10240]
        documents["10kb_mixed"] = (mixed_content * 5)[:10240]
        
        documents["100kb_english"] = (english_text * 300)[:102400]
        documents["100kb_cjk"] = (cjk_text * 150)[:102400]
        documents["100kb_mixed"] = (mixed_content * 50)[:102400]
        
        return documents
    
    @contextmanager
    def _measure_performance(self) -> Iterator[None]:
        """Context manager for measuring performance metrics."""
        process = None
        if PSUTIL_AVAILABLE and psutil is not None:
            import os
            process = psutil.Process(os.getpid())
            memory_before = process.memory_info().rss / 1024 / 1024  # MB
        else:
            memory_before = 0
            
        gc.collect()  # Clean up before measurement
        start_time = time.perf_counter()
        
        try:
            yield
        finally:
            end_time = time.perf_counter()
            
            if process is not None:
                memory_after = process.memory_info().rss / 1024 / 1024  # MB
                memory_used = memory_after - memory_before
            else:
                memory_used = 0
                
            self._last_performance = {
                "processing_time_ms": (end_time - start_time) * 1000,
                "memory_used_mb": memory_used
            }
    
    def benchmark_tokenization_performance(self) -> List[PerformanceMetrics]:
        """Benchmark tokenization performance across different text sizes and adapters."""
        results = []
        
        # Test different adapter configurations
        adapter_configs: List[AdapterConfig] = [
            ("heuristic_3.0", HeuristicTokenizer, {"chars_per_token": 3.0}),
            ("heuristic_4.0", HeuristicTokenizer, {"chars_per_token": 4.0}),
            ("heuristic_5.0", HeuristicTokenizer, {"chars_per_token": 5.0}),
        ]
        
        # Add tiktoken and huggingface if available
        try:
            from kano_backlog_core.tokenizer import TiktokenAdapter
            adapter_configs.append(("tiktoken", TiktokenAdapter, {}))
        except ImportError:
            pass
            
        try:
            from kano_backlog_core.tokenizer import HuggingFaceAdapter
            adapter_configs.append(("huggingface", HuggingFaceAdapter, {}))
        except ImportError:
            pass
        
        for doc_name, text in self.test_documents.items():
            text_size_kb = len(text) / 1024
            
            for adapter_name, adapter_class, kwargs in adapter_configs:
                try:
                    adapter = adapter_class("benchmark-model", **kwargs)
                    
                    # Warm up
                    adapter.count_tokens("warmup")
                    
                    # Benchmark tokenization
                    with self._measure_performance():
                        token_count = adapter.count_tokens(text)
                    
                    perf = self._last_performance
                    
                    # Calculate throughput
                    throughput_chars = len(text) / (perf["processing_time_ms"] / 1000) if perf["processing_time_ms"] > 0 else 0
                    throughput_tokens = token_count.count / (perf["processing_time_ms"] / 1000) if perf["processing_time_ms"] > 0 else 0
                    
                    # Check if target is met (10KB documents should be < 100ms)
                    target_threshold = self.performance_targets["tokenization_10kb_ms"]
                    scaled_target = target_threshold
                    if text_size_kb >= 10:
                        target_met = perf["processing_time_ms"] <= target_threshold
                    else:
                        # Scale target proportionally for smaller documents
                        scaled_target = target_threshold * (text_size_kb / 10)
                        target_met = perf["processing_time_ms"] <= scaled_target
                    
                    results.append(PerformanceMetrics(
                        operation="tokenization",
                        adapter_type=adapter_name,
                        text_size_kb=text_size_kb,
                        text_size_chars=len(text),
                        processing_time_ms=perf["processing_time_ms"],
                        memory_used_mb=perf["memory_used_mb"],
                        tokens_processed=token_count.count,
                        chunks_produced=0,
                        throughput_chars_per_sec=throughput_chars,
                        throughput_tokens_per_sec=throughput_tokens,
                        target_met=target_met,
                        target_threshold_ms=target_threshold if text_size_kb >= 10 else scaled_target
                    ))
                    
                except Exception as e:
                    results.append(PerformanceMetrics(
                        operation="tokenization",
                        adapter_type=adapter_name,
                        text_size_kb=text_size_kb,
                        text_size_chars=len(text),
                        processing_time_ms=0,
                        memory_used_mb=0,
                        tokens_processed=0,
                        chunks_produced=0,
                        throughput_chars_per_sec=0,
                        throughput_tokens_per_sec=0,
                        target_met=False,
                        target_threshold_ms=0,
                        error=str(e)
                    ))
        
        return results
    
    def benchmark_chunking_performance(self) -> List[PerformanceMetrics]:
        """Benchmark end-to-end chunking pipeline performance."""
        results = []
        
        # Test different chunking configurations
        chunking_configs = [
            ("small_chunks", {"target_tokens": 50, "max_tokens": 100, "overlap_tokens": 10}),
            ("medium_chunks", {"target_tokens": 150, "max_tokens": 300, "overlap_tokens": 30}),
            ("large_chunks", {"target_tokens": 400, "max_tokens": 800, "overlap_tokens": 80}),
        ]
        
        adapter_types = ["heuristic"]
        
        # Add other adapters if available
        try:
            from kano_backlog_core.tokenizer import TiktokenAdapter
            adapter_types.append("tiktoken")
        except ImportError:
            pass
            
        for doc_name, text in self.test_documents.items():
            text_size_kb = len(text) / 1024
            
            for config_name, config in chunking_configs:
                for adapter_type in adapter_types:
                    try:
                        options = ChunkingOptions(
                            target_tokens=config["target_tokens"],
                            max_tokens=config["max_tokens"],
                            overlap_tokens=config["overlap_tokens"],
                            tokenizer_adapter=adapter_type
                        )
                        
                        tokenizer: Any
                        if adapter_type == "heuristic":
                            tokenizer = HeuristicTokenizer("benchmark-model", chars_per_token=4.0)
                        else:
                            # Try to resolve from registry
                            tokenizer = self.registry.resolve(adapter_type, "benchmark-model")
                        
                        # Warm up
                        chunk_text_with_tokenizer("warmup", "warmup text", options, tokenizer)
                        
                        # Benchmark chunking
                        with self._measure_performance():
                            chunks = chunk_text_with_tokenizer(f"benchmark-{doc_name}", text, options, tokenizer)
                        
                        perf = self._last_performance
                        
                        # Calculate total tokens processed
                        total_tokens = sum(tokenizer.count_tokens(chunk.text).count for chunk in chunks)
                        
                        # Calculate throughput
                        throughput_chars = len(text) / (perf["processing_time_ms"] / 1000) if perf["processing_time_ms"] > 0 else 0
                        throughput_tokens = total_tokens / (perf["processing_time_ms"] / 1000) if perf["processing_time_ms"] > 0 else 0
                        
                        # Check if target is met (100KB documents should be < 500ms)
                        target_threshold = self.performance_targets["chunking_100kb_ms"]
                        scaled_target = target_threshold
                        if text_size_kb >= 100:
                            target_met = perf["processing_time_ms"] <= target_threshold
                        else:
                            # Scale target proportionally for smaller documents
                            scaled_target = target_threshold * (text_size_kb / 100)
                            target_met = perf["processing_time_ms"] <= scaled_target
                        
                        results.append(PerformanceMetrics(
                            operation=f"chunking_{config_name}",
                            adapter_type=adapter_type,
                            text_size_kb=text_size_kb,
                            text_size_chars=len(text),
                            processing_time_ms=perf["processing_time_ms"],
                            memory_used_mb=perf["memory_used_mb"],
                            tokens_processed=total_tokens,
                            chunks_produced=len(chunks),
                            throughput_chars_per_sec=throughput_chars,
                            throughput_tokens_per_sec=throughput_tokens,
                            target_met=target_met,
                            target_threshold_ms=target_threshold if text_size_kb >= 100 else scaled_target
                        ))
                        
                    except Exception as e:
                        results.append(PerformanceMetrics(
                            operation=f"chunking_{config_name}",
                            adapter_type=adapter_type,
                            text_size_kb=text_size_kb,
                            text_size_chars=len(text),
                            processing_time_ms=0,
                            memory_used_mb=0,
                            tokens_processed=0,
                            chunks_produced=0,
                            throughput_chars_per_sec=0,
                            throughput_tokens_per_sec=0,
                            target_met=False,
                            target_threshold_ms=0,
                            error=str(e)
                        ))
        
        return results
    
    def benchmark_memory_usage(self) -> List[PerformanceMetrics]:
        """Benchmark memory usage patterns and validate linear scaling."""
        if not PSUTIL_AVAILABLE:
            return []
            
        results = []
        
        # Test memory scaling with increasing document sizes
        base_text = self.test_documents["10kb_english"]
        size_multipliers = [0.1, 0.5, 1.0, 2.0, 5.0, 10.0]  # 1KB to 100KB
        
        options = ChunkingOptions(
            target_tokens=200,
            max_tokens=400,
            overlap_tokens=40,
            tokenizer_adapter="heuristic"
        )
        
        tokenizer = HeuristicTokenizer("memory-benchmark-model", chars_per_token=4.0)
        
        for multiplier in size_multipliers:
            # Create document of target size
            target_size = int(len(base_text) * multiplier)
            if multiplier < 1.0:
                text = base_text[:target_size]
            else:
                text = (base_text * int(multiplier + 1))[:target_size]
            
            text_size_kb = len(text) / 1024
            
            try:
                # Force garbage collection before measurement
                gc.collect()
                
                with self._measure_performance():
                    chunks = chunk_text_with_tokenizer(f"memory-test-{multiplier}", text, options, tokenizer)
                
                perf = self._last_performance
                
                # Calculate total tokens
                total_tokens = sum(tokenizer.count_tokens(chunk.text).count for chunk in chunks)
                
                # Memory scaling validation
                memory_per_kb = perf["memory_used_mb"] / text_size_kb if text_size_kb > 0 else 0
                
                results.append(PerformanceMetrics(
                    operation="memory_scaling",
                    adapter_type="heuristic",
                    text_size_kb=text_size_kb,
                    text_size_chars=len(text),
                    processing_time_ms=perf["processing_time_ms"],
                    memory_used_mb=perf["memory_used_mb"],
                    tokens_processed=total_tokens,
                    chunks_produced=len(chunks),
                    throughput_chars_per_sec=len(text) / (perf["processing_time_ms"] / 1000) if perf["processing_time_ms"] > 0 else 0,
                    throughput_tokens_per_sec=total_tokens / (perf["processing_time_ms"] / 1000) if perf["processing_time_ms"] > 0 else 0,
                    target_met=True,  # Memory scaling is validated separately
                    target_threshold_ms=0
                ))
                
            except Exception as e:
                results.append(PerformanceMetrics(
                    operation="memory_scaling",
                    adapter_type="heuristic",
                    text_size_kb=text_size_kb,
                    text_size_chars=len(text),
                    processing_time_ms=0,
                    memory_used_mb=0,
                    tokens_processed=0,
                    chunks_produced=0,
                    throughput_chars_per_sec=0,
                    throughput_tokens_per_sec=0,
                    target_met=False,
                    target_threshold_ms=0,
                    error=str(e)
                ))
        
        return results
    
    def analyze_adapter_comparison(self, tokenization_results: List[PerformanceMetrics]) -> Dict[str, Dict[str, float]]:
        """Analyze performance comparison between different adapters."""
        comparison: Dict[str, Dict[str, float]] = {}
        
        # Group results by adapter type
        by_adapter: Dict[str, List[PerformanceMetrics]] = {}
        for result in tokenization_results:
            if result.error is None:
                if result.adapter_type not in by_adapter:
                    by_adapter[result.adapter_type] = []
                by_adapter[result.adapter_type].append(result)
        
        # Calculate statistics for each adapter
        for adapter_type, results in by_adapter.items():
            if results:
                processing_times = [r.processing_time_ms for r in results]
                memory_usage = [r.memory_used_mb for r in results]
                throughput_chars = [r.throughput_chars_per_sec for r in results]
                throughput_tokens = [r.throughput_tokens_per_sec for r in results]
                
                comparison[adapter_type] = {
                    "avg_processing_time_ms": statistics.mean(processing_times),
                    "median_processing_time_ms": statistics.median(processing_times),
                    "max_processing_time_ms": max(processing_times),
                    "avg_memory_mb": statistics.mean(memory_usage) if PSUTIL_AVAILABLE else 0,
                    "avg_throughput_chars_per_sec": statistics.mean(throughput_chars),
                    "avg_throughput_tokens_per_sec": statistics.mean(throughput_tokens),
                    "target_compliance_rate": sum(1 for r in results if r.target_met) / len(results),
                    "error_rate": 0,  # Only successful results included
                    "sample_count": len(results)
                }
        
        return comparison
    
    def detect_performance_regressions(self, current_results: List[PerformanceMetrics], 
                                     baseline_file: Optional[Path] = None) -> Dict[str, Any]:
        """Detect performance regressions by comparing with baseline."""
        regressions_detected: List[RegressionRecord] = []
        improvements_detected: List[RegressionRecord] = []
        new_failures: List[RegressionRecord] = []

        regression_analysis: RegressionAnalysis = {
            "baseline_available": False,
            "regressions_detected": regressions_detected,
            "improvements_detected": improvements_detected,
            "new_failures": new_failures,
            "summary": {}
        }
        
        if baseline_file and baseline_file.exists():
            try:
                with open(baseline_file, 'r') as f:
                    baseline_data = json.load(f)
                
                baseline_results = [PerformanceMetrics(**r) for r in baseline_data.get("tokenization_benchmarks", [])]
                regression_analysis["baseline_available"] = True
                
                # Compare current results with baseline
                for current in current_results:
                    if current.error is None:
                        # Find matching baseline result
                        baseline = next((b for b in baseline_results 
                                       if b.adapter_type == current.adapter_type 
                                       and abs(b.text_size_kb - current.text_size_kb) < 0.1
                                       and b.operation == current.operation), None)
                        
                        if baseline and baseline.error is None:
                            # Check for regression (>20% slower)
                            if current.processing_time_ms > baseline.processing_time_ms * 1.2:
                                regressions_detected.append({
                                    "operation": current.operation,
                                    "adapter": current.adapter_type,
                                    "text_size_kb": current.text_size_kb,
                                    "baseline_time_ms": baseline.processing_time_ms,
                                    "current_time_ms": current.processing_time_ms,
                                    "regression_percent": ((current.processing_time_ms / baseline.processing_time_ms) - 1) * 100
                                })
                            
                            # Check for improvement (>20% faster)
                            elif current.processing_time_ms < baseline.processing_time_ms * 0.8:
                                improvements_detected.append({
                                    "operation": current.operation,
                                    "adapter": current.adapter_type,
                                    "text_size_kb": current.text_size_kb,
                                    "baseline_time_ms": baseline.processing_time_ms,
                                    "current_time_ms": current.processing_time_ms,
                                    "improvement_percent": (1 - (current.processing_time_ms / baseline.processing_time_ms)) * 100
                                })
                        
                        # Check for new failures
                        elif baseline and baseline.error is not None and current.error is None:
                            improvements_detected.append({
                                "operation": current.operation,
                                "adapter": current.adapter_type,
                                "text_size_kb": current.text_size_kb,
                                "note": "Previously failing test now passes"
                            })
                        elif baseline is None:
                            # New test case
                            pass
                    else:
                        # Check if this is a new failure
                        baseline = next((b for b in baseline_results 
                                       if b.adapter_type == current.adapter_type 
                                       and abs(b.text_size_kb - current.text_size_kb) < 0.1
                                       and b.operation == current.operation), None)
                        
                        if baseline and baseline.error is None:
                            new_failures.append({
                                "operation": current.operation,
                                "adapter": current.adapter_type,
                                "text_size_kb": current.text_size_kb,
                                "error": current.error
                            })
                
            except Exception as e:
                regression_analysis["baseline_error"] = str(e)
        
        # Generate summary
        summary: RegressionSummary = {
            "total_regressions": len(regressions_detected),
            "total_improvements": len(improvements_detected),
            "total_new_failures": len(new_failures),
            "overall_status": "PASS" if len(regressions_detected) == 0 and len(new_failures) == 0 else "FAIL"
        }
        regression_analysis["summary"] = summary
        
        return regression_analysis
    
    def generate_performance_report(self, output_file: Optional[Path] = None) -> BenchmarkReport:
        """Generate comprehensive performance report."""
        import sys
        import platform
        from datetime import datetime
        
        print("🚀 Running comprehensive performance benchmarks...")
        
        # Run all benchmarks
        print("  📊 Tokenization performance...")
        tokenization_results = self.benchmark_tokenization_performance()
        
        print("  🔧 Chunking pipeline performance...")
        chunking_results = self.benchmark_chunking_performance()
        
        print("  💾 Memory usage profiling...")
        memory_results = self.benchmark_memory_usage()
        
        print("  📈 Analyzing adapter comparison...")
        adapter_comparison = self.analyze_adapter_comparison(tokenization_results)
        
        print("  🔍 Detecting performance regressions...")
        baseline_file = Path("performance_baseline.json") if output_file else None
        regression_analysis = self.detect_performance_regressions(tokenization_results, baseline_file)
        
        # Generate system info
        system_info = {
            "platform": platform.platform(),
            "python_version": sys.version,
            "processor": platform.processor(),
            "memory_available": psutil.virtual_memory().total / 1024 / 1024 / 1024 if PSUTIL_AVAILABLE and psutil is not None else "unknown",
            "psutil_available": PSUTIL_AVAILABLE
        }
        
        # Calculate summary statistics
        all_results = tokenization_results + chunking_results + memory_results
        successful_results = [r for r in all_results if r.error is None]
        
        summary = {
            "total_benchmarks": len(all_results),
            "successful_benchmarks": len(successful_results),
            "failed_benchmarks": len(all_results) - len(successful_results),
            "target_compliance_rate": sum(1 for r in successful_results if r.target_met) / len(successful_results) if successful_results else 0,
            "avg_processing_time_ms": statistics.mean([r.processing_time_ms for r in successful_results]) if successful_results else 0,
            "avg_memory_usage_mb": statistics.mean([r.memory_used_mb for r in successful_results]) if successful_results and PSUTIL_AVAILABLE else 0,
            "avg_throughput_chars_per_sec": statistics.mean([r.throughput_chars_per_sec for r in successful_results]) if successful_results else 0,
        }
        
        # Create report
        report = BenchmarkReport(
            timestamp=datetime.now().isoformat(),
            python_version=sys.version,
            system_info=system_info,
            performance_targets=self.performance_targets,
            tokenization_benchmarks=tokenization_results,
            chunking_benchmarks=chunking_results,
            memory_benchmarks=memory_results,
            adapter_comparison=adapter_comparison,
            regression_analysis=regression_analysis,
            summary=summary
        )
        
        # Save report if output file specified
        if output_file:
            with open(output_file, 'w') as f:
                json.dump(asdict(report), f, indent=2, default=str)
            print(f"  💾 Report saved to {output_file}")
        
        return report


# Test classes for pytest integration
class TestTokenizationPerformance:
    """Test tokenization performance against targets."""
    
    def setup_method(self) -> None:
        self.benchmark_suite = PerformanceBenchmarkSuite()
    
    def test_tokenization_performance_targets(self) -> None:
        """Test that tokenization meets performance targets."""
        results = self.benchmark_suite.benchmark_tokenization_performance()
        
        # Filter for 10KB documents (primary target)
        target_results = [r for r in results if 9.5 <= r.text_size_kb <= 10.5 and r.error is None]
        
        assert len(target_results) > 0, "No 10KB tokenization results found"
        
        # Check that at least 80% of results meet the target
        passing_results = [r for r in target_results if r.target_met]
        pass_rate = len(passing_results) / len(target_results)
        
        assert pass_rate >= 0.8, f"Only {pass_rate:.1%} of 10KB tokenization tests met the 100ms target"
        
        # Log performance summary
        avg_time = statistics.mean([r.processing_time_ms for r in target_results])
        print(f"\n📊 Tokenization Performance Summary (10KB documents):")
        print(f"   Average time: {avg_time:.1f}ms (target: <100ms)")
        print(f"   Pass rate: {pass_rate:.1%}")
        print(f"   Adapters tested: {len(set(r.adapter_type for r in target_results))}")
    
    def test_tokenization_scaling(self) -> None:
        """Test that tokenization performance scales reasonably with document size."""
        results = self.benchmark_suite.benchmark_tokenization_performance()
        
        # Group by adapter type and analyze scaling
        by_adapter: Dict[str, List[PerformanceMetrics]] = {}
        for result in results:
            if result.error is None:
                if result.adapter_type not in by_adapter:
                    by_adapter[result.adapter_type] = []
                by_adapter[result.adapter_type].append(result)
        
        for adapter_type, adapter_results in by_adapter.items():
            if len(adapter_results) >= 3:  # Need multiple data points
                # Sort by document size
                adapter_results.sort(key=lambda r: r.text_size_kb)
                
                # Check that processing time increases reasonably with size
                smallest = adapter_results[0]
                largest = adapter_results[-1]
                
                if largest.text_size_kb > smallest.text_size_kb * 2:
                    # Time should not increase more than 10x for document size increase
                    time_ratio = largest.processing_time_ms / max(smallest.processing_time_ms, 0.1)
                    size_ratio = largest.text_size_kb / smallest.text_size_kb
                    
                    assert time_ratio <= size_ratio * 5, \
                        f"{adapter_type} adapter scaling is poor: {time_ratio:.1f}x time for {size_ratio:.1f}x size"


class TestChunkingPerformance:
    """Test chunking pipeline performance against targets."""
    
    def setup_method(self) -> None:
        self.benchmark_suite = PerformanceBenchmarkSuite()
    
    def test_chunking_performance_targets(self) -> None:
        """Test that chunking meets performance targets."""
        results = self.benchmark_suite.benchmark_chunking_performance()
        
        # Filter for 100KB documents (primary target)
        target_results = [r for r in results if 95 <= r.text_size_kb <= 105 and r.error is None]
        
        if len(target_results) == 0:
            pytest.skip("No 100KB chunking results found")
        
        # Check that at least 80% of results meet the target
        passing_results = [r for r in target_results if r.target_met]
        pass_rate = len(passing_results) / len(target_results)
        
        assert pass_rate >= 0.8, f"Only {pass_rate:.1%} of 100KB chunking tests met the 500ms target"
        
        # Log performance summary
        avg_time = statistics.mean([r.processing_time_ms for r in target_results])
        print(f"\n🔧 Chunking Performance Summary (100KB documents):")
        print(f"   Average time: {avg_time:.1f}ms (target: <500ms)")
        print(f"   Pass rate: {pass_rate:.1%}")
        print(f"   Configurations tested: {len(set(r.operation for r in target_results))}")
    
    def test_chunking_produces_valid_results(self) -> None:
        """Test that chunking produces valid results while meeting performance targets."""
        results = self.benchmark_suite.benchmark_chunking_performance()
        
        successful_results = [r for r in results if r.error is None]
        assert len(successful_results) > 0, "No successful chunking results"
        
        for result in successful_results:
            # Should produce at least one chunk
            assert result.chunks_produced >= 1, f"No chunks produced for {result.operation} with {result.adapter_type}"
            
            # Should process some tokens
            assert result.tokens_processed > 0, f"No tokens processed for {result.operation} with {result.adapter_type}"
            
            # Throughput should be reasonable
            assert result.throughput_chars_per_sec > 0, f"Zero throughput for {result.operation} with {result.adapter_type}"


class TestMemoryUsage:
    """Test memory usage patterns and scaling."""
    
    def setup_method(self) -> None:
        self.benchmark_suite = PerformanceBenchmarkSuite()
    
    def test_memory_scaling_linearity(self) -> None:
        """Test that memory usage scales linearly with document size."""
        if not PSUTIL_AVAILABLE:
            pytest.skip("psutil not available for memory testing")
        
        results = self.benchmark_suite.benchmark_memory_usage()
        
        # Filter successful results and sort by size
        successful_results = [r for r in results if r.error is None and r.memory_used_mb > 0]
        successful_results.sort(key=lambda r: r.text_size_kb)
        
        if len(successful_results) < 3:
            pytest.skip("Insufficient memory usage data points")
        
        # Check that memory usage doesn't grow excessively
        largest = successful_results[-1]
        smallest = successful_results[0]
        
        if largest.text_size_kb > smallest.text_size_kb * 2:
            memory_ratio = largest.memory_used_mb / max(smallest.memory_used_mb, 0.1)
            size_ratio = largest.text_size_kb / smallest.text_size_kb
            
            # Memory should not grow more than 3x the size ratio (allowing for overhead)
            scaling_factor = memory_ratio / size_ratio
            target_scaling = self.benchmark_suite.performance_targets["memory_scaling_factor"]
            
            assert scaling_factor <= target_scaling, \
                f"Memory scaling factor {scaling_factor:.1f} exceeds target {target_scaling}"
            
            print(f"\n💾 Memory Scaling Summary:")
            print(f"   Size ratio: {size_ratio:.1f}x")
            print(f"   Memory ratio: {memory_ratio:.1f}x")
            print(f"   Scaling factor: {scaling_factor:.1f} (target: <{target_scaling})")
    
    def test_memory_usage_reasonable(self) -> None:
        """Test that memory usage is reasonable for document sizes."""
        if not PSUTIL_AVAILABLE:
            pytest.skip("psutil not available for memory testing")
        
        results = self.benchmark_suite.benchmark_memory_usage()
        
        for result in results:
            if result.error is None and result.memory_used_mb > 0:
                # Memory usage should not exceed 10MB per KB of document (very generous limit)
                memory_per_kb = result.memory_used_mb / result.text_size_kb
                assert memory_per_kb <= 10, \
                    f"Excessive memory usage: {memory_per_kb:.1f}MB per KB for {result.text_size_kb:.1f}KB document"


class TestPerformanceRegression:
    """Test for performance regressions."""
    
    def setup_method(self) -> None:
        self.benchmark_suite = PerformanceBenchmarkSuite()
    
    def test_no_performance_regressions(self) -> None:
        """Test that there are no significant performance regressions."""
        # Run tokenization benchmarks
        results = self.benchmark_suite.benchmark_tokenization_performance()
        
        # Check for regression against baseline (if available)
        baseline_file = Path("performance_baseline.json")
        regression_analysis = self.benchmark_suite.detect_performance_regressions(results, baseline_file)
        
        if regression_analysis["baseline_available"]:
            # Assert no regressions detected
            regressions = regression_analysis["regressions_detected"]
            new_failures = regression_analysis["new_failures"]
            
            if regressions:
                regression_details = "\n".join([
                    f"  - {r['operation']} ({r['adapter']}, {r['text_size_kb']:.1f}KB): "
                    f"{r['baseline_time_ms']:.1f}ms → {r['current_time_ms']:.1f}ms "
                    f"({r['regression_percent']:+.1f}%)"
                    for r in regressions
                ])
                pytest.fail(f"Performance regressions detected:\n{regression_details}")
            
            if new_failures:
                failure_details = "\n".join([
                    f"  - {f['operation']} ({f['adapter']}, {f['text_size_kb']:.1f}KB): {f['error']}"
                    for f in new_failures
                ])
                pytest.fail(f"New test failures detected:\n{failure_details}")
            
            # Log improvements if any
            improvements = regression_analysis["improvements_detected"]
            if improvements:
                improvement_details = "\n".join([
                    f"  - {i['operation']} ({i['adapter']}, {i['text_size_kb']:.1f}KB): "
                    f"{i.get('improvement_percent', 0):.1f}% faster"
                    for i in improvements
                ])
                print(f"\n🎉 Performance improvements detected:\n{improvement_details}")
        else:
            print("\n📝 No baseline available for regression testing")


# Utility functions for generating reports
def generate_performance_report(output_file: str = "performance_report.json") -> BenchmarkReport:
    """Generate a comprehensive performance report."""
    suite = PerformanceBenchmarkSuite()
    report = suite.generate_performance_report(Path(output_file))
    
    print(f"\n📋 Performance Report Summary:")
    print(f"   Total benchmarks: {report.summary['total_benchmarks']}")
    print(f"   Successful: {report.summary['successful_benchmarks']}")
    print(f"   Failed: {report.summary['failed_benchmarks']}")
    print(f"   Target compliance: {report.summary['target_compliance_rate']:.1%}")
    print(f"   Average processing time: {report.summary['avg_processing_time_ms']:.1f}ms")
    if PSUTIL_AVAILABLE:
        print(f"   Average memory usage: {report.summary['avg_memory_usage_mb']:.1f}MB")
    
    return report


if __name__ == "__main__":
    # Generate performance report when run directly
    generate_performance_report()
