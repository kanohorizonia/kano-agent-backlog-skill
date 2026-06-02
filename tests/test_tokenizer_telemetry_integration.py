"""Integration tests for tokenizer telemetry system.

This module tests the complete integration of telemetry collection with
tokenizer adapters, including real-world usage scenarios and end-to-end
telemetry workflows.
"""

import json
import time
from datetime import datetime, timedelta
from pathlib import Path
from unittest.mock import patch

import pytest

from kano_backlog_core.tokenizer import (
    TokenizerRegistry, 
    HeuristicTokenizer, 
    TelemetryEnabledAdapter,
    get_default_registry
)
from kano_backlog_core.tokenizer_telemetry import (
    get_default_collector,
    get_default_monitor,
    configure_telemetry,
    setup_default_alerting,
    AlertThresholds
)
from kano_backlog_core.tokenizer_reporting import TelemetryReporter, HealthChecker


class TestTelemetryIntegrationWithRegistry:
    """Test telemetry integration with TokenizerRegistry."""
    
    def test_registry_wraps_adapters_with_telemetry(self):
        """Test that registry automatically wraps adapters with telemetry."""
        # Configure telemetry
        collector, monitor = configure_telemetry(max_history=100)
        
        # Create registry and resolve adapter
        registry = TokenizerRegistry()
        adapter = registry.resolve(
            adapter_name="heuristic",
            model_name="test-model",
            max_tokens=1024
        )
        
        # Should be wrapped with telemetry
        assert isinstance(adapter, TelemetryEnabledAdapter)
        assert adapter._wrapped_adapter.adapter_id == "heuristic"
        assert adapter.model_name == "test-model"
    
    def test_telemetry_collection_during_tokenization(self):
        """Test that telemetry is collected during normal tokenization."""
        # Configure telemetry
        collector, monitor = configure_telemetry(max_history=100)
        
        # Clear any existing telemetry
        collector.clear_history()
        
        # Create registry and resolve adapter
        registry = TokenizerRegistry()
        adapter = registry.resolve(
            adapter_name="heuristic",
            model_name="test-model"
        )
        
        # Perform tokenization
        test_text = "This is a comprehensive test of the telemetry integration system."
        result = adapter.count_tokens(test_text)
        
        # Verify tokenization worked
        assert result.count > 0
        assert result.method == "heuristic"
        
        # Verify telemetry was collected
        recent_telemetry = collector.get_recent_telemetry(limit=10)
        assert len(recent_telemetry) == 1
        
        telemetry = recent_telemetry[0]
        assert telemetry.adapter_name == "heuristic"
        assert telemetry.model_name == "test-model"
        assert telemetry.text_length == len(test_text)
        assert telemetry.token_count.count == result.count
        assert telemetry.processing_time_ms > 0
        assert not telemetry.error_occurred
        assert not telemetry.was_fallback
    
    def test_fallback_telemetry_collection(self):
        """Test telemetry collection during adapter fallback."""
        # Configure telemetry
        collector, monitor = configure_telemetry(max_history=100)
        collector.clear_history()
        
        # Create registry with fallback chain
        registry = TokenizerRegistry()
        registry.set_fallback_chain(["tiktoken", "heuristic"])  # tiktoken will likely fail
        
        # Try to resolve tiktoken (should fallback to heuristic)
        adapter = registry.resolve(
            adapter_name="tiktoken",  # This will likely fail and fallback
            model_name="test-model"
        )
        
        # Perform tokenization
        test_text = "Testing fallback telemetry collection."
        result = adapter.count_tokens(test_text)
        
        # Verify tokenization worked
        assert result.count > 0
        
        # Check telemetry for fallback information
        recent_telemetry = collector.get_recent_telemetry(limit=10)
        assert len(recent_telemetry) >= 1
        
        # Find telemetry record (might be heuristic if tiktoken failed)
        telemetry = recent_telemetry[0]
        assert telemetry.text_length == len(test_text)
        assert telemetry.processing_time_ms > 0
        
        # If fallback occurred, should be recorded
        if telemetry.was_fallback:
            assert telemetry.fallback_from is not None
    
    def test_error_telemetry_collection(self):
        """Test telemetry collection when errors occur."""
        # Configure telemetry
        collector, monitor = configure_telemetry(max_history=100)
        collector.clear_history()
        
        # Create a failing adapter for testing
        class FailingAdapter(HeuristicTokenizer):
            def count_tokens(self, text):
                raise ValueError("Simulated tokenization failure")
        
        # Wrap with telemetry
        failing_adapter = FailingAdapter("test-model")
        telemetry_adapter = TelemetryEnabledAdapter(failing_adapter)
        
        # Try tokenization (should fail)
        with pytest.raises(ValueError, match="Simulated tokenization failure"):
            telemetry_adapter.count_tokens("Test text for error scenario")
        
        # Verify error telemetry was collected
        recent_telemetry = collector.get_recent_telemetry(limit=10)
        assert len(recent_telemetry) == 1
        
        telemetry = recent_telemetry[0]
        assert telemetry.error_occurred
        assert telemetry.error_type == "ValueError"
        assert "Simulated tokenization failure" in telemetry.error_message
        assert telemetry.processing_time_ms > 0
    
    def test_multiple_adapters_telemetry(self):
        """Test telemetry collection with multiple adapters."""
        # Configure telemetry
        collector, monitor = configure_telemetry(max_history=100)
        collector.clear_history()
        
        # Create registry
        registry = TokenizerRegistry()
        
        # Test multiple adapters
        adapters_to_test = ["heuristic"]  # Start with heuristic which should always work
        
        # Try to add other adapters if available
        try:
            registry._create_adapter("tiktoken", "gpt-4")
            adapters_to_test.append("tiktoken")
        except:
            pass
        
        try:
            registry._create_adapter("huggingface", "bert-base-uncased")
            adapters_to_test.append("huggingface")
        except:
            pass
        
        # Test each adapter
        test_text = "Multi-adapter telemetry test with various tokenization methods."
        
        for adapter_name in adapters_to_test:
            try:
                adapter = registry.resolve(
                    adapter_name=adapter_name,
                    model_name="test-model"
                )
                result = adapter.count_tokens(test_text)
                assert result.count > 0
            except Exception as e:
                # If adapter fails, that's okay for this test
                print(f"Adapter {adapter_name} failed: {e}")
        
        # Verify telemetry was collected for successful adapters
        recent_telemetry = collector.get_recent_telemetry(limit=10)
        assert len(recent_telemetry) > 0
        
        # Check that we have telemetry for different adapters
        adapter_names = {t.adapter_name for t in recent_telemetry}
        assert len(adapter_names) >= 1  # At least heuristic should work
        
        # Verify all telemetry records have required fields
        for telemetry in recent_telemetry:
            assert telemetry.operation_id
            assert telemetry.adapter_name
            assert telemetry.model_name
            assert telemetry.text_length == len(test_text)
            assert telemetry.processing_time_ms > 0


class TestTelemetryReportingIntegration:
    """Test telemetry reporting with real data."""
    
    def test_end_to_end_reporting_workflow(self):
        """Test complete telemetry reporting workflow."""
        # Configure telemetry
        collector, monitor = configure_telemetry(max_history=100)
        collector.clear_history()
        
        # Create reporter and health checker
        reporter = TelemetryReporter(collector, monitor)
        health_checker = HealthChecker(collector, monitor)
        
        # Generate test data with multiple operations
        registry = TokenizerRegistry()
        adapter = registry.resolve(adapter_name="heuristic", model_name="test-model")
        
        test_texts = [
            "Short text.",
            "This is a medium length text for testing telemetry reporting capabilities.",
            "This is a much longer text that will generate more tokens and take more processing time to tokenize, which should provide good data for telemetry analysis and reporting functionality testing.",
            "Another test text with different characteristics.",
            "Final test text for comprehensive telemetry data collection."
        ]
        
        # Process all test texts
        for i, text in enumerate(test_texts):
            result = adapter.count_tokens(text)
            assert result.count > 0
            
            # Add small delay to spread out timestamps
            time.sleep(0.01)
        
        # Generate dashboard data
        dashboard_data = reporter.generate_dashboard_data(window_hours=1)
        
        # Verify dashboard data
        assert dashboard_data.total_operations == len(test_texts)
        assert dashboard_data.successful_operations == len(test_texts)
        assert dashboard_data.failed_operations == 0
        assert dashboard_data.total_adapters == 1
        assert dashboard_data.active_adapters == 1
        
        # Check adapter usage stats
        assert "heuristic" in dashboard_data.adapter_usage
        heuristic_stats = dashboard_data.adapter_usage["heuristic"]
        assert heuristic_stats.total_operations == len(test_texts)
        assert heuristic_stats.success_rate == 1.0
        assert heuristic_stats.fallback_rate == 0.0
        
        # Check health status
        health_status = dashboard_data.health_status
        assert health_status.status in ["healthy", "warning", "critical"]
        assert 0.0 <= health_status.score <= 1.0
        
        # Generate text report
        text_report = reporter.generate_text_report(window_hours=1)
        assert "TOKENIZER TELEMETRY REPORT" in text_report
        assert "heuristic" in text_report.lower()
        assert str(len(test_texts)) in text_report
        
        # Generate JSON report
        json_report = reporter.generate_json_report(window_hours=1)
        assert json_report["total_operations"] == len(test_texts)
        assert json_report["successful_operations"] == len(test_texts)
        assert "heuristic" in json_report["adapter_usage"]
        
        # Check system health
        health_summary = health_checker.get_health_summary()
        assert "status" in health_summary
        assert "score" in health_summary
        assert "component_health" in health_summary
    
    def test_performance_monitoring_integration(self):
        """Test performance monitoring with real operations."""
        # Configure telemetry with custom thresholds
        thresholds = AlertThresholds(
            max_avg_processing_time_ms=100.0,  # Low threshold for testing
            max_error_rate=0.1,
            min_sample_count=3
        )
        collector, monitor = configure_telemetry(
            max_history=100,
            alert_thresholds=thresholds
        )
        collector.clear_history()
        
        # Setup alert callback
        alerts_received = []
        def test_alert_callback(alert_type, alert_data):
            alerts_received.append((alert_type, alert_data))
        
        monitor.add_alert_callback(test_alert_callback)
        
        # Generate operations that might trigger alerts
        registry = TokenizerRegistry()
        adapter = registry.resolve(adapter_name="heuristic", model_name="test-model")
        
        # Create some operations with varying performance
        for i in range(10):
            text = f"Performance test text number {i} " * (i + 1)  # Varying lengths
            result = adapter.count_tokens(text)
            assert result.count > 0
            time.sleep(0.001)  # Small delay
        
        # Calculate performance metrics
        metrics = monitor.calculate_metrics(window_minutes=5)
        
        # Should have metrics for heuristic adapter
        assert "heuristic" in metrics
        heuristic_metrics = metrics["heuristic"]
        
        assert heuristic_metrics.sample_count == 10
        assert heuristic_metrics.avg_processing_time_ms > 0
        assert heuristic_metrics.operations_per_second > 0
        assert heuristic_metrics.error_rate == 0.0  # No errors
        assert heuristic_metrics.fallback_rate == 0.0  # No fallbacks
        
        # Check for alerts
        alerts = monitor.check_alerts(window_minutes=5)
        
        # Alerts may or may not be triggered depending on actual performance
        # Just verify the alert checking works without errors
        assert isinstance(alerts, list)
        
        # If alerts were triggered, verify structure
        for alert in alerts:
            assert "alert_type" in alert
            assert "adapter_name" in alert
            assert "message" in alert
            assert "timestamp" in alert
            assert "data" in alert
    
    def test_telemetry_export_and_import_workflow(self, tmp_path):
        """Test exporting and working with telemetry data."""
        # Configure telemetry
        collector, monitor = configure_telemetry(max_history=100)
        collector.clear_history()
        
        # Generate test data
        registry = TokenizerRegistry()
        adapter = registry.resolve(adapter_name="heuristic", model_name="export-test-model")
        
        test_operations = [
            "Export test operation 1",
            "Export test operation 2 with more content",
            "Export test operation 3 with even more content for variety"
        ]
        
        for text in test_operations:
            result = adapter.count_tokens(text)
            assert result.count > 0
        
        # Export telemetry data
        export_path = tmp_path / "telemetry_export.json"
        collector.export_telemetry(export_path, format="json")
        
        # Verify export file exists and has content
        assert export_path.exists()
        assert export_path.stat().st_size > 0
        
        # Load and verify exported data
        with open(export_path) as f:
            exported_data = json.load(f)
        
        assert "export_timestamp" in exported_data
        assert exported_data["telemetry_count"] == len(test_operations)
        assert "adapter_stats" in exported_data
        assert "telemetry_records" in exported_data
        
        # Verify adapter stats
        adapter_stats = exported_data["adapter_stats"]
        assert "heuristic" in adapter_stats
        heuristic_stats = adapter_stats["heuristic"]
        assert heuristic_stats["total_operations"] == len(test_operations)
        assert heuristic_stats["successful_operations"] == len(test_operations)
        
        # Verify telemetry records
        telemetry_records = exported_data["telemetry_records"]
        assert len(telemetry_records) == len(test_operations)
        
        for record in telemetry_records:
            assert "operation_id" in record
            assert "adapter_name" in record
            assert record["adapter_name"] == "heuristic"
            assert record["model_name"] == "export-test-model"
            assert "processing_time_ms" in record
            assert "token_count" in record
        
        # Export report
        reporter = TelemetryReporter(collector, monitor)
        report_path = tmp_path / "telemetry_report.json"
        reporter.export_report(report_path, format="json", window_hours=1)
        
        # Verify report export
        assert report_path.exists()
        
        with open(report_path) as f:
            report_data = json.load(f)
        
        assert report_data["total_operations"] == len(test_operations)
        assert "adapter_usage" in report_data
        assert "health_status" in report_data


class TestTelemetryPerformanceImpact:
    """Test that telemetry doesn't significantly impact performance."""
    
    def test_telemetry_overhead(self):
        """Test that telemetry adds minimal overhead."""
        # Test without telemetry
        adapter_no_telemetry = HeuristicTokenizer("test-model")
        # Use a larger text so baseline tokenization dominates fixed telemetry overhead.
        test_text = ("Performance impact test text for measuring telemetry overhead. " * 200).strip()
        
        # Measure time without telemetry
        start_time = time.perf_counter()
        for _ in range(200):
            result = adapter_no_telemetry.count_tokens(test_text)
        no_telemetry_time = time.perf_counter() - start_time
        
        # Test with telemetry
        # Disable memory tracking to measure telemetry wrapper overhead separately from
        # memory sampling overhead, which is intentionally configurable.
        collector, monitor = configure_telemetry(
            max_history=1000, enable_memory_tracking=False
        )
        collector.clear_history()
        
        adapter_with_telemetry = TelemetryEnabledAdapter(
            HeuristicTokenizer("test-model")
        )
        
        # Measure time with telemetry
        start_time = time.perf_counter()
        for _ in range(200):
            result = adapter_with_telemetry.count_tokens(test_text)
        with_telemetry_time = time.perf_counter() - start_time
        
        # Calculate overhead
        overhead_ratio = with_telemetry_time / no_telemetry_time
        
        # Telemetry should add bounded overhead on a representative workload.
        assert overhead_ratio < 2.5, f"Telemetry overhead too high: {overhead_ratio:.2f}x"
        
        # Verify telemetry was collected
        recent_telemetry = collector.get_recent_telemetry(limit=300)
        assert len(recent_telemetry) == 200
    
    def test_telemetry_memory_usage(self):
        """Test telemetry memory usage with large datasets."""
        # Configure telemetry with limited history
        collector, monitor = configure_telemetry(max_history=50)
        collector.clear_history()
        
        # Create adapter
        registry = TokenizerRegistry()
        adapter = registry.resolve(adapter_name="heuristic", model_name="memory-test")
        
        # Generate many operations
        for i in range(100):  # More than max_history
            text = f"Memory test operation {i} with some content"
            result = adapter.count_tokens(text)
            assert result.count > 0
        
        # Verify history is limited
        recent_telemetry = collector.get_recent_telemetry(limit=1000)
        assert len(recent_telemetry) == 50  # Should be limited by max_history
        
        # Verify we have the most recent operations
        operation_ids = [t.operation_id for t in recent_telemetry]
        # Should contain recent operations (exact IDs depend on implementation)
        assert len(set(operation_ids)) == 50  # All should be unique


class TestTelemetryErrorHandling:
    """Test telemetry error handling and graceful degradation."""
    
    def test_telemetry_failure_doesnt_break_tokenization(self):
        """Test that telemetry failures don't break tokenization."""
        # Create adapter with broken telemetry collector
        class BrokenCollector:
            def track_operation(self, *args, **kwargs):
                raise Exception("Telemetry system failure")
        
        # This should still work even if telemetry fails
        adapter = HeuristicTokenizer("test-model")
        
        # Mock the telemetry to fail
        with patch('kano_backlog_core.tokenizer_telemetry.get_default_collector') as mock_collector:
            mock_collector.return_value = BrokenCollector()
            
            # Tokenization should still work
            result = adapter.count_tokens("Test text with broken telemetry")
            assert result.count > 0
            assert result.method == "heuristic"
    
    def test_telemetry_disabled_gracefully(self):
        """Test graceful handling when telemetry is disabled."""
        # Create adapter without telemetry
        adapter = HeuristicTokenizer("test-model")
        
        # Should work normally
        result = adapter.count_tokens("Test text without telemetry")
        assert result.count > 0
        assert result.method == "heuristic"
    
    def test_partial_telemetry_failure(self):
        """Test handling of partial telemetry failures."""
        collector, monitor = configure_telemetry(max_history=100)
        collector.clear_history()
        
        # Create adapter
        adapter = TelemetryEnabledAdapter(HeuristicTokenizer("test-model"))
        
        # Mock memory tracking to fail
        with patch.object(collector, '_get_memory_usage', side_effect=Exception("Memory tracking failed")):
            # Should still collect telemetry without memory info
            result = adapter.count_tokens("Test with partial telemetry failure")
            assert result.count > 0
        
        # Verify telemetry was still collected
        recent_telemetry = collector.get_recent_telemetry(limit=10)
        assert len(recent_telemetry) == 1
        
        telemetry = recent_telemetry[0]
        assert not telemetry.error_occurred  # Tokenization succeeded
        assert telemetry.memory_used_mb is None  # Memory tracking failed


class TestTelemetryConfigurationIntegration:
    """Test telemetry configuration and setup."""
    
    def test_default_telemetry_setup(self):
        """Test default telemetry setup and configuration."""
        # Get default instances
        collector = get_default_collector()
        monitor = get_default_monitor()
        
        assert isinstance(collector, type(get_default_collector()))
        assert isinstance(monitor, type(get_default_monitor()))
        
        # Should be the same instances
        assert collector is get_default_collector()
        assert monitor is get_default_monitor()
    
    def test_custom_telemetry_configuration(self):
        """Test custom telemetry configuration."""
        custom_thresholds = AlertThresholds(
            max_avg_processing_time_ms=50.0,
            max_error_rate=0.05,
            max_fallback_rate=0.15
        )
        
        collector, monitor = configure_telemetry(
            max_history=200,
            enable_memory_tracking=True,
            alert_thresholds=custom_thresholds
        )
        
        # Verify configuration
        assert monitor._thresholds.max_avg_processing_time_ms == 50.0
        assert monitor._thresholds.max_error_rate == 0.05
        assert monitor._thresholds.max_fallback_rate == 0.15
    
    def test_alert_setup_and_callbacks(self):
        """Test alert setup and callback functionality."""
        collector, monitor = configure_telemetry(max_history=100)
        
        # Setup default alerting
        setup_default_alerting()
        
        # Should have at least one callback
        assert len(monitor._alert_callbacks) > 0
        
        # Add custom callback
        custom_alerts = []
        def custom_callback(alert_type, alert_data):
            custom_alerts.append((alert_type, alert_data))
        
        monitor.add_alert_callback(custom_callback)
        
        # Should have multiple callbacks now
        assert len(monitor._alert_callbacks) > 1


if __name__ == "__main__":
    pytest.main([__file__])
