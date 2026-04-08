"""Tests for robust error handling and fallback mechanisms in tokenizer adapters."""

import pytest
from unittest.mock import Mock, patch, MagicMock
from typing import Dict, Any

from kano_backlog_core.tokenizer import (
    TokenizerRegistry,
    HeuristicTokenizer,
    TiktokenAdapter,
    HuggingFaceAdapter,
    resolve_tokenizer_with_fallback,
)
from kano_backlog_core.tokenizer_errors import (
    TokenizerError,
    AdapterNotAvailableError,
    DependencyMissingError,
    TokenizationFailedError,
    FallbackChainExhaustedError,
    ErrorRecoveryManager,
)


class TestErrorRecoveryManager:
    """Test ErrorRecoveryManager functionality."""
    
    def test_recovery_attempt_tracking(self):
        """Test recovery attempt tracking and limits."""
        manager = ErrorRecoveryManager()
        error_key = "tiktoken:gpt-4"
        
        # Should allow initial attempts
        assert manager.should_attempt_recovery(error_key) is True
        
        # Record attempts up to limit
        for i in range(3):
            manager.record_recovery_attempt(error_key)
            if i < 2:
                assert manager.should_attempt_recovery(error_key) is True
            else:
                assert manager.should_attempt_recovery(error_key) is False
        
        # Reset should allow attempts again
        manager.reset_recovery_attempts(error_key)
        assert manager.should_attempt_recovery(error_key) is True
    
    def test_fallback_adapter_suggestion(self):
        """Test fallback adapter suggestion logic."""
        manager = ErrorRecoveryManager()
        
        # Test tiktoken fallback preferences
        fallback = manager.suggest_fallback_adapter("tiktoken", ["huggingface", "heuristic"])
        assert fallback == "huggingface"
        
        fallback = manager.suggest_fallback_adapter("tiktoken", ["heuristic"])
        assert fallback == "heuristic"
        
        # Test huggingface fallback preferences
        fallback = manager.suggest_fallback_adapter("huggingface", ["tiktoken", "heuristic"])
        assert fallback == "tiktoken"
        
        # Test no available fallbacks
        fallback = manager.suggest_fallback_adapter("tiktoken", [])
        assert fallback is None
    
    def test_degradation_event_recording(self):
        """Test degradation event recording and history."""
        manager = ErrorRecoveryManager()
        error = ImportError("tiktoken not found")
        
        manager.record_degradation_event("tiktoken", "heuristic", "gpt-4", error)
        
        stats = manager.get_recovery_statistics()
        assert stats["total_degradation_events"] == 1
        assert "tiktoken" in stats["degradation_by_adapter"]
        assert stats["degradation_by_adapter"]["tiktoken"]["total_events"] == 1
    
    def test_recovery_strategy_suggestions(self):
        """Test recovery strategy suggestions for different error types."""
        manager = ErrorRecoveryManager()
        
        # Test dependency missing error
        import_error = ImportError("No module named 'tiktoken'")
        strategy = manager.suggest_recovery_strategy(import_error, "tiktoken", "gpt-4")
        
        assert strategy["recommended_action"] == "install_dependency"
        assert strategy["fallback_adapter"] == "huggingface"
        assert "pip install tiktoken" in strategy["user_message"]
        assert strategy["retry_recommended"] is True
        
        # Test configuration error
        config_error = ValueError("Invalid model name")
        strategy = manager.suggest_recovery_strategy(config_error, "tiktoken", "invalid-model")
        
        assert strategy["recommended_action"] == "fix_configuration"
        assert strategy["fallback_adapter"] == "heuristic"
        assert "Configuration error" in strategy["user_message"]
        
        # Test generic error
        generic_error = RuntimeError("Unknown error")
        strategy = manager.suggest_recovery_strategy(generic_error, "tiktoken", "gpt-4")
        
        assert strategy["fallback_adapter"] == "heuristic"
        assert "approximate tokenization" in strategy["user_message"]


class TestTokenizerRegistryErrorHandling:
    """Test TokenizerRegistry error handling and fallback mechanisms."""
    
    def test_graceful_degradation_on_import_error(self):
        """Test graceful degradation when adapter dependencies are missing."""
        registry = TokenizerRegistry()
        
        # Mock the adapter class in the registry's internal storage
        original_adapters = registry._adapters.copy()
        
        # Create a mock adapter class that raises ImportError
        def mock_tiktoken_class(*args, **kwargs):
            raise ImportError("No module named 'tiktoken'")
        
        # Replace the tiktoken adapter in the registry
        registry._adapters["tiktoken"] = (mock_tiktoken_class, {})
        
        try:
            # Should fall back to next adapter in chain
            adapter = registry.resolve(adapter_name="tiktoken", model_name="gpt-4")
            
            # Should get a working adapter (likely heuristic)
            assert adapter is not None
            assert adapter.adapter_id in ["huggingface", "heuristic"]
        finally:
            # Restore original adapters
            registry._adapters = original_adapters
    
    def test_graceful_degradation_on_adapter_failure(self):
        """Test graceful degradation when adapter creation fails."""
        registry = TokenizerRegistry()
        
        # Mock the adapter class in the registry's internal storage
        original_adapters = registry._adapters.copy()
        
        # Create a mock adapter class that raises ValueError
        def mock_tiktoken_class(*args, **kwargs):
            raise ValueError("Invalid encoding")
        
        # Replace the tiktoken adapter in the registry
        registry._adapters["tiktoken"] = (mock_tiktoken_class, {})
        
        try:
            # Should fall back to next adapter
            adapter = registry.resolve(adapter_name="tiktoken", model_name="gpt-4")
            
            assert adapter is not None
            assert adapter.adapter_id in ["huggingface", "heuristic"]
        finally:
            # Restore original adapters
            registry._adapters = original_adapters
    
    def test_fallback_chain_exhaustion(self):
        """Test behavior when all adapters in fallback chain fail."""
        registry = TokenizerRegistry()
        
        # Mock all adapter classes in the registry's internal storage
        original_adapters = registry._adapters.copy()
        
        # Create mock adapter classes that all fail
        def mock_heuristic_class(*args, **kwargs):
            raise RuntimeError("Heuristic failed")
        
        def mock_tiktoken_class(*args, **kwargs):
            raise ImportError("tiktoken not found")
        
        def mock_huggingface_class(*args, **kwargs):
            raise ImportError("transformers not found")
        
        # Replace all adapters in the registry
        registry._adapters["heuristic"] = (mock_heuristic_class, {})
        registry._adapters["tiktoken"] = (mock_tiktoken_class, {})
        registry._adapters["huggingface"] = (mock_huggingface_class, {})
        
        try:
            # Should raise FallbackChainExhaustedError
            with pytest.raises(FallbackChainExhaustedError) as exc_info:
                registry.resolve(adapter_name="tiktoken", model_name="gpt-4")
            
            error = exc_info.value
            assert "gpt-4" in str(error)
            assert len(error.attempted_adapters) > 0
            assert len(error.errors) > 0
        finally:
            # Restore original adapters
            registry._adapters = original_adapters
    
    def test_user_notification_creation(self):
        """Test user notification creation for fallback scenarios."""
        registry = TokenizerRegistry()
        
        strategy = {
            "user_message": "TikToken library not installed. Install with: pip install tiktoken",
            "technical_details": "ImportError: No module named 'tiktoken'",
            "retry_recommended": True
        }
        
        notification = registry.create_user_notification(
            "tiktoken", "heuristic", "gpt-4", strategy
        )
        
        assert "Tokenizer Adapter Fallback" in notification
        assert "tiktoken" in notification
        assert "heuristic" in notification
        assert "gpt-4" in notification
        assert "pip install tiktoken" in notification
        assert "approximate tokenization" in notification
    
    def test_recovery_statistics_tracking(self):
        """Test recovery statistics tracking."""
        registry = TokenizerRegistry()
        
        # Mock the adapter class to fail and trigger degradation
        original_adapters = registry._adapters.copy()
        
        def mock_tiktoken_class(*args, **kwargs):
            raise ImportError("tiktoken not found")
        
        registry._adapters["tiktoken"] = (mock_tiktoken_class, {})
        
        try:
            # This should trigger fallback and record statistics
            adapter = registry.resolve(adapter_name="tiktoken", model_name="gpt-4")
            
            stats = registry.get_recovery_statistics()
            assert stats["total_degradation_events"] >= 1
        finally:
            # Restore original adapters
            registry._adapters = original_adapters


class TestAdapterSpecificErrorHandling:
    """Test error handling specific to individual adapters."""
    
    def test_heuristic_adapter_error_handling(self):
        """Test HeuristicTokenizer error handling."""
        adapter = HeuristicTokenizer("test-model")
        
        # Test None input handling
        with pytest.raises(TokenizationFailedError):
            adapter.count_tokens(None)
        
        # Test invalid chars_per_token
        with pytest.raises(ValueError):
            HeuristicTokenizer("test-model", chars_per_token=-1.0)
    
    def test_tiktoken_adapter_dependency_error(self):
        """Test TiktokenAdapter dependency error handling."""
        with patch("kano_backlog_core.tokenizer.tiktoken", None):
            with pytest.raises(ImportError, match="tiktoken package required"):
                TiktokenAdapter("gpt-4")


class TestIntegrationErrorHandling:
    """Test end-to-end error handling scenarios."""
    
    def test_complete_fallback_chain_with_notifications(self):
        """Test complete fallback chain with proper user notifications."""
        registry = TokenizerRegistry()
        
        # Mock exact adapters to fail, leaving only heuristic
        original_adapters = registry._adapters.copy()
        
        def mock_tiktoken_class(*args, **kwargs):
            raise ImportError("tiktoken not found")
        
        def mock_huggingface_class(*args, **kwargs):
            raise ImportError("transformers not found")
        
        registry._adapters["tiktoken"] = (mock_tiktoken_class, {})
        registry._adapters["huggingface"] = (mock_huggingface_class, {})
        
        try:
            # Should successfully fall back to heuristic using the same registry
            adapter = registry.resolve(
                adapter_name="tiktoken",
                model_name="gpt-4"
            )
            
            assert adapter is not None
            assert adapter.adapter_id == "heuristic"
            
            # Test tokenization works
            result = adapter.count_tokens("Hello world")
            assert result.count > 0
            assert result.is_exact is False
        finally:
            # Restore original adapters
            registry._adapters = original_adapters
    
    def test_configuration_driven_error_handling(self):
        """Test error handling with configuration-driven adapter selection."""
        from kano_backlog_core.tokenizer_config import TokenizerConfig
        
        config = TokenizerConfig(
            adapter="tiktoken",
            model="gpt-4",
            fallback_chain=["tiktoken", "heuristic"]  # Skip huggingface
        )
        
        registry = TokenizerRegistry()
        registry.set_fallback_chain(config.fallback_chain)
        
        # Mock tiktoken to fail
        original_adapters = registry._adapters.copy()
        
        def mock_tiktoken_class(*args, **kwargs):
            raise ImportError("tiktoken not found")
        
        registry._adapters["tiktoken"] = (mock_tiktoken_class, {})
        
        try:
            # Should fall back to heuristic (skipping huggingface)
            adapter = registry.resolve(
                adapter_name=config.adapter,
                model_name=config.model
            )
            
            assert adapter.adapter_id == "heuristic"
        finally:
            # Restore original adapters
            registry._adapters = original_adapters


class TestErrorMessageQuality:
    """Test quality and usefulness of error messages."""
    
    def test_dependency_missing_error_message(self):
        """Test dependency missing error provides clear guidance."""
        error = DependencyMissingError("tiktoken", "tiktoken", "gpt-4")
        
        assert "tiktoken" in str(error)
        assert "pip install tiktoken" in error.recovery_suggestions[0]
        assert len(error.recovery_suggestions) > 1  # Multiple suggestions
    
    def test_fallback_chain_exhausted_error_message(self):
        """Test fallback chain exhausted error provides comprehensive guidance."""
        attempted = ["tiktoken", "huggingface", "heuristic"]
        errors = ["ImportError: tiktoken", "ImportError: transformers", "RuntimeError: failed"]
        
        error = FallbackChainExhaustedError(attempted, errors, "gpt-4")
        
        assert "gpt-4" in str(error)
        assert all(adapter in str(error) for adapter in attempted)
        assert len(error.recovery_suggestions) > 0
    
    def test_user_friendly_error_formatting(self):
        """Test user-friendly error message formatting."""
        from kano_backlog_core.tokenizer_errors import create_user_friendly_error_message
        
        error = DependencyMissingError("tiktoken", "tiktoken", "gpt-4")
        message = create_user_friendly_error_message(error)
        
        assert "❌ Tokenizer Error" in message
        assert "📍 Adapter: tiktoken" in message
        assert "🤖 Model: gpt-4" in message
        assert "💡 How to fix this:" in message
        assert "pip install tiktoken" in message


class TestPerformanceUnderErrors:
    """Test performance characteristics under error conditions."""
    
    def test_error_handling_performance(self):
        """Test that error handling doesn't significantly impact performance.

        Mocks adapters at the registry level (not module level) to avoid
        network I/O from HuggingFace's ``from_pretrained`` which can take
        seconds per call and causes flaky timing failures.  Only tiktoken
        and huggingface are mocked to fail; heuristic remains as the valid
        fallback so ``resolve()`` succeeds via the fallback chain.
        """
        import time

        registry = TokenizerRegistry()

        # Replace tiktoken and huggingface adapter factories in the registry
        # with instantly-failing callables.  This mirrors the pattern used by
        # the other tests in this module and avoids network/disk I/O that the
        # module-level ``patch()`` cannot prevent (the registry captured the
        # real class references at construction time).
        original_adapters = registry._adapters.copy()

        def mock_tiktoken_class(*args, **kwargs):
            raise ImportError("tiktoken not found")

        def mock_huggingface_class(*args, **kwargs):
            raise ImportError("transformers not found")

        registry._adapters["tiktoken"] = (mock_tiktoken_class, {})
        registry._adapters["huggingface"] = (mock_huggingface_class, {})

        try:
            start_time = time.time()

            # Multiple fallback attempts should still be fast
            for _ in range(5):
                try:
                    registry.resolve(adapter_name="tiktoken", model_name="gpt-4")
                except Exception:
                    pass

            elapsed = time.time() - start_time

            # Should complete quickly even with errors -- the fallback chain
            # (tiktoken fail -> huggingface fail -> heuristic succeed) must
            # not involve any network or heavy I/O.
            assert elapsed < 1.0, (
                f"Fallback chain took {elapsed:.2f}s for 5 iterations "
                f"(threshold 1.0s). Check for unexpected network or disk I/O "
                f"in the adapter creation path."
            )
        finally:
            registry._adapters = original_adapters
    
    def test_memory_usage_under_errors(self):
        """Test that error handling doesn't cause memory leaks."""
        import gc
        
        registry = TokenizerRegistry()

        # Mock at the registry level to avoid network I/O from
        # HuggingFaceAdapter.from_pretrained for unknown model names.
        original_adapters = registry._adapters.copy()

        def mock_tiktoken_class(*args, **kwargs):
            raise ImportError("tiktoken not found")

        def mock_huggingface_class(*args, **kwargs):
            raise ImportError("transformers not found")

        registry._adapters["tiktoken"] = (mock_tiktoken_class, {})
        registry._adapters["huggingface"] = (mock_huggingface_class, {})

        try:
            # Generate many errors
            for i in range(100):
                try:
                    registry.resolve(adapter_name="tiktoken", model_name=f"model-{i}")
                except Exception:
                    pass
            
            # Force garbage collection
            gc.collect()
            
            # Recovery manager should not accumulate unbounded data
            stats = registry.get_recovery_statistics()
            
            # Should have reasonable limits on stored data
            assert stats["active_recovery_keys"] < 200  # Some reasonable limit
            assert len(registry._error_recovery.degradation_history) < 50  # Reasonable history size
        finally:
            registry._adapters = original_adapters
