"""Performance and edge case integration tests for tokenizer-chunking pipeline.

This module provides performance-focused integration tests and edge case validation
for the tokenizer-chunking pipeline, complementing the main integration test suite.

Tests include:
- Performance benchmarks for different document sizes
- Memory usage validation
- Large document processing
- Edge cases and error conditions
- Stress testing with various configurations
"""

import pytest
import time
import gc
from typing import List, Dict, Any, Optional
from pathlib import Path

try:
    import psutil
    PSUTIL_AVAILABLE = True
except ImportError:
    PSUTIL_AVAILABLE = False
    psutil = None

from kano_backlog_core.tokenizer import (
    TokenizerRegistry,
    HeuristicTokenizer,
    TokenCount,
)
from kano_backlog_core.chunking import (
    ChunkingOptions,
    Chunk,
    chunk_text_with_tokenizer,
    normalize_text,
)
from kano_backlog_core.token_budget import (
    budget_chunks,
    TokenBudgetPolicy,
)


class TestPerformanceBenchmarks:
    """Performance benchmarks for different document sizes and configurations."""

    def test_small_document_performance(self):
        """Test performance with small documents (< 1KB)."""
        source_id = "small-perf-test"
        text = "This is a small test document for performance testing. " * 10  # ~500 chars
        
        options = ChunkingOptions(
            target_tokens=50,
            max_tokens=100,
            overlap_tokens=10,
            tokenizer_adapter="heuristic"
        )
        
        tokenizer = HeuristicTokenizer("small-perf-model", chars_per_token=4.0)
        
        # Measure performance
        start_time = time.time()
        chunks = chunk_text_with_tokenizer(source_id, text, options, tokenizer)
        end_time = time.time()
        
        processing_time = end_time - start_time
        
        # Performance assertions
        assert processing_time < 0.1, f"Small document processing took {processing_time:.3f}s (should be < 0.1s)"
        assert len(chunks) >= 1
        
        # Verify results are correct
        for chunk in chunks:
            token_count = tokenizer.count_tokens(chunk.text)
            assert token_count.count <= options.max_tokens

    def test_medium_document_performance(self):
        """Test performance with medium documents (1-10KB)."""
        source_id = "medium-perf-test"
        # Create ~5KB document
        paragraph = "This is a medium-sized test document for performance testing. " \
                   "It contains multiple paragraphs and sentences to simulate real-world content. " \
                   "The tokenizer and chunking pipeline should handle this efficiently. "
        text = (paragraph * 50) + "\n\n" + (paragraph * 50)  # ~5KB
        
        options = ChunkingOptions(
            target_tokens=100,
            max_tokens=200,
            overlap_tokens=20,
            tokenizer_adapter="heuristic"
        )
        
        tokenizer = HeuristicTokenizer("medium-perf-model", chars_per_token=4.0)
        
        # Measure performance
        start_time = time.time()
        chunks = chunk_text_with_tokenizer(source_id, text, options, tokenizer)
        end_time = time.time()
        
        processing_time = end_time - start_time
        
        # Performance assertions (should complete within 500ms for 5KB)
        assert processing_time < 0.5, f"Medium document processing took {processing_time:.3f}s (should be < 0.5s)"
        assert len(chunks) >= 1
        
        # Verify chunk quality
        total_chars = sum(len(chunk.text) for chunk in chunks)
        assert total_chars > 0
        
        for chunk in chunks:
            token_count = tokenizer.count_tokens(chunk.text)
            assert token_count.count <= options.max_tokens

    def test_large_document_performance(self):
        """Test performance with large documents (10-100KB)."""
        source_id = "large-perf-test"
        # Create ~50KB document
        section = """# Section Header

This is a large test document section for performance testing. It contains multiple
paragraphs, headers, and various formatting elements to simulate real-world documents.

## Subsection

The tokenizer and chunking pipeline should handle large documents efficiently while
maintaining accuracy and deterministic behavior. This section is repeated many times
to create a substantial document for testing.

Key requirements:
- Processing should complete within reasonable time limits
- Memory usage should scale linearly with document size
- All chunks should maintain quality and accuracy
- Deterministic behavior should be preserved

### Technical Details

The implementation uses efficient algorithms for:
1. Text normalization and Unicode handling
2. Boundary detection with hierarchical fallback
3. Token-aware chunking with budget management
4. Stable chunk ID generation

This content is designed to test various aspects of the chunking pipeline including
paragraph boundaries, list processing, and mixed content handling.

"""
        text = section * 100  # ~50KB
        
        options = ChunkingOptions(
            target_tokens=200,
            max_tokens=400,
            overlap_tokens=40,
            tokenizer_adapter="heuristic"
        )
        
        tokenizer = HeuristicTokenizer("large-perf-model", chars_per_token=4.0)
        
        # Measure performance and memory (if psutil available)
        memory_before = memory_after = 0
        if PSUTIL_AVAILABLE:
            import os
            process = psutil.Process(os.getpid())
            memory_before = process.memory_info().rss / 1024 / 1024  # MB
        
        start_time = time.time()
        chunks = chunk_text_with_tokenizer(source_id, text, options, tokenizer)
        end_time = time.time()
        
        if PSUTIL_AVAILABLE:
            memory_after = process.memory_info().rss / 1024 / 1024  # MB
            memory_used = memory_after - memory_before
        else:
            memory_used = 0  # Skip memory checks if psutil not available
        
        processing_time = end_time - start_time
        
        # Performance assertions (should complete within 5s for 50KB)
        assert processing_time < 5.0, f"Large document processing took {processing_time:.3f}s (should be < 5.0s)"
        assert len(chunks) >= 10, "Large document should produce multiple chunks"
        
        # Memory usage should be reasonable (< 100MB for 50KB document) - only check if psutil available
        if PSUTIL_AVAILABLE:
            assert memory_used < 100, f"Memory usage {memory_used:.1f}MB is excessive for 50KB document"
        
        # Verify chunk quality
        for chunk in chunks:
            token_count = tokenizer.count_tokens(chunk.text)
            assert token_count.count <= options.max_tokens
            assert len(chunk.text) > 0

    def test_performance_with_different_configurations(self):
        """Test performance impact of different chunking configurations."""
        source_id = "config-perf-test"
        text = "Performance test document with various configurations. " * 200  # ~10KB
        
        configurations = [
            # Small chunks, high overlap
            {"target_tokens": 25, "max_tokens": 50, "overlap_tokens": 15},
            # Medium chunks, medium overlap  
            {"target_tokens": 100, "max_tokens": 200, "overlap_tokens": 30},
            # Large chunks, low overlap
            {"target_tokens": 300, "max_tokens": 500, "overlap_tokens": 25},
        ]
        
        tokenizer = HeuristicTokenizer("config-perf-model", chars_per_token=4.0)
        
        performance_results = []
        
        for config in configurations:
            options = ChunkingOptions(
                target_tokens=config["target_tokens"],
                max_tokens=config["max_tokens"],
                overlap_tokens=config["overlap_tokens"],
                tokenizer_adapter="heuristic"
            )
            
            # Measure performance
            start_time = time.time()
            chunks = chunk_text_with_tokenizer(source_id, text, options, tokenizer)
            end_time = time.time()
            
            processing_time = end_time - start_time
            performance_results.append({
                "config": config,
                "time": processing_time,
                "chunks": len(chunks)
            })
            
            # Verify results
            assert len(chunks) >= 1
            for chunk in chunks:
                token_count = tokenizer.count_tokens(chunk.text)
                assert token_count.count <= options.max_tokens
        
        # All configurations should complete within reasonable time
        for result in performance_results:
            assert result["time"] < 2.0, \
                f"Configuration {result['config']} took {result['time']:.3f}s (should be < 2.0s)"

    def test_tokenizer_performance_comparison(self):
        """Test performance comparison between different tokenizer configurations."""
        source_id = "tokenizer-perf-test"
        text = "Tokenizer performance comparison test document. " * 100  # ~5KB
        
        options = ChunkingOptions(
            target_tokens=100,
            max_tokens=200,
            overlap_tokens=20,
            tokenizer_adapter="heuristic"
        )
        
        # Different tokenizer configurations
        tokenizers = [
            HeuristicTokenizer("perf-model-1", chars_per_token=3.0),
            HeuristicTokenizer("perf-model-2", chars_per_token=4.0),
            HeuristicTokenizer("perf-model-3", chars_per_token=5.0),
        ]
        
        performance_results = []
        
        for tokenizer in tokenizers:
            # Measure performance
            start_time = time.time()
            chunks = chunk_text_with_tokenizer(source_id, text, options, tokenizer)
            end_time = time.time()
            
            processing_time = end_time - start_time
            performance_results.append({
                "tokenizer": tokenizer.chars_per_token,
                "time": processing_time,
                "chunks": len(chunks)
            })
            
            # Verify results
            assert len(chunks) >= 1
        
        # All tokenizers should have similar performance
        max_time = max(result["time"] for result in performance_results)
        assert max_time < 1.0, f"Slowest tokenizer took {max_time:.3f}s (should be < 1.0s)"


class TestMemoryUsageValidation:
    """Test memory usage patterns and validate linear scaling."""

    def test_memory_scaling_with_document_size(self):
        """Test that memory usage scales linearly with document size."""
        if not PSUTIL_AVAILABLE:
            pytest.skip("psutil not available for memory testing")
            
        import os
        source_id = "memory-scaling-test"
        base_text = "Memory scaling test content with various elements. " * 10
        
        options = ChunkingOptions(
            target_tokens=100,
            max_tokens=200,
            overlap_tokens=20,
            tokenizer_adapter="heuristic"
        )
        
        tokenizer = HeuristicTokenizer("memory-test-model", chars_per_token=4.0)
        
        document_sizes = [1, 5, 10, 20]  # Multipliers for base text
        memory_usage = []
        
        for size_multiplier in document_sizes:
            text = base_text * size_multiplier
            
            # Force garbage collection before measurement
            gc.collect()
            
            process = psutil.Process(os.getpid())
            memory_before = process.memory_info().rss / 1024 / 1024  # MB
            
            chunks = chunk_text_with_tokenizer(source_id, text, options, tokenizer)
            
            memory_after = process.memory_info().rss / 1024 / 1024  # MB
            memory_used = memory_after - memory_before
            
            memory_usage.append({
                "size_multiplier": size_multiplier,
                "memory_mb": memory_used,
                "chunks": len(chunks)
            })
            
            # Verify results
            assert len(chunks) >= 1
        
        # Memory usage should not grow excessively
        max_memory = max(result["memory_mb"] for result in memory_usage)
        assert max_memory < 50, f"Maximum memory usage {max_memory:.1f}MB is excessive"
        
        # Memory usage should be roughly proportional to document size
        # (allowing for some overhead and variation)
        if len(memory_usage) >= 2:
            ratio_1_to_2 = memory_usage[1]["memory_mb"] / max(memory_usage[0]["memory_mb"], 0.1)
            assert ratio_1_to_2 < 10, "Memory usage scaling is not reasonable"

    def test_memory_cleanup_after_processing(self):
        """Test that memory is properly cleaned up after processing."""
        if not PSUTIL_AVAILABLE:
            pytest.skip("psutil not available for memory testing")
            
        import os
        source_id = "memory-cleanup-test"
        text = "Memory cleanup test document. " * 1000  # ~25KB
        
        options = ChunkingOptions(
            target_tokens=150,
            max_tokens=300,
            overlap_tokens=30,
            tokenizer_adapter="heuristic"
        )
        
        tokenizer = HeuristicTokenizer("cleanup-test-model", chars_per_token=4.0)
        
        # Measure memory before processing
        gc.collect()
        process = psutil.Process(os.getpid())
        memory_before = process.memory_info().rss / 1024 / 1024  # MB
        
        # Process document
        chunks = chunk_text_with_tokenizer(source_id, text, options, tokenizer)
        
        # Measure memory during processing
        memory_during = process.memory_info().rss / 1024 / 1024  # MB
        
        # Clear references and force garbage collection
        del chunks
        gc.collect()
        
        # Measure memory after cleanup
        memory_after = process.memory_info().rss / 1024 / 1024  # MB
        
        # Memory should not grow excessively during processing
        memory_growth = memory_during - memory_before
        assert memory_growth < 100, f"Memory growth during processing {memory_growth:.1f}MB is excessive"
        
        # Memory should be mostly cleaned up after processing
        memory_retained = memory_after - memory_before
        if memory_growth >= 1.0:
            assert memory_retained < memory_growth * 0.5, "Too much memory retained after cleanup"


class TestEdgeCasesAndErrorConditions:
    """Test edge cases and error conditions in the integration pipeline."""

    def test_empty_and_whitespace_documents(self):
        """Test handling of empty and whitespace-only documents."""
        source_id = "empty-test"
        
        options = ChunkingOptions(
            target_tokens=50,
            max_tokens=100,
            overlap_tokens=10,
            tokenizer_adapter="heuristic"
        )
        
        tokenizer = HeuristicTokenizer("empty-test-model", chars_per_token=4.0)
        
        test_cases = [
            "",  # Empty string
            "   ",  # Spaces only
            "\n\n\n",  # Newlines only
            "\t\t\t",  # Tabs only
            " \n \t \n ",  # Mixed whitespace
        ]
        
        for text in test_cases:
            chunks = chunk_text_with_tokenizer(source_id, text, options, tokenizer)
            
            # Empty/whitespace documents should produce no chunks
            assert len(chunks) == 0, f"Empty/whitespace text '{repr(text)}' should produce no chunks"

    def test_single_character_documents(self):
        """Test handling of single character documents."""
        source_id = "single-char-test"
        
        options = ChunkingOptions(
            target_tokens=10,
            max_tokens=20,
            overlap_tokens=5,
            tokenizer_adapter="heuristic"
        )
        
        tokenizer = HeuristicTokenizer("single-char-model", chars_per_token=4.0)
        
        test_cases = ["a", "1", "!", "中", "🙂"]
        
        for text in test_cases:
            chunks = chunk_text_with_tokenizer(source_id, text, options, tokenizer)
            
            # Should produce exactly one chunk
            assert len(chunks) == 1, f"Single character '{text}' should produce one chunk"
            
            chunk = chunks[0]
            assert chunk.text == text
            assert chunk.start_char == 0
            assert chunk.end_char == len(text)
            
            # Token count should be reasonable
            token_count = tokenizer.count_tokens(chunk.text)
            assert token_count.count > 0

    def test_very_long_single_sentences(self):
        """Test handling of very long single sentences without paragraph breaks."""
        source_id = "long-sentence-test"
        
        # Create a very long single sentence
        text = "This is an extremely long single sentence that contains many words and phrases " \
               "but no paragraph breaks or sentence endings to test the hard cut behavior " \
               "of the chunking algorithm when no good boundaries are available and the " \
               "tokenizer must resort to hard cuts to maintain token budget compliance " \
               "while ensuring forward progress is always made even in difficult cases " \
               "like this one where natural boundaries are not available for chunking."
        
        options = ChunkingOptions(
            target_tokens=20,  # Small chunks to force hard cuts
            max_tokens=40,
            overlap_tokens=5,
            tokenizer_adapter="heuristic"
        )
        
        tokenizer = HeuristicTokenizer("long-sentence-model", chars_per_token=4.0)
        
        chunks = chunk_text_with_tokenizer(source_id, text, options, tokenizer)
        
        # Should produce multiple chunks due to length
        assert len(chunks) > 1, "Long sentence should be split into multiple chunks"
        
        # Verify all chunks respect token budget
        for chunk in chunks:
            token_count = tokenizer.count_tokens(chunk.text)
            assert token_count.count <= options.max_tokens
            assert len(chunk.text) > 0  # Progress guarantee

    def test_documents_with_unusual_unicode(self):
        """Test handling of documents with unusual Unicode characters."""
        source_id = "unicode-edge-test"
        
        # Text with various Unicode edge cases
        text = """Unicode Edge Cases Test

Emoji: 🌟 🚀 💻 🎉 🔥 ⭐ 🌈 🎯
Mathematical symbols: ∑ ∫ ∞ ≈ ≠ ≤ ≥ ± √ ∂
Currency: $ € £ ¥ ₹ ₽ ₿ ¢
Arrows: → ← ↑ ↓ ↔ ⇒ ⇐ ⇔
Greek letters: α β γ δ ε ζ η θ ι κ λ μ ν ξ ο π ρ σ τ υ φ χ ψ ω
Superscripts: x² y³ z⁴ a⁵ b⁶
Subscripts: H₂O CO₂ CH₄
Fractions: ½ ⅓ ¼ ⅕ ⅙ ⅛
Roman numerals: Ⅰ Ⅱ Ⅲ Ⅳ Ⅴ Ⅵ Ⅶ Ⅷ Ⅸ Ⅹ
Diacritics: àáâãäåæçèéêëìíîïðñòóôõöøùúûüýþÿ
Combining characters: e̊ a̋ o̧ u̇ i̋
Zero-width characters: a‌b c‍d e​f
Right-to-left: العربية עברית
"""
        
        options = ChunkingOptions(
            target_tokens=50,
            max_tokens=100,
            overlap_tokens=10,
            tokenizer_adapter="heuristic"
        )
        
        tokenizer = HeuristicTokenizer("unicode-edge-model", chars_per_token=4.0)
        
        chunks = chunk_text_with_tokenizer(source_id, text, options, tokenizer)
        
        # Should handle Unicode characters without errors
        assert len(chunks) >= 1
        
        for chunk in chunks:
            # Verify chunk is valid
            assert len(chunk.text) > 0
            assert chunk.start_char >= 0
            assert chunk.end_char > chunk.start_char
            
            # Verify tokenization works with Unicode
            token_count = tokenizer.count_tokens(chunk.text)
            assert token_count.count > 0
            assert token_count.count <= options.max_tokens

    def test_malformed_input_handling(self):
        """Test handling of malformed or problematic input."""
        source_id = "malformed-test"
        
        options = ChunkingOptions(
            target_tokens=30,
            max_tokens=60,
            overlap_tokens=10,
            tokenizer_adapter="heuristic"
        )
        
        tokenizer = HeuristicTokenizer("malformed-model", chars_per_token=4.0)
        
        # Test with None input (should raise appropriate error)
        with pytest.raises((ValueError, TypeError)):
            chunk_text_with_tokenizer(source_id, None, options, tokenizer)
        
        # Test with non-string input (should raise appropriate error)
        with pytest.raises((ValueError, TypeError)):
            chunk_text_with_tokenizer(source_id, 123, options, tokenizer)
        
        # Test with empty source_id (should raise appropriate error)
        with pytest.raises(ValueError):
            chunk_text_with_tokenizer("", "test text", options, tokenizer)

    def test_extreme_configuration_values(self):
        """Test behavior with extreme configuration values."""
        source_id = "extreme-config-test"
        text = "Test document for extreme configuration values."
        
        tokenizer = HeuristicTokenizer("extreme-config-model", chars_per_token=4.0)
        
        # Test with maximum reasonable values
        extreme_options = ChunkingOptions(
            target_tokens=10000,
            max_tokens=20000,
            overlap_tokens=5000,
            tokenizer_adapter="heuristic"
        )
        
        chunks = chunk_text_with_tokenizer(source_id, text, options=extreme_options, tokenizer=tokenizer)
        
        # Should handle extreme values gracefully
        assert len(chunks) >= 1
        for chunk in chunks:
            assert len(chunk.text) > 0
            token_count = tokenizer.count_tokens(chunk.text)
            assert token_count.count <= extreme_options.max_tokens


class TestStressTesting:
    """Stress testing with various challenging scenarios."""

    def test_concurrent_processing_simulation(self):
        """Test behavior under simulated concurrent processing load."""
        source_id = "concurrent-test"
        text = "Concurrent processing test document. " * 100
        
        options = ChunkingOptions(
            target_tokens=75,
            max_tokens=150,
            overlap_tokens=15,
            tokenizer_adapter="heuristic"
        )
        
        tokenizer = HeuristicTokenizer("concurrent-model", chars_per_token=4.0)
        
        # Simulate concurrent processing by running multiple times rapidly
        results = []
        for i in range(10):
            chunks = chunk_text_with_tokenizer(f"{source_id}-{i}", text, options, tokenizer)
            results.append(chunks)
        
        # All results should be valid and consistent
        for i, chunks in enumerate(results):
            assert len(chunks) >= 1, f"Run {i} produced no chunks"
            
            for chunk in chunks:
                assert len(chunk.text) > 0
                token_count = tokenizer.count_tokens(chunk.text)
                assert token_count.count <= options.max_tokens

    def test_repeated_processing_stability(self):
        """Test stability under repeated processing of the same document."""
        source_id = "stability-test"
        text = "Stability test document for repeated processing validation. " * 50
        
        options = ChunkingOptions(
            target_tokens=80,
            max_tokens=160,
            overlap_tokens=16,
            tokenizer_adapter="heuristic"
        )
        
        tokenizer = HeuristicTokenizer("stability-model", chars_per_token=4.0)
        
        # Process the same document many times
        first_result = None
        for iteration in range(20):
            chunks = chunk_text_with_tokenizer(source_id, text, options, tokenizer)
            
            if first_result is None:
                first_result = chunks
            else:
                # Results should be identical across iterations
                assert len(chunks) == len(first_result), \
                    f"Iteration {iteration} produced different number of chunks"
                
                for i, (chunk, first_chunk) in enumerate(zip(chunks, first_result)):
                    assert chunk.chunk_id == first_chunk.chunk_id, \
                        f"Iteration {iteration}, chunk {i} has different ID"
                    assert chunk.text == first_chunk.text, \
                        f"Iteration {iteration}, chunk {i} has different text"

    def test_resource_exhaustion_resilience(self):
        """Test resilience under simulated resource constraints."""
        source_id = "resource-test"
        
        # Create a moderately large document
        text = "Resource exhaustion test content. " * 500  # ~15KB
        
        options = ChunkingOptions(
            target_tokens=50,
            max_tokens=100,
            overlap_tokens=10,
            tokenizer_adapter="heuristic"
        )
        
        tokenizer = HeuristicTokenizer("resource-model", chars_per_token=4.0)
        
        # Process with time constraints
        start_time = time.time()
        chunks = chunk_text_with_tokenizer(source_id, text, options, tokenizer)
        end_time = time.time()
        
        processing_time = end_time - start_time
        
        # Should complete within reasonable time even for larger documents
        assert processing_time < 10.0, f"Processing took {processing_time:.3f}s (should be < 10.0s)"
        
        # Results should still be valid
        assert len(chunks) >= 1
        for chunk in chunks:
            token_count = tokenizer.count_tokens(chunk.text)
            assert token_count.count <= options.max_tokens
            assert len(chunk.text) > 0


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
