"""
Property-based tests for workset operations.

Feature: workset-topic-context
Task: 2.3 Write property tests for workset init
Requirements: 1.1-1.8

This module tests the workset init functionality using property-based testing
with Hypothesis to verify correctness properties.
"""

import json
import pytest
import tempfile
import shutil
from datetime import datetime, timezone
from pathlib import Path
from hypothesis import given, strategies as st, settings, assume, HealthCheck

import sys
test_dir = Path(__file__).parent
src_dir = test_dir.parent / "src"
sys.path.insert(0, str(src_dir))

from kano_backlog_ops.workset import (
    init_workset,
    WorksetInitResult,
    WorksetMetadata,
    ItemNotFoundError,
    get_item_workset_path,
)


# =============================================================================
# Test Helpers
# =============================================================================


def create_temp_backlog() -> Path:
    """Create a temporary backlog structure for testing."""
    temp_dir = Path(tempfile.mkdtemp())
    backlog_root = temp_dir / "_kano" / "backlog"
    
    # Create product structure
    product_dir = backlog_root / "products" / "test-product"
    items_dir = product_dir / "items" / "task" / "0000"
    items_dir.mkdir(parents=True, exist_ok=True)
    
    # Create config
    config_dir = product_dir / "_config"
    config_dir.mkdir(parents=True, exist_ok=True)
    config_content = {
        "project": {"name": "test-product", "prefix": "TST"}
    }
    (config_dir / "config.json").write_text(json.dumps(config_content))
    
    return backlog_root


def cleanup_temp_backlog(backlog_root: Path) -> None:
    """Clean up temporary backlog."""
    # Go up to temp dir root
    temp_dir = backlog_root.parent.parent
    shutil.rmtree(temp_dir, ignore_errors=True)


def create_test_item(backlog_root: Path, item_id: str, title: str = "Test Item") -> Path:
    """Create a test item file with proper frontmatter."""
    product_dir = backlog_root / "products" / "test-product"
    items_dir = product_dir / "items" / "task" / "0000"
    items_dir.mkdir(parents=True, exist_ok=True)
    
    item_path = items_dir / f"{item_id}_test-item.md"
    content = f"""---
id: {item_id}
uid: 019b9c80-0000-7000-8000-000000000001
type: Task
title: "{title}"
state: Proposed
priority: P2
parent: null
area: testing
iteration: backlog
tags: []
created: 2026-01-12
updated: 2026-01-12
owner: null
external:
  azure_id: null
  jira_key: null
links:
  relates: []
  blocks: []
  blocked_by: []
decisions: []
---

# Context

Test context for {item_id}

# Goal

Test goal

# Approach

Test approach

# Acceptance Criteria

- First acceptance criterion
- Second acceptance criterion
- Third acceptance criterion

# Risks / Dependencies

None

# Worklog

2026-01-12 10:00 [agent=test-agent] Created item
"""
    item_path.write_text(content, encoding="utf-8")
    return item_path


# =============================================================================
# Pytest Fixture for Unit Tests
# =============================================================================


@pytest.fixture
def temp_backlog():
    """Create a temporary backlog structure for testing."""
    backlog_root = create_temp_backlog()
    yield backlog_root
    cleanup_temp_backlog(backlog_root)


# =============================================================================
# Property 1: Workset init creates complete structure
# Feature: workset-topic-context, Property 1: Workset init creates complete structure
# Validates: Requirements 1.1, 1.2, 1.3, 1.4, 1.5
# =============================================================================


@settings(max_examples=100, deadline=None, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(
    item_suffix=st.integers(min_value=1, max_value=9999),
    agent_name=st.text(
        alphabet=st.characters(whitelist_categories=('L', 'N'), whitelist_characters='-_'),
        min_size=1,
        max_size=20
    ).filter(lambda x: x.strip() and len(x) > 0 and x[0].isalpha()),
    ttl_hours=st.integers(min_value=1, max_value=720),
)
def test_workset_init_creates_complete_structure(item_suffix, agent_name, ttl_hours):
    """
    Property 1: Workset init creates complete structure
    
    For any valid backlog item, initializing a workset SHALL create a directory
    containing meta.json, plan.md, notes.md, and an empty deliverables/ directory,
    where meta.json contains all required fields.
    
    **Validates: Requirements 1.1, 1.2, 1.3, 1.4, 1.5**
    """
    backlog_root = create_temp_backlog()
    try:
        # Create a test item
        item_id = f"TST-TSK-{item_suffix:04d}"
        create_test_item(backlog_root, item_id)
        
        # Initialize workset (without worklog to avoid modifying source during property test)
        result = init_workset(
            item_id,
            agent=agent_name,
            backlog_root=backlog_root,
            ttl_hours=ttl_hours,
            append_worklog=False,
        )
        
        # Verify result type
        assert isinstance(result, WorksetInitResult)
        assert result.created is True
        assert result.item_count == 1
        
        # Verify workset directory exists at correct location (Requirement 1.1)
        workset_path = result.workset_path
        assert workset_path.exists()
        assert workset_path.is_dir()
        expected_path = backlog_root / ".cache" / "worksets" / "items" / item_id
        assert workset_path == expected_path
        
        # Verify meta.json exists and contains required fields (Requirement 1.2)
        meta_path = workset_path / "meta.json"
        assert meta_path.exists()
        
        with open(meta_path, "r", encoding="utf-8") as f:
            meta_data = json.load(f)
        
        required_fields = [
            "workset_id", "item_id", "item_uid", "item_path",
            "agent", "created_at", "refreshed_at", "ttl_hours"
        ]
        for field in required_fields:
            assert field in meta_data, f"Missing required field: {field}"
        
        assert meta_data["item_id"] == item_id
        assert meta_data["agent"] == agent_name
        assert meta_data["ttl_hours"] == ttl_hours
        
        # Verify plan.md exists (Requirement 1.3)
        plan_path = workset_path / "plan.md"
        assert plan_path.exists()
        plan_content = plan_path.read_text(encoding="utf-8")
        assert "# Execution Plan" in plan_content
        assert item_id in plan_content
        
        # Verify notes.md exists with Decision: marker guidance (Requirement 1.4)
        notes_path = workset_path / "notes.md"
        assert notes_path.exists()
        notes_content = notes_path.read_text(encoding="utf-8")
        assert "Decision:" in notes_content
        
        # Verify deliverables/ directory exists and is empty (Requirement 1.5)
        deliverables_path = workset_path / "deliverables"
        assert deliverables_path.exists()
        assert deliverables_path.is_dir()
        assert list(deliverables_path.iterdir()) == []
    finally:
        cleanup_temp_backlog(backlog_root)


# =============================================================================
# Property 3: Workset init handles invalid items
# Feature: workset-topic-context, Property 3: Workset init handles invalid items
# Validates: Requirements 1.7
# =============================================================================


@settings(max_examples=100, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(
    invalid_ref=st.text(
        alphabet=st.characters(whitelist_categories=('L', 'N'), whitelist_characters='-_'),
        min_size=5,
        max_size=30
    ).filter(lambda x: x.strip() and not x.startswith("TST-")),
)
def test_workset_init_handles_invalid_items(invalid_ref):
    """
    Property 3: Workset init handles invalid items
    
    For any invalid item reference (non-existent ID, malformed reference),
    initializing a workset SHALL return an error without creating any files
    or directories.
    
    **Validates: Requirements 1.7**
    """
    # Ensure the reference doesn't accidentally match a real item
    assume(not invalid_ref.endswith(".md"))
    
    backlog_root = create_temp_backlog()
    try:
        # Get the workset cache path before attempting init
        cache_root = backlog_root / ".cache" / "worksets" / "items"
        
        # Count existing directories before
        if cache_root.exists():
            dirs_before = set(p.name for p in cache_root.iterdir() if p.is_dir())
        else:
            dirs_before = set()
        
        # Attempt to initialize workset with invalid reference
        with pytest.raises(ItemNotFoundError) as exc_info:
            init_workset(
                invalid_ref,
                agent="test-agent",
                backlog_root=backlog_root,
                append_worklog=False,
            )
        
        # Verify error contains the invalid reference
        assert invalid_ref in str(exc_info.value)
        
        # Verify no new directories were created
        if cache_root.exists():
            dirs_after = set(p.name for p in cache_root.iterdir() if p.is_dir())
        else:
            dirs_after = set()
        
        assert dirs_before == dirs_after, "No new directories should be created for invalid items"
    finally:
        cleanup_temp_backlog(backlog_root)


# =============================================================================
# Property 4: Workset init is idempotent
# Feature: workset-topic-context, Property 4: Workset init is idempotent
# Validates: Requirements 1.8
# =============================================================================


@settings(max_examples=100, deadline=None, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(
    item_suffix=st.integers(min_value=1, max_value=9999),
    agent_name=st.text(
        alphabet=st.characters(whitelist_categories=('L', 'N'), whitelist_characters='-_'),
        min_size=1,
        max_size=20
    ).filter(lambda x: x.strip() and len(x) > 0 and x[0].isalpha()),
)
def test_workset_init_is_idempotent(item_suffix, agent_name):
    """
    Property 4: Workset init is idempotent
    
    For any valid backlog item, initializing a workset twice SHALL return
    the same workset path and not create duplicate directories or files.
    
    **Validates: Requirements 1.8**
    """
    backlog_root = create_temp_backlog()
    try:
        # Create a test item
        item_id = f"TST-TSK-{item_suffix:04d}"
        create_test_item(backlog_root, item_id)
        
        # First initialization
        result1 = init_workset(
            item_id,
            agent=agent_name,
            backlog_root=backlog_root,
            append_worklog=False,
        )
        
        assert result1.created is True
        
        # Record state after first init
        meta_path = result1.workset_path / "meta.json"
        with open(meta_path, "r", encoding="utf-8") as f:
            meta_after_first = json.load(f)
        
        # Second initialization (should be idempotent)
        result2 = init_workset(
            item_id,
            agent=agent_name,
            backlog_root=backlog_root,
            append_worklog=False,
        )
        
        # Verify same path returned
        assert result1.workset_path == result2.workset_path
        
        # Verify second call indicates existing (not newly created)
        assert result2.created is False
        
        # Verify metadata was not modified
        with open(meta_path, "r", encoding="utf-8") as f:
            meta_after_second = json.load(f)
        
        assert meta_after_first == meta_after_second, "Metadata should not change on second init"
        
        # Verify only one workset directory exists for this item
        cache_root = backlog_root / ".cache" / "worksets" / "items"
        matching_dirs = [p for p in cache_root.iterdir() if p.name == item_id]
        assert len(matching_dirs) == 1, "Should have exactly one workset directory"
    finally:
        cleanup_temp_backlog(backlog_root)


# =============================================================================
# Additional Unit Tests for Edge Cases
# =============================================================================


def test_workset_init_with_acceptance_criteria(temp_backlog):
    """Test that plan.md is generated from item's acceptance criteria."""
    item_id = "TST-TSK-0001"
    create_test_item(temp_backlog, item_id)
    
    result = init_workset(
        item_id,
        agent="test-agent",
        backlog_root=temp_backlog,
        append_worklog=False,
    )
    
    plan_content = (result.workset_path / "plan.md").read_text(encoding="utf-8")
    
    # Should contain checklist items derived from acceptance criteria
    assert "- [ ]" in plan_content
    assert "Step" in plan_content


def test_workset_init_appends_worklog(temp_backlog):
    """Test that worklog entry is appended to source item when enabled."""
    item_id = "TST-TSK-0002"
    item_path = create_test_item(temp_backlog, item_id)
    
    # Read original content
    original_content = item_path.read_text(encoding="utf-8")
    original_worklog_count = original_content.count("[agent=")
    
    # Initialize with worklog enabled
    result = init_workset(
        item_id,
        agent="worklog-test-agent",
        backlog_root=temp_backlog,
        append_worklog=True,
    )
    
    # Read updated content
    updated_content = item_path.read_text(encoding="utf-8")
    updated_worklog_count = updated_content.count("[agent=")
    
    # Verify worklog entry was added
    assert updated_worklog_count == original_worklog_count + 1
    assert "Workset initialized" in updated_content
    assert "worklog-test-agent" in updated_content


def test_workset_init_metadata_structure(temp_backlog):
    """Test that meta.json has correct structure per design spec."""
    item_id = "TST-TSK-0003"
    create_test_item(temp_backlog, item_id)
    
    result = init_workset(
        item_id,
        agent="meta-test-agent",
        backlog_root=temp_backlog,
        ttl_hours=48,
        append_worklog=False,
    )
    
    # Load and verify metadata
    metadata = WorksetMetadata.load(result.workset_path / "meta.json")
    
    assert metadata.item_id == item_id
    assert metadata.agent == "meta-test-agent"
    assert metadata.ttl_hours == 48
    assert metadata.workset_id  # Should have a UUID
    assert metadata.created_at  # Should have timestamp
    assert metadata.refreshed_at  # Should have timestamp
    assert metadata.item_path  # Should have path


# =============================================================================
# Import additional functions for refresh and next tests
# =============================================================================

from kano_backlog_ops.workset import (
    refresh_workset,
    get_next_action,
    WorksetRefreshResult,
    WorksetNextResult,
    WorksetNotFoundError,
    WorksetError,
)


# =============================================================================
# Property 5: Workset refresh updates metadata
# Feature: workset-topic-context, Property 5: Workset refresh updates metadata
# Validates: Requirements 2.1, 2.3
# =============================================================================


@settings(max_examples=100, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(
    item_suffix=st.integers(min_value=1, max_value=9999),
    agent_name=st.text(
        alphabet=st.characters(whitelist_categories=('L', 'N'), whitelist_characters='-_'),
        min_size=1,
        max_size=20
    ).filter(lambda x: x.strip() and len(x) > 0 and x[0].isalpha()),
)
def test_workset_refresh_updates_metadata(item_suffix, agent_name):
    """
    Property 5: Workset refresh updates metadata
    
    For any existing workset, refreshing SHALL update meta.json's refreshed_at
    timestamp to a value greater than or equal to the previous value, and SHALL
    append a worklog entry to the source item.
    
    **Validates: Requirements 2.1, 2.3**
    """
    backlog_root = create_temp_backlog()
    try:
        # Create a test item
        item_id = f"TST-TSK-{item_suffix:04d}"
        item_path = create_test_item(backlog_root, item_id)
        
        # Initialize workset first
        init_result = init_workset(
            item_id,
            agent=agent_name,
            backlog_root=backlog_root,
            append_worklog=False,
        )
        
        # Load metadata before refresh
        meta_path = init_result.workset_path / "meta.json"
        with open(meta_path, "r", encoding="utf-8") as f:
            meta_before = json.load(f)
        
        refreshed_at_before = meta_before["refreshed_at"]
        
        # Read item content before refresh
        item_content_before = item_path.read_text(encoding="utf-8")
        worklog_count_before = item_content_before.count("[agent=")
        
        # Refresh workset
        refresh_result = refresh_workset(
            item_id,
            agent=agent_name,
            backlog_root=backlog_root,
            append_worklog=True,
        )
        
        # Verify result type
        assert isinstance(refresh_result, WorksetRefreshResult)
        assert refresh_result.workset_path == init_result.workset_path
        
        # Load metadata after refresh
        with open(meta_path, "r", encoding="utf-8") as f:
            meta_after = json.load(f)
        
        refreshed_at_after = meta_after["refreshed_at"]
        
        # Verify refreshed_at was updated (Requirement 2.1)
        assert refreshed_at_after >= refreshed_at_before, \
            "refreshed_at should be updated to current or later timestamp"
        
        # Verify worklog entry was appended (Requirement 2.3)
        item_content_after = item_path.read_text(encoding="utf-8")
        worklog_count_after = item_content_after.count("[agent=")
        
        assert worklog_count_after == worklog_count_before + 1, \
            "Worklog entry should be appended"
        assert "Workset refreshed" in item_content_after, \
            "Worklog should contain 'Workset refreshed'"
    finally:
        cleanup_temp_backlog(backlog_root)


# =============================================================================
# Property 7: Workset refresh requires existing workset
# Feature: workset-topic-context, Property 7: Workset refresh requires existing workset
# Validates: Requirements 2.4
# =============================================================================


@settings(max_examples=100, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(
    item_suffix=st.integers(min_value=1, max_value=9999),
    agent_name=st.text(
        alphabet=st.characters(whitelist_categories=('L', 'N'), whitelist_characters='-_'),
        min_size=1,
        max_size=20
    ).filter(lambda x: x.strip() and len(x) > 0 and x[0].isalpha()),
)
def test_workset_refresh_requires_existing_workset(item_suffix, agent_name):
    """
    Property 7: Workset refresh requires existing workset
    
    For any item without an existing workset, refreshing SHALL return an error
    suggesting workset init.
    
    **Validates: Requirements 2.4**
    """
    backlog_root = create_temp_backlog()
    try:
        # Create a test item but DO NOT initialize workset
        item_id = f"TST-TSK-{item_suffix:04d}"
        create_test_item(backlog_root, item_id)
        
        # Attempt to refresh without init
        with pytest.raises(WorksetNotFoundError) as exc_info:
            refresh_workset(
                item_id,
                agent=agent_name,
                backlog_root=backlog_root,
            )
        
        # Verify error message suggests init
        assert "workset init" in str(exc_info.value.suggestion).lower(), \
            "Error should suggest running workset init"
    finally:
        cleanup_temp_backlog(backlog_root)


# =============================================================================
# Property 8: Workset next returns correct step
# Feature: workset-topic-context, Property 8: Workset next returns correct step
# Validates: Requirements 3.1, 3.2, 3.4
# =============================================================================


@settings(max_examples=100, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(
    item_suffix=st.integers(min_value=1, max_value=9999),
    checked_steps=st.lists(st.booleans(), min_size=1, max_size=10),
)
def test_workset_next_returns_correct_step(item_suffix, checked_steps):
    """
    Property 8: Workset next returns correct step
    
    For any workset with a plan.md containing checkboxes, getting the next action
    SHALL return the first unchecked item with its step number and description,
    or indicate completion if all items are checked.
    
    **Validates: Requirements 3.1, 3.2, 3.4**
    """
    backlog_root = create_temp_backlog()
    try:
        # Create a test item
        item_id = f"TST-TSK-{item_suffix:04d}"
        create_test_item(backlog_root, item_id)
        
        # Initialize workset
        init_result = init_workset(
            item_id,
            agent="test-agent",
            backlog_root=backlog_root,
            append_worklog=False,
        )
        
        # Create a custom plan.md with the given checked_steps pattern
        plan_lines = ["# Execution Plan: Test", "", "## Checklist", ""]
        for i, checked in enumerate(checked_steps):
            checkbox = "[x]" if checked else "[ ]"
            plan_lines.append(f"- {checkbox} Step {i+1}: Task description {i+1}")
        
        plan_path = init_result.workset_path / "plan.md"
        plan_path.write_text("\n".join(plan_lines), encoding="utf-8")
        
        # Get next action
        result = get_next_action(item_id, backlog_root=backlog_root)
        
        # Verify result type
        assert isinstance(result, WorksetNextResult)
        
        # Find expected first unchecked step
        first_unchecked_idx = None
        for i, checked in enumerate(checked_steps):
            if not checked:
                first_unchecked_idx = i
                break
        
        if first_unchecked_idx is None:
            # All steps checked - should indicate completion (Requirement 3.2)
            assert result.is_complete is True, \
                "Should indicate completion when all steps are checked"
            assert result.step_number == len(checked_steps), \
                "Step number should be total count when complete"
        else:
            # Should return first unchecked step (Requirement 3.1)
            assert result.is_complete is False, \
                "Should not indicate completion when unchecked steps exist"
            assert result.step_number == first_unchecked_idx + 1, \
                f"Should return step {first_unchecked_idx + 1}, got {result.step_number}"
            # Verify description contains step info (Requirement 3.4)
            assert f"Task description {first_unchecked_idx + 1}" in result.description, \
                "Description should match the step content"
    finally:
        cleanup_temp_backlog(backlog_root)


# =============================================================================
# Additional Unit Tests for Refresh and Next
# =============================================================================


def test_refresh_workset_with_deleted_source_item(temp_backlog):
    """Test that refresh fails gracefully when source item is deleted."""
    item_id = "TST-TSK-0010"
    item_path = create_test_item(temp_backlog, item_id)
    
    # Initialize workset
    init_workset(
        item_id,
        agent="test-agent",
        backlog_root=temp_backlog,
        append_worklog=False,
    )
    
    # Delete the source item
    item_path.unlink()
    
    # Attempt to refresh - should fail with appropriate error
    with pytest.raises(WorksetError) as exc_info:
        refresh_workset(
            item_id,
            agent="test-agent",
            backlog_root=temp_backlog,
        )
    
    assert "deleted" in str(exc_info.value).lower() or "not found" in str(exc_info.value).lower()


def test_get_next_action_without_workset(temp_backlog):
    """Test that get_next_action fails when workset doesn't exist."""
    item_id = "TST-TSK-0011"
    create_test_item(temp_backlog, item_id)
    
    # Don't initialize workset
    with pytest.raises(WorksetNotFoundError):
        get_next_action(item_id, backlog_root=temp_backlog)


def test_get_next_action_all_complete(temp_backlog):
    """Test that get_next_action returns completion when all steps done."""
    item_id = "TST-TSK-0012"
    create_test_item(temp_backlog, item_id)
    
    # Initialize workset
    init_result = init_workset(
        item_id,
        agent="test-agent",
        backlog_root=temp_backlog,
        append_worklog=False,
    )
    
    # Create plan with all checked items
    plan_content = """# Execution Plan: Test

## Checklist

- [x] Step 1: First task
- [x] Step 2: Second task
- [x] Step 3: Third task
"""
    plan_path = init_result.workset_path / "plan.md"
    plan_path.write_text(plan_content, encoding="utf-8")
    
    # Get next action
    result = get_next_action(item_id, backlog_root=temp_backlog)
    
    assert result.is_complete is True
    assert result.step_number == 3


def test_get_next_action_mixed_checkboxes(temp_backlog):
    """Test get_next_action with mixed checked/unchecked items."""
    item_id = "TST-TSK-0013"
    create_test_item(temp_backlog, item_id)
    
    # Initialize workset
    init_result = init_workset(
        item_id,
        agent="test-agent",
        backlog_root=temp_backlog,
        append_worklog=False,
    )
    
    # Create plan with mixed items
    plan_content = """# Execution Plan: Test

## Checklist

- [x] Step 1: First task (done)
- [x] Step 2: Second task (done)
- [ ] Step 3: Third task (pending)
- [ ] Step 4: Fourth task (pending)
"""
    plan_path = init_result.workset_path / "plan.md"
    plan_path.write_text(plan_content, encoding="utf-8")
    
    # Get next action
    result = get_next_action(item_id, backlog_root=temp_backlog)
    
    assert result.is_complete is False
    assert result.step_number == 3
    assert "Third task" in result.description


# =============================================================================
# Import promote and cleanup functions for tests
# =============================================================================

from kano_backlog_ops.workset import (
    promote_deliverables,
    cleanup_worksets,
    WorksetPromoteResult,
    WorksetCleanupResult,
    get_workset_cache_root,
)


# =============================================================================
# Property 9: Workset promote copies files and logs
# Feature: workset-topic-context, Property 9: Workset promote copies files and logs
# Validates: Requirements 4.1, 4.2, 4.3
# =============================================================================


@settings(max_examples=100, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(
    item_suffix=st.integers(min_value=1, max_value=9999),
    agent_name=st.text(
        alphabet=st.characters(whitelist_categories=('L', 'N'), whitelist_characters='-_'),
        min_size=1,
        max_size=20
    ).filter(lambda x: x.strip() and len(x) > 0 and x[0].isalpha()),
    file_count=st.integers(min_value=1, max_value=5),
)
def test_workset_promote_copies_files_and_logs(item_suffix, agent_name, file_count):
    """
    Property 9: Workset promote copies files and logs
    
    For any workset with files in deliverables/, promoting SHALL copy all files
    to the canonical artifacts location and append a worklog entry summarizing
    the promoted files.
    
    **Validates: Requirements 4.1, 4.2, 4.3**
    """
    backlog_root = create_temp_backlog()
    try:
        # Create a test item
        item_id = f"TST-TSK-{item_suffix:04d}"
        item_path = create_test_item(backlog_root, item_id)
        
        # Initialize workset
        init_result = init_workset(
            item_id,
            agent=agent_name,
            backlog_root=backlog_root,
            append_worklog=False,
        )
        
        # Create test deliverable files
        deliverables_dir = init_result.workset_path / "deliverables"
        created_files = []
        for i in range(file_count):
            file_name = f"deliverable_{i}.txt"
            file_path = deliverables_dir / file_name
            file_path.write_text(f"Content for deliverable {i}", encoding="utf-8")
            created_files.append(file_name)
        
        # Read item content before promote
        item_content_before = item_path.read_text(encoding="utf-8")
        worklog_count_before = item_content_before.count("[agent=")
        
        # Promote deliverables (Requirement 4.1, 4.2)
        result = promote_deliverables(
            item_id,
            agent=agent_name,
            backlog_root=backlog_root,
            dry_run=False,
            append_worklog=True,
        )
        
        # Verify result type
        assert isinstance(result, WorksetPromoteResult)
        
        # Verify all files were promoted (Requirement 4.1)
        assert len(result.promoted_files) == file_count, \
            f"Expected {file_count} promoted files, got {len(result.promoted_files)}"
        
        # Verify files exist in target location (Requirement 4.2)
        target_dir = result.target_path
        assert target_dir.exists(), "Target artifacts directory should exist"
        
        for file_name in created_files:
            target_file = target_dir / file_name
            assert target_file.exists(), f"Promoted file {file_name} should exist in target"
            # Verify content was copied correctly
            original_content = (deliverables_dir / file_name).read_text(encoding="utf-8")
            copied_content = target_file.read_text(encoding="utf-8")
            assert original_content == copied_content, "File content should match"
        
        # Verify worklog entry was appended (Requirement 4.3)
        item_content_after = item_path.read_text(encoding="utf-8")
        worklog_count_after = item_content_after.count("[agent=")
        
        assert worklog_count_after == worklog_count_before + 1, \
            "Worklog entry should be appended"
        assert "Promoted" in item_content_after, \
            "Worklog should contain 'Promoted'"
        assert "deliverable" in item_content_after.lower(), \
            "Worklog should mention deliverables"
    finally:
        cleanup_temp_backlog(backlog_root)


# =============================================================================
# Property 10: Workset promote dry-run is side-effect free
# Feature: workset-topic-context, Property 10: Workset promote dry-run is side-effect free
# Validates: Requirements 4.4
# =============================================================================


@settings(max_examples=100, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(
    item_suffix=st.integers(min_value=1, max_value=9999),
    agent_name=st.text(
        alphabet=st.characters(whitelist_categories=('L', 'N'), whitelist_characters='-_'),
        min_size=1,
        max_size=20
    ).filter(lambda x: x.strip() and len(x) > 0 and x[0].isalpha()),
    file_count=st.integers(min_value=1, max_value=5),
)
def test_workset_promote_dry_run_is_side_effect_free(item_suffix, agent_name, file_count):
    """
    Property 10: Workset promote dry-run is side-effect free
    
    For any workset, promoting with --dry-run SHALL not modify any files on disk
    (no copies, no worklog entries).
    
    **Validates: Requirements 4.4**
    """
    backlog_root = create_temp_backlog()
    try:
        # Create a test item
        item_id = f"TST-TSK-{item_suffix:04d}"
        item_path = create_test_item(backlog_root, item_id)
        
        # Initialize workset
        init_result = init_workset(
            item_id,
            agent=agent_name,
            backlog_root=backlog_root,
            append_worklog=False,
        )
        
        # Create test deliverable files
        deliverables_dir = init_result.workset_path / "deliverables"
        for i in range(file_count):
            file_name = f"deliverable_{i}.txt"
            file_path = deliverables_dir / file_name
            file_path.write_text(f"Content for deliverable {i}", encoding="utf-8")
        
        # Read item content before promote
        item_content_before = item_path.read_text(encoding="utf-8")
        
        # Get artifacts directory state before
        artifacts_dir = backlog_root / "products" / "test-product" / "artifacts" / item_id
        artifacts_existed_before = artifacts_dir.exists()
        
        # Promote with dry_run=True (Requirement 4.4)
        result = promote_deliverables(
            item_id,
            agent=agent_name,
            backlog_root=backlog_root,
            dry_run=True,
            append_worklog=True,  # Even with append_worklog=True, dry_run should prevent it
        )
        
        # Verify result lists files that would be promoted
        assert len(result.promoted_files) == file_count, \
            "Dry run should list files that would be promoted"
        
        # Verify no files were actually copied (Requirement 4.4)
        if not artifacts_existed_before:
            assert not artifacts_dir.exists(), \
                "Artifacts directory should not be created in dry run"
        
        # Verify worklog was not modified (Requirement 4.4)
        item_content_after = item_path.read_text(encoding="utf-8")
        assert item_content_before == item_content_after, \
            "Item content should not change in dry run"
    finally:
        cleanup_temp_backlog(backlog_root)


# =============================================================================
# Property 11: Workset cleanup respects TTL
# Feature: workset-topic-context, Property 11: Workset cleanup respects TTL
# Validates: Requirements 5.1, 5.2
# =============================================================================


@settings(max_examples=100, suppress_health_check=[HealthCheck.function_scoped_fixture], deadline=None)
@given(
    ttl_hours=st.integers(min_value=1, max_value=168),
    old_workset_count=st.integers(min_value=0, max_value=3),
    new_workset_count=st.integers(min_value=0, max_value=3),
)
def test_workset_cleanup_respects_ttl(ttl_hours, old_workset_count, new_workset_count):
    """
    Property 11: Workset cleanup respects TTL
    
    For any set of worksets with varying ages, cleanup with --ttl-hours N SHALL
    delete only worksets older than N hours and report accurate counts.
    
    **Validates: Requirements 5.1, 5.2**
    """
    from datetime import timedelta
    
    backlog_root = create_temp_backlog()
    try:
        # Create old worksets (older than TTL)
        old_item_ids = []
        for i in range(old_workset_count):
            item_id = f"TST-TSK-{1000 + i:04d}"
            create_test_item(backlog_root, item_id)
            
            init_result = init_workset(
                item_id,
                agent="test-agent",
                backlog_root=backlog_root,
                append_worklog=False,
            )
            
            # Modify created_at to be older than TTL
            meta_path = init_result.workset_path / "meta.json"
            with open(meta_path, "r", encoding="utf-8") as f:
                meta_data = json.load(f)
            
            old_time = datetime.now(timezone.utc) - timedelta(hours=ttl_hours + 1)
            meta_data["created_at"] = old_time.isoformat().replace("+00:00", "Z")
            
            with open(meta_path, "w", encoding="utf-8") as f:
                json.dump(meta_data, f, indent=2)
            
            old_item_ids.append(item_id)
        
        # Create new worksets (newer than TTL)
        new_item_ids = []
        for i in range(new_workset_count):
            item_id = f"TST-TSK-{2000 + i:04d}"
            create_test_item(backlog_root, item_id)
            
            init_workset(
                item_id,
                agent="test-agent",
                backlog_root=backlog_root,
                append_worklog=False,
            )
            new_item_ids.append(item_id)
        
        # Run cleanup (Requirement 5.1)
        result = cleanup_worksets(
            ttl_hours=ttl_hours,
            backlog_root=backlog_root,
            dry_run=False,
        )
        
        # Verify result type
        assert isinstance(result, WorksetCleanupResult)
        
        # Verify correct count deleted (Requirement 5.1, 5.2)
        assert result.deleted_count == old_workset_count, \
            f"Expected {old_workset_count} deleted, got {result.deleted_count}"
        
        # Verify old worksets were deleted
        for item_id in old_item_ids:
            workset_path = get_item_workset_path(item_id, backlog_root)
            assert not workset_path.exists(), \
                f"Old workset {item_id} should be deleted"
        
        # Verify new worksets were preserved
        for item_id in new_item_ids:
            workset_path = get_item_workset_path(item_id, backlog_root)
            assert workset_path.exists(), \
                f"New workset {item_id} should be preserved"
    finally:
        cleanup_temp_backlog(backlog_root)


# =============================================================================
# Property 12: Workset cleanup scope is limited
# Feature: workset-topic-context, Property 12: Workset cleanup scope is limited
# Validates: Requirements 5.3
# =============================================================================

# Windows reserved names that cannot be used as directory names
WINDOWS_RESERVED_NAMES = {'CON', 'PRN', 'AUX', 'NUL', 'COM1', 'COM2', 'COM3', 'COM4', 
                          'COM5', 'COM6', 'COM7', 'COM8', 'COM9', 'LPT1', 'LPT2', 
                          'LPT3', 'LPT4', 'LPT5', 'LPT6', 'LPT7', 'LPT8', 'LPT9'}


@settings(max_examples=100, suppress_health_check=[HealthCheck.function_scoped_fixture], deadline=None)
@given(
    item_suffix=st.integers(min_value=1, max_value=9999),
    topic_name=st.text(
        alphabet=st.characters(whitelist_categories=('L', 'N'), whitelist_characters='-_'),
        min_size=3,
        max_size=20
    ).filter(lambda x: x.strip() and len(x) > 0 and x[0].isalpha() and x.upper() not in WINDOWS_RESERVED_NAMES),
)
def test_workset_cleanup_scope_is_limited(item_suffix, topic_name):
    """
    Property 12: Workset cleanup scope is limited
    
    For any cleanup operation, only worksets under `_kano/backlog/.cache/worksets/items/`
    SHALL be affected; topics and other directories SHALL remain untouched.
    
    **Validates: Requirements 5.3**
    """
    from datetime import timedelta
    
    backlog_root = create_temp_backlog()
    try:
        # Create an old workset that should be deleted
        item_id = f"TST-TSK-{item_suffix:04d}"
        create_test_item(backlog_root, item_id)
        
        init_result = init_workset(
            item_id,
            agent="test-agent",
            backlog_root=backlog_root,
            append_worklog=False,
        )
        
        # Make the workset old
        meta_path = init_result.workset_path / "meta.json"
        with open(meta_path, "r", encoding="utf-8") as f:
            meta_data = json.load(f)
        
        old_time = datetime.now(timezone.utc) - timedelta(hours=100)
        meta_data["created_at"] = old_time.isoformat().replace("+00:00", "Z")
        
        with open(meta_path, "w", encoding="utf-8") as f:
            json.dump(meta_data, f, indent=2)
        
        # Create a topic directory (should NOT be affected by cleanup)
        cache_root = get_workset_cache_root(backlog_root)
        topics_dir = cache_root / "topics"
        topics_dir.mkdir(parents=True, exist_ok=True)
        
        topic_path = topics_dir / topic_name
        topic_path.mkdir(parents=True, exist_ok=True)
        
        # Create a manifest.json in the topic
        manifest = {
            "topic": topic_name,
            "agent": "test-agent",
            "seed_items": [],
            "pinned_docs": [],
            "created_at": old_time.isoformat().replace("+00:00", "Z"),  # Also old
            "updated_at": old_time.isoformat().replace("+00:00", "Z"),
        }
        manifest_path = topic_path / "manifest.json"
        with open(manifest_path, "w", encoding="utf-8") as f:
            json.dump(manifest, f, indent=2)
        
        # Run cleanup with short TTL
        result = cleanup_worksets(
            ttl_hours=1,
            backlog_root=backlog_root,
            dry_run=False,
        )
        
        # Verify workset was deleted
        assert result.deleted_count == 1, "Old workset should be deleted"
        assert not init_result.workset_path.exists(), "Workset directory should be deleted"
        
        # Verify topic was NOT affected (Requirement 5.3)
        assert topic_path.exists(), "Topic directory should NOT be deleted"
        assert manifest_path.exists(), "Topic manifest should NOT be deleted"
    finally:
        cleanup_temp_backlog(backlog_root)


# =============================================================================
# Property 13: Workset cleanup dry-run is side-effect free
# Feature: workset-topic-context, Property 13: Workset cleanup dry-run is side-effect free
# Validates: Requirements 5.4
# =============================================================================


@settings(max_examples=100, deadline=None, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(
    workset_count=st.integers(min_value=1, max_value=5),
)
def test_workset_cleanup_dry_run_is_side_effect_free(workset_count):
    """
    Property 13: Workset cleanup dry-run is side-effect free
    
    For any cleanup operation with --dry-run, no worksets SHALL be deleted.
    
    **Validates: Requirements 5.4**
    """
    from datetime import timedelta
    
    backlog_root = create_temp_backlog()
    try:
        # Create old worksets
        item_ids = []
        for i in range(workset_count):
            item_id = f"TST-TSK-{3000 + i:04d}"
            create_test_item(backlog_root, item_id)
            
            init_result = init_workset(
                item_id,
                agent="test-agent",
                backlog_root=backlog_root,
                append_worklog=False,
            )
            
            # Make the workset old
            meta_path = init_result.workset_path / "meta.json"
            with open(meta_path, "r", encoding="utf-8") as f:
                meta_data = json.load(f)
            
            old_time = datetime.now(timezone.utc) - timedelta(hours=100)
            meta_data["created_at"] = old_time.isoformat().replace("+00:00", "Z")
            
            with open(meta_path, "w", encoding="utf-8") as f:
                json.dump(meta_data, f, indent=2)
            
            item_ids.append(item_id)
        
        # Run cleanup with dry_run=True (Requirement 5.4)
        result = cleanup_worksets(
            ttl_hours=1,
            backlog_root=backlog_root,
            dry_run=True,
        )
        
        # Verify result reports what would be deleted
        assert result.deleted_count == workset_count, \
            f"Dry run should report {workset_count} would be deleted"
        
        # Verify NO worksets were actually deleted (Requirement 5.4)
        for item_id in item_ids:
            workset_path = get_item_workset_path(item_id, backlog_root)
            assert workset_path.exists(), \
                f"Workset {item_id} should NOT be deleted in dry run"
            
            # Verify meta.json still exists
            meta_path = workset_path / "meta.json"
            assert meta_path.exists(), \
                f"meta.json for {item_id} should NOT be deleted in dry run"
    finally:
        cleanup_temp_backlog(backlog_root)


# =============================================================================
# Additional Unit Tests for Promote and Cleanup
# =============================================================================


def test_promote_empty_deliverables(temp_backlog):
    """Test that promote handles empty deliverables directory."""
    item_id = "TST-TSK-0020"
    create_test_item(temp_backlog, item_id)
    
    # Initialize workset (deliverables/ is empty by default)
    init_workset(
        item_id,
        agent="test-agent",
        backlog_root=temp_backlog,
        append_worklog=False,
    )
    
    # Promote with empty deliverables (Requirement 4.5)
    result = promote_deliverables(
        item_id,
        agent="test-agent",
        backlog_root=temp_backlog,
        append_worklog=False,
    )
    
    assert result.promoted_files == []
    assert "No deliverables" in result.worklog_entry


def test_promote_without_workset(temp_backlog):
    """Test that promote fails when workset doesn't exist."""
    item_id = "TST-TSK-0021"
    create_test_item(temp_backlog, item_id)
    
    # Don't initialize workset
    with pytest.raises(WorksetNotFoundError):
        promote_deliverables(
            item_id,
            agent="test-agent",
            backlog_root=temp_backlog,
        )


def test_cleanup_empty_cache(temp_backlog):
    """Test that cleanup handles empty cache directory."""
    # Don't create any worksets
    result = cleanup_worksets(
        ttl_hours=1,
        backlog_root=temp_backlog,
        dry_run=False,
    )
    
    assert result.deleted_count == 0
    assert result.deleted_paths == []
    assert result.space_reclaimed_bytes == 0


# =============================================================================
# Import detect_adr_candidates for tests
# =============================================================================

from kano_backlog_ops.workset import detect_adr_candidates


# =============================================================================
# Property 28: Detect ADR finds all Decision markers
# Feature: workset-topic-context, Property 28: Detect ADR finds all Decision markers
# Validates: Requirements 12.1, 12.2, 12.3, 12.4
# =============================================================================


@settings(max_examples=100, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(
    item_suffix=st.integers(min_value=1, max_value=9999),
    decision_texts=st.lists(
        st.text(
            alphabet=st.characters(whitelist_categories=('L', 'N', 'P', 'Z'), whitelist_characters=' '),
            min_size=5,
            max_size=100
        ).filter(lambda x: x.strip() and len(x.strip()) >= 5),
        min_size=0,
        max_size=5
    ),
)
def test_detect_adr_finds_all_decision_markers(item_suffix, decision_texts):
    """
    Property 28: Detect ADR finds all Decision markers
    
    For any workset with notes.md containing Decision: markers, detecting SHALL
    return all markers with their text and suggested ADR titles.
    
    **Validates: Requirements 12.1, 12.2, 12.3, 12.4**
    """
    backlog_root = create_temp_backlog()
    try:
        # Create a test item
        item_id = f"TST-TSK-{item_suffix:04d}"
        create_test_item(backlog_root, item_id)
        
        # Initialize workset
        init_result = init_workset(
            item_id,
            agent="test-agent",
            backlog_root=backlog_root,
            append_worklog=False,
        )
        
        # Create notes.md with Decision: markers
        notes_lines = ["# Notes: Test", "", "## Decisions", ""]
        
        # Add decision markers with various formats
        expected_decisions = []
        for i, text in enumerate(decision_texts):
            clean_text = text.strip()
            if clean_text:
                # Alternate between different case formats
                if i % 3 == 0:
                    notes_lines.append(f"Decision: {clean_text}")
                elif i % 3 == 1:
                    notes_lines.append(f"decision: {clean_text}")
                else:
                    notes_lines.append(f"DECISION: {clean_text}")
                expected_decisions.append(clean_text)
                notes_lines.append("")  # Add blank line between decisions
        
        # Add some non-decision content
        notes_lines.extend([
            "",
            "## Other Notes",
            "",
            "This is not a decision marker",
            "Neither is this: Decision without colon",
            "Or this decision: lowercase without capital D at start",
        ])
        
        notes_path = init_result.workset_path / "notes.md"
        notes_path.write_text("\n".join(notes_lines), encoding="utf-8")
        
        # Detect ADR candidates (Requirement 12.1)
        candidates = detect_adr_candidates(item_id, backlog_root=backlog_root)
        
        # Verify all decision markers were found (Requirement 12.1)
        assert len(candidates) == len(expected_decisions), \
            f"Expected {len(expected_decisions)} candidates, got {len(candidates)}"
        
        # Verify each candidate has required fields (Requirement 12.2, 12.3)
        for i, candidate in enumerate(candidates):
            assert "text" in candidate, "Candidate should have 'text' field"
            assert "suggested_title" in candidate, "Candidate should have 'suggested_title' field"
            
            # Verify text matches expected decision
            assert candidate["text"] == expected_decisions[i], \
                f"Expected text '{expected_decisions[i]}', got '{candidate['text']}'"
            
            # Verify suggested_title is non-empty and kebab-case
            title = candidate["suggested_title"]
            assert title, "Suggested title should not be empty"
            assert "-" in title or title.isalnum(), \
                "Suggested title should be kebab-case or alphanumeric"
            assert title == title.lower(), \
                "Suggested title should be lowercase"
    finally:
        cleanup_temp_backlog(backlog_root)


# =============================================================================
# Additional Unit Tests for Detect ADR
# =============================================================================


def test_detect_adr_empty_notes(temp_backlog):
    """Test that detect_adr handles empty notes gracefully."""
    item_id = "TST-TSK-0030"
    create_test_item(temp_backlog, item_id)
    
    # Initialize workset
    init_result = init_workset(
        item_id,
        agent="test-agent",
        backlog_root=temp_backlog,
        append_worklog=False,
    )
    
    # Create empty notes.md
    notes_path = init_result.workset_path / "notes.md"
    notes_path.write_text("# Notes\n\nNo decisions here.", encoding="utf-8")
    
    # Detect ADR candidates
    candidates = detect_adr_candidates(item_id, backlog_root=temp_backlog)
    
    assert candidates == []


def test_detect_adr_without_workset(temp_backlog):
    """Test that detect_adr fails when workset doesn't exist."""
    item_id = "TST-TSK-0031"
    create_test_item(temp_backlog, item_id)
    
    # Don't initialize workset
    with pytest.raises(WorksetNotFoundError):
        detect_adr_candidates(item_id, backlog_root=temp_backlog)


def test_detect_adr_multiple_decisions(temp_backlog):
    """Test that detect_adr finds multiple Decision markers."""
    item_id = "TST-TSK-0032"
    create_test_item(temp_backlog, item_id)
    
    # Initialize workset
    init_result = init_workset(
        item_id,
        agent="test-agent",
        backlog_root=temp_backlog,
        append_worklog=False,
    )
    
    # Create notes with multiple decisions
    notes_content = """# Notes: Test

## Decisions

Decision: Use SQLite for local storage because it's lightweight and portable

Decision: Implement caching layer to improve performance

decision: prefer JSON over YAML for configuration files

## Other Notes

This is not a decision.
"""
    notes_path = init_result.workset_path / "notes.md"
    notes_path.write_text(notes_content, encoding="utf-8")
    
    # Detect ADR candidates
    candidates = detect_adr_candidates(item_id, backlog_root=temp_backlog)
    
    assert len(candidates) == 3
    assert candidates[0]["text"] == "Use SQLite for local storage because it's lightweight and portable"
    assert candidates[1]["text"] == "Implement caching layer to improve performance"
    assert candidates[2]["text"] == "prefer JSON over YAML for configuration files"
    
    # Verify suggested titles are generated
    assert "sqlite" in candidates[0]["suggested_title"]
    assert "caching" in candidates[1]["suggested_title"]
    assert "json" in candidates[2]["suggested_title"]


def test_detect_adr_suggested_title_format(temp_backlog):
    """Test that suggested ADR titles are properly formatted."""
    item_id = "TST-TSK-0033"
    create_test_item(temp_backlog, item_id)
    
    # Initialize workset
    init_result = init_workset(
        item_id,
        agent="test-agent",
        backlog_root=temp_backlog,
        append_worklog=False,
    )
    
    # Create notes with a decision that has special characters
    notes_content = """# Notes

Decision: Use React.js & TypeScript for the frontend UI!
"""
    notes_path = init_result.workset_path / "notes.md"
    notes_path.write_text(notes_content, encoding="utf-8")
    
    # Detect ADR candidates
    candidates = detect_adr_candidates(item_id, backlog_root=temp_backlog)
    
    assert len(candidates) == 1
    title = candidates[0]["suggested_title"]
    
    # Title should be kebab-case, lowercase, no special chars
    assert title == title.lower()
    assert "&" not in title
    assert "!" not in title
    assert "." not in title
    # Should contain key words
    assert "react" in title or "typescript" in title or "frontend" in title
