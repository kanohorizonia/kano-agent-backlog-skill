"""Validation tests for the performance benchmark suite.

This module tests the performance benchmark implementation itself to ensure:
- Benchmarks run correctly and produce valid results
- Performance metrics are calculated accurately
- Regression detection works properly
- Report generation functions correctly
- All performance targets are properly validated
"""

import pytest
import json
import tempfile
from pathlib import Path
from unittest.mock import patch, MagicMock

from test_tokenizer_performance_benchmarks import (
    PerformanceBenchmarkSuite,
    PerformanceMetrics,
    BenchmarkReport,
)
from performance_utils import (
    BaselineManager,
    analyze_performance_trends,
    generate_performance_summary,
)


class TestPerformanceBenchmarkSuite:
    """Test the performance benchmark suite implementation."""
    
    def setup_method(self):
        self.suite = PerformanceBenchmarkSuite()
    
    def test_benchmark_suite_initialization(self):
        """Test that benchmark suite initializes correctly."""
        assert self.suite.registry is not None
        assert len(self.suite.performance_targets) > 0
        assert len(self.suite.test_documents) > 0
        
        # Check that test documents have expected sizes
        assert "1kb_english" in self.suite.test_documents
        assert "10kb_english" in self.suite.test_documents
        assert "100kb_english" in self.suite.test_documents
        
        # Verify document sizes are approximately correct
        assert 900 <= len(self.suite.test_documents["1kb_english"]) <= 1100
        assert 9000 <= len(self.suite.test_documents["10kb_english"]) <= 11000
        assert 90000 <= len(self.suite.test_documents["100kb_english"]) <= 110000
    
    def test_test_document_generation(self):
        """Test that test documents are generated with correct characteristics."""
        documents = self.suite.test_documents
        
        # Check that we have documents of different types
        english_docs = [k for k in documents.keys() if "english" in k]
        cjk_docs = [k for k in documents.keys() if "cjk" in k]
        mixed_docs = [k for k in documents.keys() if "mixed" in k]
        
        assert len(english_docs) >= 3  # 1kb, 10kb, 100kb
        assert len(cjk_docs) >= 3
        assert len(mixed_docs) >= 3
        
        # Verify content characteristics
        assert "tokenizer" in documents["1kb_english"].lower()
        assert "トークナイザー" in documents["1kb_cjk"]
        assert "🚀" in documents["10kb_mixed"]  # Mixed corpus should contain emoji
    
    def test_performance_measurement_context(self):
        """Test the performance measurement context manager."""
        with self.suite._measure_performance():
            # Simulate some work
            text = "test" * 1000
            _ = len(text)
        
        # Should have recorded performance metrics
        assert hasattr(self.suite, '_last_performance')
        assert 'processing_time_ms' in self.suite._last_performance
        assert 'memory_used_mb' in self.suite._last_performance
        assert self.suite._last_performance['processing_time_ms'] >= 0
    
    def test_tokenization_benchmark_execution(self):
        """Test that tokenization benchmarks execute without errors."""
        # Run a subset of benchmarks to avoid long test times
        original_docs = self.suite.test_documents
        self.suite.test_documents = {
            "1kb_english": original_docs["1kb_english"],
            "10kb_english": original_docs["10kb_english"]
        }
        
        results = self.suite.benchmark_tokenization_performance()
        
        # Should have results
        assert len(results) > 0
        
        # Check result structure
        for result in results:
            assert isinstance(result, PerformanceMetrics)
            assert result.operation == "tokenization"
            assert result.adapter_type in ["heuristic_3.0", "heuristic_4.0", "heuristic_5.0", "tiktoken", "huggingface"]
            assert result.text_size_kb > 0
            assert result.text_size_chars > 0
            
            if result.error is None:
                assert result.processing_time_ms >= 0
                assert result.tokens_processed > 0
                assert result.throughput_chars_per_sec >= 0
                assert isinstance(result.target_met, bool)
    
    def test_chunking_benchmark_execution(self):
        """Test that chunking benchmarks execute without errors."""
        # Run a subset of benchmarks
        original_docs = self.suite.test_documents
        self.suite.test_documents = {
            "10kb_english": original_docs["10kb_english"]
        }
        
        results = self.suite.benchmark_chunking_performance()
        
        # Should have results
        assert len(results) > 0
        
        # Check result structure
        for result in results:
            assert isinstance(result, PerformanceMetrics)
            assert "chunking" in result.operation
            assert result.adapter_type in ["heuristic", "tiktoken", "huggingface"]
            
            if result.error is None:
                assert result.processing_time_ms >= 0
                assert result.chunks_produced > 0
                assert result.tokens_processed > 0
    
    @pytest.mark.skipif(not pytest.importorskip("psutil", reason="psutil not available"), reason="psutil required for memory tests")
    def test_memory_benchmark_execution(self):
        """Test that memory benchmarks execute without errors."""
        results = self.suite.benchmark_memory_usage()
        
        # Should have results if psutil is available
        assert len(results) > 0
        
        # Check result structure
        for result in results:
            assert isinstance(result, PerformanceMetrics)
            assert result.operation == "memory_scaling"
            assert result.adapter_type == "heuristic"
            
            if result.error is None:
                assert result.processing_time_ms >= 0
                assert result.memory_used_mb >= 0
                assert result.chunks_produced > 0
    
    def test_adapter_comparison_analysis(self):
        """Test adapter comparison analysis."""
        # Create mock results
        mock_results = [
            PerformanceMetrics(
                operation="tokenization",
                adapter_type="heuristic_4.0",
                text_size_kb=10.0,
                text_size_chars=10240,
                processing_time_ms=50.0,
                memory_used_mb=1.0,
                tokens_processed=2500,
                chunks_produced=0,
                throughput_chars_per_sec=204800,
                throughput_tokens_per_sec=50000,
                target_met=True,
                target_threshold_ms=100.0
            ),
            PerformanceMetrics(
                operation="tokenization",
                adapter_type="heuristic_3.0",
                text_size_kb=10.0,
                text_size_chars=10240,
                processing_time_ms=75.0,
                memory_used_mb=1.2,
                tokens_processed=3400,
                chunks_produced=0,
                throughput_chars_per_sec=136533,
                throughput_tokens_per_sec=45333,
                target_met=True,
                target_threshold_ms=100.0
            )
        ]
        
        comparison = self.suite.analyze_adapter_comparison(mock_results)
        
        # Should have comparison data
        assert len(comparison) == 2
        assert "heuristic_4.0" in comparison
        assert "heuristic_3.0" in comparison
        
        # Check comparison structure
        for adapter, metrics in comparison.items():
            assert "avg_processing_time_ms" in metrics
            assert "target_compliance_rate" in metrics
            assert "avg_throughput_chars_per_sec" in metrics
            assert "sample_count" in metrics
            assert metrics["sample_count"] > 0
    
    def test_regression_detection_no_baseline(self):
        """Test regression detection when no baseline is available."""
        mock_results = [
            PerformanceMetrics(
                operation="tokenization",
                adapter_type="heuristic",
                text_size_kb=10.0,
                text_size_chars=10240,
                processing_time_ms=50.0,
                memory_used_mb=1.0,
                tokens_processed=2500,
                chunks_produced=0,
                throughput_chars_per_sec=204800,
                throughput_tokens_per_sec=50000,
                target_met=True,
                target_threshold_ms=100.0
            )
        ]
        
        regression_analysis = self.suite.detect_performance_regressions(mock_results, None)
        
        # Should indicate no baseline available
        assert regression_analysis["baseline_available"] is False
        assert len(regression_analysis["regressions_detected"]) == 0
        assert len(regression_analysis["improvements_detected"]) == 0
        assert regression_analysis["summary"]["overall_status"] == "PASS"
    
    def test_regression_detection_with_baseline(self):
        """Test regression detection with a baseline file."""
        # Create temporary baseline file
        with tempfile.NamedTemporaryFile(mode='w', suffix='.json', delete=False) as f:
            baseline_data = {
                "tokenization_benchmarks": [
                    {
                        "operation": "tokenization",
                        "adapter_type": "heuristic",
                        "text_size_kb": 10.0,
                        "text_size_chars": 10240,
                        "processing_time_ms": 40.0,  # Baseline is faster
                        "memory_used_mb": 1.0,
                        "tokens_processed": 2500,
                        "chunks_produced": 0,
                        "throughput_chars_per_sec": 256000,
                        "throughput_tokens_per_sec": 62500,
                        "target_met": True,
                        "target_threshold_ms": 100.0,
                        "error": None
                    }
                ]
            }
            json.dump(baseline_data, f)
            baseline_file = Path(f.name)
        
        try:
            # Current results (slower than baseline)
            current_results = [
                PerformanceMetrics(
                    operation="tokenization",
                    adapter_type="heuristic",
                    text_size_kb=10.0,
                    text_size_chars=10240,
                    processing_time_ms=60.0,  # 50% slower than baseline
                    memory_used_mb=1.0,
                    tokens_processed=2500,
                    chunks_produced=0,
                    throughput_chars_per_sec=170667,
                    throughput_tokens_per_sec=41667,
                    target_met=True,
                    target_threshold_ms=100.0
                )
            ]
            
            regression_analysis = self.suite.detect_performance_regressions(current_results, baseline_file)
            
            # Should detect regression
            assert regression_analysis["baseline_available"] is True
            assert len(regression_analysis["regressions_detected"]) == 1
            assert regression_analysis["summary"]["overall_status"] == "FAIL"
            
            regression = regression_analysis["regressions_detected"][0]
            assert regression["operation"] == "tokenization"
            assert regression["adapter"] == "heuristic"
            assert regression["baseline_time_ms"] == 40.0
            assert regression["current_time_ms"] == 60.0
            assert regression["regression_percent"] == 50.0
            
        finally:
            baseline_file.unlink()  # Clean up
    
    def test_performance_report_generation(self):
        """Test that performance reports are generated correctly."""
        with tempfile.NamedTemporaryFile(mode='w', suffix='.json', delete=False) as f:
            output_file = Path(f.name)
        
        try:
            # Mock the benchmark methods to avoid long execution
            with patch.object(self.suite, 'benchmark_tokenization_performance') as mock_tokenization, \
                 patch.object(self.suite, 'benchmark_chunking_performance') as mock_chunking, \
                 patch.object(self.suite, 'benchmark_memory_usage') as mock_memory:
                
                # Set up mock returns
                mock_tokenization.return_value = [
                    PerformanceMetrics(
                        operation="tokenization",
                        adapter_type="heuristic",
                        text_size_kb=10.0,
                        text_size_chars=10240,
                        processing_time_ms=50.0,
                        memory_used_mb=1.0,
                        tokens_processed=2500,
                        chunks_produced=0,
                        throughput_chars_per_sec=204800,
                        throughput_tokens_per_sec=50000,
                        target_met=True,
                        target_threshold_ms=100.0
                    )
                ]
                mock_chunking.return_value = []
                mock_memory.return_value = []
                
                report = self.suite.generate_performance_report(output_file)
                
                # Check report structure
                assert isinstance(report, BenchmarkReport)
                assert report.timestamp is not None
                assert report.python_version is not None
                assert len(report.performance_targets) > 0
                assert len(report.tokenization_benchmarks) > 0
                assert report.summary is not None
                
                # Check that file was created
                assert output_file.exists()
                
                # Verify file content
                with open(output_file, 'r') as f:
                    saved_data = json.load(f)
                assert "timestamp" in saved_data
                assert "tokenization_benchmarks" in saved_data
                assert "summary" in saved_data
                
        finally:
            if output_file.exists():
                output_file.unlink()  # Clean up


class TestPerformanceUtils:
    """Test the performance utilities."""
    
    def test_baseline_manager_save_and_load(self):
        """Test baseline manager save and load functionality."""
        with tempfile.TemporaryDirectory() as temp_dir:
            baseline_dir = Path(temp_dir)
            manager = BaselineManager(baseline_dir)
            
            # Mock report data
            report_data = {
                "system_info": {"platform": "test", "python_version": "3.8"},
                "tokenization_benchmarks": [
                    {
                        "operation": "tokenization",
                        "adapter_type": "heuristic",
                        "text_size_kb": 10.0,
                        "processing_time_ms": 50.0,
                        "memory_used_mb": 1.0,
                        "throughput_chars_per_sec": 204800,
                        "target_met": True,
                        "error": None
                    }
                ]
            }
            
            # Save baseline
            baseline_file = manager.save_baseline(report_data, "test_version")
            assert baseline_file.exists()
            
            # Load baseline
            baseline = manager.load_baseline("test_version")
            assert baseline is not None
            assert baseline.version == "test_version"
            assert len(baseline.benchmarks) == 1
            
            # Test loading non-existent baseline
            missing_baseline = manager.load_baseline("non_existent")
            assert missing_baseline is None
    
    def test_performance_trends_analysis(self):
        """Test performance trends analysis."""
        # Create mock reports with trend
        reports = [
            {
                "timestamp": "2024-01-01T00:00:00",
                "tokenization_benchmarks": [
                    {
                        "operation": "tokenization",
                        "adapter_type": "heuristic",
                        "text_size_kb": 10.0,
                        "processing_time_ms": 100.0,
                        "memory_used_mb": 1.0,
                        "throughput_chars_per_sec": 100000,
                        "error": None
                    }
                ]
            },
            {
                "timestamp": "2024-01-02T00:00:00",
                "tokenization_benchmarks": [
                    {
                        "operation": "tokenization",
                        "adapter_type": "heuristic",
                        "text_size_kb": 10.0,
                        "processing_time_ms": 80.0,  # Improving trend
                        "memory_used_mb": 1.0,
                        "throughput_chars_per_sec": 125000,
                        "error": None
                    }
                ]
            }
        ]
        
        trends = analyze_performance_trends(reports)
        
        # Should detect improving trend
        key = "tokenization_heuristic_10.0kb"
        assert key in trends
        assert trends[key]["direction"] == "improving"
        assert trends[key]["magnitude_percent"] > 0
        assert trends[key]["data_points"] == 2
    
    def test_performance_summary_generation(self):
        """Test performance summary generation."""
        # Mock report data
        report = {
            "summary": {
                "total_benchmarks": 10,
                "successful_benchmarks": 9,
                "failed_benchmarks": 1,
                "target_compliance_rate": 0.8
            },
            "performance_targets": {
                "tokenization_10kb_ms": 100.0,
                "chunking_100kb_ms": 500.0,
                "memory_scaling_factor": 2.0
            },
            "adapter_comparison": {
                "heuristic": {
                    "avg_processing_time_ms": 75.0,
                    "target_compliance_rate": 0.9
                }
            },
            "regression_analysis": {
                "baseline_available": True,
                "summary": {
                    "overall_status": "PASS",
                    "total_regressions": 0,
                    "total_improvements": 1
                },
                "regressions_detected": [],
                "improvements_detected": [
                    {
                        "operation": "tokenization",
                        "adapter": "heuristic",
                        "improvement_percent": 20.0
                    }
                ]
            },
            "system_info": {
                "platform": "Linux-test",
                "python_version": "3.8.10",
                "memory_available": 16.0
            }
        }
        
        summary = generate_performance_summary(report)
        
        # Check that summary contains expected sections
        assert "Performance Benchmark Summary" in summary
        assert "Overall Results:" in summary
        assert "Performance Targets:" in summary
        assert "Adapter Performance Comparison:" in summary
        assert "Regression Analysis: PASS" in summary
        assert "Improvements:" in summary
        assert "System Information:" in summary
        
        # Check specific values
        assert "Total benchmarks: 10" in summary
        assert "Successful: 9" in summary
        assert "Target compliance: 80.0%" in summary
        assert "heuristic: 75.0ms avg, 90.0% compliance" in summary


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
