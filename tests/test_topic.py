"""
Property-based tests for topic operations.

Feature: workset-topic-context
Task: 9.3 Write property tests for topic create and add
Requirements: 6.1-6.5, 7.1-7.5
"""

import json
import pytest
import tempfile
import shutil
from datetime import datetime, timezone
from pathlib import Path
from typing import List
from hypothesis import given, strategies as st, settings, assume, HealthCheck

import sys
test_dir = Path(__file__).parent
src_dir = test_dir.parent / "src"
sys.path.insert(0, str(src_dir))

from kano_backlog_ops.topic import (
    create_topic,
    add_item_to_topic,
    TopicCreateResult,
    TopicAddResult,
    TopicManifest,
    TopicNotFoundError,
    TopicExistsError,
    TopicValidationError,
    TopicError,
    get_topic_path,
    get_topics_root,
    validate_topic_name,
    is_valid_topic_name,
)

RESERVED_NAMES = {"items", "topics", "cache", "index", "meta"}


def create_temp_backlog() -> Path:
    temp_dir = Path(tempfile.mkdtemp())
    backlog_root = temp_dir / "_kano" / "backlog"
    product_dir = backlog_root / "products" / "test-product"
    items_dir = product_dir / "items" / "task" / "0000"
    items_dir.mkdir(parents=True, exist_ok=True)
    config_dir = product_dir / "_config"
    config_dir.mkdir(parents=True, exist_ok=True)
    config_content = {"project": {"name": "test-product", "prefix": "TST"}}
    (config_dir / "config.json").write_text(json.dumps(config_content))
    return backlog_root


def cleanup_temp_backlog(backlog_root: Path) -> None:
    temp_dir = backlog_root.parent.parent
    shutil.rmtree(temp_dir, ignore_errors=True)


def create_test_item(backlog_root: Path, item_id: str, title: str = "Test Item") -> Path:
    product_dir = backlog_root / "products" / "test-product"
    items_dir = product_dir / "items" / "task" / "0000"
    items_dir.mkdir(parents=True, exist_ok=True)
    uid = f"019b9c80-0000-7000-8000-{item_id.replace(chr(45), chr(48)).zfill(12)[-12:]}"
    item_path = items_dir / f"{item_id}_test-item.md"
    content = f"""---
id: {item_id}
uid: {uid}
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

# Risks / Dependencies

None

# Worklog

2026-01-12 10:00 [agent=test-agent] Created item
"""
    item_path.write_text(content, encoding="utf-8")
    return item_path


@pytest.fixture
def temp_backlog():
    backlog_root = create_temp_backlog()
    yield backlog_root
    cleanup_temp_backlog(backlog_root)


valid_topic_name_strategy = st.from_regex(
    r"^[a-z][a-z0-9_-]{0,30}$", fullmatch=True
).filter(lambda x: x.lower() not in RESERVED_NAMES)

invalid_topic_name_strategy = st.one_of(
    st.just(""),
    st.just("-invalid"),
    st.just("_invalid"),
    st.just("123invalid"),
    st.just("invalid name"),
    st.just("items"),
    st.just("topics"),
    st.text(min_size=65, max_size=70, alphabet="abcdefghijklmnopqrstuvwxyz"),
)

agent_strategy = st.from_regex(r"^[a-zA-Z][a-zA-Z0-9_-]{2,20}$", fullmatch=True)


@settings(max_examples=100, deadline=None, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(topic_name=valid_topic_name_strategy, agent=agent_strategy)
def test_property_14_topic_create_produces_valid_structure(topic_name: str, agent: str):
    """Property 14: Topic create produces valid structure. Validates: Requirements 6.1, 6.2"""
    backlog_root = create_temp_backlog()
    try:
        result = create_topic(topic_name, agent=agent, backlog_root=backlog_root)
        assert isinstance(result, TopicCreateResult)
        assert result.topic_path.exists()
        assert result.topic_path.is_dir()
        manifest_path = result.topic_path / "manifest.json"
        assert manifest_path.exists()
        with open(manifest_path, "r", encoding="utf-8") as f:
            manifest_data = json.load(f)
        assert manifest_data["topic"] == topic_name
        assert manifest_data["agent"] == agent
        assert manifest_data["seed_items"] == []
        assert manifest_data["pinned_docs"] == []
        assert "created_at" in manifest_data
        assert "updated_at" in manifest_data
        datetime.fromisoformat(manifest_data["created_at"].replace("Z", "+00:00"))
        datetime.fromisoformat(manifest_data["updated_at"].replace("Z", "+00:00"))
        notes_path = result.topic_path / "notes.md"
        assert notes_path.exists()
    finally:
        cleanup_temp_backlog(backlog_root)


@settings(max_examples=100, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(topic_name=valid_topic_name_strategy, agent=agent_strategy)
def test_property_15_topic_create_rejects_duplicates(topic_name: str, agent: str):
    """Property 15: Topic create rejects duplicates. Validates: Requirements 6.4"""
    backlog_root = create_temp_backlog()
    try:
        result1 = create_topic(topic_name, agent=agent, backlog_root=backlog_root)
        manifest_path = result1.topic_path / "manifest.json"
        original_content = manifest_path.read_text()
        with pytest.raises(TopicExistsError) as exc_info:
            create_topic(topic_name, agent=agent, backlog_root=backlog_root)
        assert topic_name in str(exc_info.value)
        assert manifest_path.read_text() == original_content
    finally:
        cleanup_temp_backlog(backlog_root)


@settings(max_examples=100, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(invalid_name=invalid_topic_name_strategy, agent=agent_strategy)
def test_property_16_topic_name_validation(invalid_name: str, agent: str):
    """Property 16: Topic name validation. Validates: Requirements 6.5"""
    backlog_root = create_temp_backlog()
    try:
        with pytest.raises(TopicValidationError):
            create_topic(invalid_name, agent=agent, backlog_root=backlog_root)
        topics_root = get_topics_root(backlog_root)
        if topics_root.exists():
            topic_path = topics_root / invalid_name if invalid_name else topics_root / "_empty_"
            assert not topic_path.exists()
    finally:
        cleanup_temp_backlog(backlog_root)


@settings(max_examples=100, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(
    topic_name=valid_topic_name_strategy,
    agent=agent_strategy,
    item_suffix=st.integers(min_value=1, max_value=9999),
)
def test_property_17_topic_add_updates_manifest_correctly(topic_name: str, agent: str, item_suffix: int):
    """Property 17: Topic add updates manifest correctly. Validates: Requirements 7.1, 7.3"""
    backlog_root = create_temp_backlog()
    try:
        create_result = create_topic(topic_name, agent=agent, backlog_root=backlog_root)
        manifest_path = create_result.topic_path / "manifest.json"
        with open(manifest_path, "r", encoding="utf-8") as f:
            original_manifest = json.load(f)
        original_updated_at = original_manifest["updated_at"]
        item_id = f"TST-TSK-{item_suffix:04d}"
        create_test_item(backlog_root, item_id)
        add_result = add_item_to_topic(topic_name, item_id, backlog_root=backlog_root)
        assert isinstance(add_result, TopicAddResult)
        assert add_result.topic == topic_name
        assert add_result.added is True
        with open(manifest_path, "r", encoding="utf-8") as f:
            updated_manifest = json.load(f)
        assert len(updated_manifest["seed_items"]) == 1
        assert add_result.item_uid in updated_manifest["seed_items"]
        assert updated_manifest["updated_at"] >= original_updated_at
    finally:
        cleanup_temp_backlog(backlog_root)


@settings(max_examples=100, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(topic_name=valid_topic_name_strategy, agent=agent_strategy)
def test_property_18_topic_add_validates_item_exists(topic_name: str, agent: str):
    """Property 18: Topic add validates item exists. Validates: Requirements 7.2"""
    backlog_root = create_temp_backlog()
    try:
        create_result = create_topic(topic_name, agent=agent, backlog_root=backlog_root)
        manifest_path = create_result.topic_path / "manifest.json"
        original_content = manifest_path.read_text()
        with pytest.raises(TopicError) as exc_info:
            add_item_to_topic(topic_name, "NONEXISTENT-ITEM-9999", backlog_root=backlog_root)
        assert "not found" in str(exc_info.value).lower()
        assert manifest_path.read_text() == original_content
    finally:
        cleanup_temp_backlog(backlog_root)


@settings(max_examples=100, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(
    topic_name=valid_topic_name_strategy,
    agent=agent_strategy,
    item_suffix=st.integers(min_value=1, max_value=9999),
)
def test_property_19_topic_add_is_idempotent(topic_name: str, agent: str, item_suffix: int):
    """Property 19: Topic add is idempotent. Validates: Requirements 7.4"""
    backlog_root = create_temp_backlog()
    try:
        create_topic(topic_name, agent=agent, backlog_root=backlog_root)
        item_id = f"TST-TSK-{item_suffix:04d}"
        create_test_item(backlog_root, item_id)
        result1 = add_item_to_topic(topic_name, item_id, backlog_root=backlog_root)
        assert result1.added is True
        result2 = add_item_to_topic(topic_name, item_id, backlog_root=backlog_root)
        assert result2.added is False
        assert result2.item_uid == result1.item_uid
        topic_path = get_topic_path(topic_name, backlog_root)
        manifest_path = topic_path / "manifest.json"
        with open(manifest_path, "r", encoding="utf-8") as f:
            manifest = json.load(f)
        uid_count = manifest["seed_items"].count(result1.item_uid)
        assert uid_count == 1, f"Expected 1 occurrence, found {uid_count}"
    finally:
        cleanup_temp_backlog(backlog_root)


def test_topic_not_found_error_on_add(temp_backlog):
    with pytest.raises(TopicNotFoundError) as exc_info:
        add_item_to_topic("nonexistent-topic", "TST-TSK-0001", backlog_root=temp_backlog)
    assert "nonexistent-topic" in str(exc_info.value)


def test_create_topic_without_notes(temp_backlog):
    result = create_topic("test-topic", agent="test-agent", backlog_root=temp_backlog, create_notes=False)
    assert result.topic_path.exists()
    assert (result.topic_path / "manifest.json").exists()
    assert not (result.topic_path / "notes.md").exists()


def test_validate_topic_name_function():
    assert validate_topic_name("valid-topic") == []
    assert validate_topic_name("valid_topic") == []
    assert validate_topic_name("ValidTopic123") == []
    assert validate_topic_name("a") == []
    assert len(validate_topic_name("")) > 0
    assert len(validate_topic_name("-invalid")) > 0
    assert len(validate_topic_name("_invalid")) > 0
    assert len(validate_topic_name("123invalid")) > 0
    assert len(validate_topic_name("invalid name")) > 0
    assert len(validate_topic_name("items")) > 0
    assert len(validate_topic_name("a" * 65)) > 0


def test_is_valid_topic_name_function():
    assert is_valid_topic_name("valid-topic") is True
    assert is_valid_topic_name("") is False
    assert is_valid_topic_name("-invalid") is False
    assert is_valid_topic_name("items") is False


# =============================================================================
# Property Tests for Pin and Switch (Task 10.4)
# Requirements: 8.1-8.4, 9.1-9.4
# =============================================================================

from kano_backlog_ops.topic import (
    pin_document,
    switch_topic,
    get_active_topic,
    TopicPinResult,
    TopicSwitchResult,
)


def create_test_document(backlog_root: Path, doc_name: str, content: str = "Test content") -> Path:
    """Create a test document in the backlog."""
    # Create document in products directory (relative to workspace root)
    workspace_root = backlog_root.parent.parent
    doc_path = workspace_root / "_kano" / "backlog" / "products" / "test-product" / "decisions" / doc_name
    doc_path.parent.mkdir(parents=True, exist_ok=True)
    doc_path.write_text(content, encoding="utf-8")
    return doc_path


@settings(max_examples=100, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(
    topic_name=valid_topic_name_strategy,
    agent=agent_strategy,
    doc_suffix=st.integers(min_value=1, max_value=999),
)
def test_property_20_topic_pin_updates_manifest_correctly(topic_name: str, agent: str, doc_suffix: int):
    """
    Property 20: Topic pin updates manifest correctly.
    *For any* valid topic and valid document path, pinning SHALL add the path to pinned_docs
    and support relative paths from backlog root.
    **Validates: Requirements 8.1, 8.4**
    """
    backlog_root = create_temp_backlog()
    try:
        # Create topic
        create_result = create_topic(topic_name, agent=agent, backlog_root=backlog_root)
        manifest_path = create_result.topic_path / "manifest.json"
        
        # Create a test document
        doc_name = f"ADR-{doc_suffix:04d}_test-decision.md"
        doc_full_path = create_test_document(backlog_root, doc_name)
        
        # Use relative path from workspace root
        workspace_root = backlog_root.parent.parent
        relative_doc_path = str(doc_full_path.relative_to(workspace_root))
        
        # Pin the document
        pin_result = pin_document(topic_name, relative_doc_path, backlog_root=backlog_root)
        
        # Verify result
        assert isinstance(pin_result, TopicPinResult)
        assert pin_result.topic == topic_name
        assert pin_result.pinned is True
        
        # Verify manifest was updated
        with open(manifest_path, "r", encoding="utf-8") as f:
            manifest = json.load(f)
        
        assert len(manifest["pinned_docs"]) == 1
        assert relative_doc_path in manifest["pinned_docs"]
    finally:
        cleanup_temp_backlog(backlog_root)


@settings(max_examples=100, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(topic_name=valid_topic_name_strategy, agent=agent_strategy)
def test_property_21_topic_pin_validates_document_exists(topic_name: str, agent: str):
    """
    Property 21: Topic pin validates document exists.
    *For any* topic and non-existent document path, pinning SHALL return an error
    without modifying the manifest.
    **Validates: Requirements 8.2**
    """
    backlog_root = create_temp_backlog()
    try:
        # Create topic
        create_result = create_topic(topic_name, agent=agent, backlog_root=backlog_root)
        manifest_path = create_result.topic_path / "manifest.json"
        original_content = manifest_path.read_text()
        
        # Try to pin non-existent document
        with pytest.raises(TopicError) as exc_info:
            pin_document(topic_name, "nonexistent/path/to/doc.md", backlog_root=backlog_root)
        
        assert "not found" in str(exc_info.value).lower()
        
        # Verify manifest was not modified
        assert manifest_path.read_text() == original_content
    finally:
        cleanup_temp_backlog(backlog_root)


@settings(max_examples=100, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(
    topic_name=valid_topic_name_strategy,
    agent=agent_strategy,
    doc_suffix=st.integers(min_value=1, max_value=999),
)
def test_property_22_topic_pin_is_idempotent(topic_name: str, agent: str, doc_suffix: int):
    """
    Property 22: Topic pin is idempotent.
    *For any* topic and document already in pinned_docs, pinning the same document again
    SHALL not create duplicates.
    **Validates: Requirements 8.3**
    """
    backlog_root = create_temp_backlog()
    try:
        # Create topic
        create_topic(topic_name, agent=agent, backlog_root=backlog_root)
        
        # Create a test document
        doc_name = f"ADR-{doc_suffix:04d}_test-decision.md"
        doc_full_path = create_test_document(backlog_root, doc_name)
        
        # Use relative path from workspace root
        workspace_root = backlog_root.parent.parent
        relative_doc_path = str(doc_full_path.relative_to(workspace_root))
        
        # Pin the document twice
        result1 = pin_document(topic_name, relative_doc_path, backlog_root=backlog_root)
        assert result1.pinned is True
        
        result2 = pin_document(topic_name, relative_doc_path, backlog_root=backlog_root)
        assert result2.pinned is False
        assert result2.doc_path == result1.doc_path
        
        # Verify no duplicates in manifest
        topic_path = get_topic_path(topic_name, backlog_root)
        manifest_path = topic_path / "manifest.json"
        with open(manifest_path, "r", encoding="utf-8") as f:
            manifest = json.load(f)
        
        doc_count = manifest["pinned_docs"].count(relative_doc_path)
        assert doc_count == 1, f"Expected 1 occurrence, found {doc_count}"
    finally:
        cleanup_temp_backlog(backlog_root)


@settings(max_examples=100, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(
    topic_name=valid_topic_name_strategy,
    agent=agent_strategy,
    item_suffix=st.integers(min_value=1, max_value=9999),
)
def test_property_23_topic_switch_updates_active_topic(topic_name: str, agent: str, item_suffix: int):
    """
    Property 23: Topic switch updates active topic.
    *For any* valid topic and agent, switching SHALL update active_topic.<agent>.txt
    to contain the topic name and output a summary with item count and pinned doc count.
    **Validates: Requirements 9.1, 9.2**
    """
    backlog_root = create_temp_backlog()
    try:
        # Create topic with an item
        create_topic(topic_name, agent=agent, backlog_root=backlog_root)
        
        # Add an item to the topic
        item_id = f"TST-TSK-{item_suffix:04d}"
        create_test_item(backlog_root, item_id)
        add_item_to_topic(topic_name, item_id, backlog_root=backlog_root)
        
        # Switch to the topic
        switch_result = switch_topic(topic_name, agent=agent, backlog_root=backlog_root)
        
        # Verify result
        assert isinstance(switch_result, TopicSwitchResult)
        assert switch_result.topic == topic_name
        assert switch_result.item_count == 1
        assert switch_result.pinned_doc_count == 0
        
        # Verify active topic file was created
        active_topic = get_active_topic(agent, backlog_root=backlog_root)
        assert active_topic == topic_name
    finally:
        cleanup_temp_backlog(backlog_root)


@settings(max_examples=100, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(agent=agent_strategy)
def test_property_24_topic_switch_requires_existing_topic(agent: str):
    """
    Property 24: Topic switch requires existing topic.
    *For any* non-existent topic, switching SHALL return an error without modifying
    active_topic files.
    **Validates: Requirements 9.3**
    """
    backlog_root = create_temp_backlog()
    try:
        # Get initial active topic (should be None)
        initial_active = get_active_topic(agent, backlog_root=backlog_root)
        
        # Try to switch to non-existent topic
        with pytest.raises(TopicNotFoundError) as exc_info:
            switch_topic("nonexistent-topic", agent=agent, backlog_root=backlog_root)
        
        assert "nonexistent-topic" in str(exc_info.value)
        
        # Verify active topic was not modified
        current_active = get_active_topic(agent, backlog_root=backlog_root)
        assert current_active == initial_active
    finally:
        cleanup_temp_backlog(backlog_root)


# =============================================================================
# Additional Unit Tests for Pin and Switch
# =============================================================================


def test_pin_document_to_nonexistent_topic(temp_backlog):
    """Test that pinning to a non-existent topic raises TopicNotFoundError."""
    with pytest.raises(TopicNotFoundError) as exc_info:
        pin_document("nonexistent-topic", "some/doc.md", backlog_root=temp_backlog)
    assert "nonexistent-topic" in str(exc_info.value)


def test_switch_topic_tracks_previous(temp_backlog):
    """Test that switch_topic correctly tracks the previous topic."""
    # Create two topics
    create_topic("topic-one", agent="test-agent", backlog_root=temp_backlog)
    create_topic("topic-two", agent="test-agent", backlog_root=temp_backlog)
    
    # Switch to first topic
    result1 = switch_topic("topic-one", agent="test-agent", backlog_root=temp_backlog)
    assert result1.previous_topic is None
    
    # Switch to second topic
    result2 = switch_topic("topic-two", agent="test-agent", backlog_root=temp_backlog)
    assert result2.previous_topic == "topic-one"
    
    # Switch back to first topic
    result3 = switch_topic("topic-one", agent="test-agent", backlog_root=temp_backlog)
    assert result3.previous_topic == "topic-two"


def test_get_active_topic_returns_none_when_no_active(temp_backlog):
    """Test that get_active_topic returns None when no topic is active."""
    active = get_active_topic("test-agent", backlog_root=temp_backlog)
    assert active is None


def test_pin_document_with_absolute_path(temp_backlog):
    """Test that pin_document handles absolute paths correctly."""
    # Create topic
    create_topic("test-topic", agent="test-agent", backlog_root=temp_backlog)
    
    # Create a test document
    doc_path = create_test_document(temp_backlog, "test-doc.md")
    
    # Pin using absolute path
    result = pin_document("test-topic", str(doc_path), backlog_root=temp_backlog)
    
    assert result.pinned is True
    # The stored path should be relative
    assert not Path(result.doc_path).is_absolute()


# =============================================================================
# Property Tests for Export and List (Task 12.4)
# Requirements: 10.1-10.5, 11.1-11.4
# =============================================================================

from kano_backlog_ops.topic import (
    export_topic_context,
    list_topics,
    TopicContextBundle,
)


@settings(max_examples=100, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(
    topic_name=valid_topic_name_strategy,
    agent=agent_strategy,
    item_suffix=st.integers(min_value=1, max_value=9999),
    doc_suffix=st.integers(min_value=1, max_value=999),
)
def test_property_25_topic_export_produces_complete_bundle(
    topic_name: str, agent: str, item_suffix: int, doc_suffix: int
):
    """
    Property 25: Topic export produces complete bundle.
    *For any* topic with seed items and pinned docs, exporting SHALL produce a bundle
    containing summaries of all seed items and content from all pinned documents.
    **Validates: Requirements 10.1, 10.2, 10.3**
    """
    backlog_root = create_temp_backlog()
    try:
        # Create topic
        create_topic(topic_name, agent=agent, backlog_root=backlog_root)
        
        # Add an item to the topic
        item_id = f"TST-TSK-{item_suffix:04d}"
        create_test_item(backlog_root, item_id, title=f"Test Item {item_suffix}")
        add_result = add_item_to_topic(topic_name, item_id, backlog_root=backlog_root)
        
        # Pin a document
        doc_name = f"ADR-{doc_suffix:04d}_test-decision.md"
        doc_content = f"# Test Decision {doc_suffix}\n\nThis is test content."
        doc_full_path = create_test_document(backlog_root, doc_name, doc_content)
        workspace_root = backlog_root.parent.parent
        relative_doc_path = str(doc_full_path.relative_to(workspace_root))
        pin_document(topic_name, relative_doc_path, backlog_root=backlog_root)
        
        # Export context
        bundle = export_topic_context(topic_name, backlog_root=backlog_root)
        
        # Verify bundle structure
        assert isinstance(bundle, TopicContextBundle)
        assert bundle.topic == topic_name
        assert len(bundle.items) == 1
        assert len(bundle.pinned_docs) == 1
        assert bundle.generated_at  # Should have a timestamp
        
        # Verify item summary
        item_summary = bundle.items[0]
        assert item_summary["uid"] == add_result.item_uid
        assert item_summary["title"] == f"Test Item {item_suffix}"
        
        # Verify pinned doc content
        pinned_doc = bundle.pinned_docs[0]
        assert pinned_doc["path"] == relative_doc_path
        assert "content" in pinned_doc
        assert doc_content in pinned_doc["content"]
    finally:
        cleanup_temp_backlog(backlog_root)


@settings(max_examples=100, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(
    topic_name=valid_topic_name_strategy,
    agent=agent_strategy,
    item_suffix=st.integers(min_value=1, max_value=9999),
)
def test_property_26_topic_export_is_deterministic(topic_name: str, agent: str, item_suffix: int):
    """
    Property 26: Topic export is deterministic.
    *For any* topic, exporting twice with the same topic state SHALL produce identical output.
    **Validates: Requirements 10.5**
    """
    backlog_root = create_temp_backlog()
    try:
        # Create topic with an item
        create_topic(topic_name, agent=agent, backlog_root=backlog_root)
        item_id = f"TST-TSK-{item_suffix:04d}"
        create_test_item(backlog_root, item_id)
        add_item_to_topic(topic_name, item_id, backlog_root=backlog_root)
        
        # Export twice
        bundle1 = export_topic_context(topic_name, backlog_root=backlog_root)
        bundle2 = export_topic_context(topic_name, backlog_root=backlog_root)
        
        # Verify deterministic output (except for generated_at timestamp)
        assert bundle1.topic == bundle2.topic
        assert bundle1.items == bundle2.items
        assert bundle1.pinned_docs == bundle2.pinned_docs
    finally:
        cleanup_temp_backlog(backlog_root)


@settings(max_examples=100, deadline=None, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(
    topic_names=st.lists(valid_topic_name_strategy, min_size=1, max_size=5, unique=True),
    agent=agent_strategy,
)
def test_property_27_list_returns_complete_metadata(topic_names: List[str], agent: str):
    """
    Property 27: List returns complete metadata.
    *For any* set of topics, listing SHALL return all items with their metadata
    (counts, timestamps).
    **Validates: Requirements 11.1, 11.3**
    """
    backlog_root = create_temp_backlog()
    try:
        # Create multiple topics
        for topic_name in topic_names:
            create_topic(topic_name, agent=agent, backlog_root=backlog_root)
        
        # List topics
        topics = list_topics(backlog_root=backlog_root)
        
        # Verify all topics are returned
        assert len(topics) == len(topic_names)
        
        # Verify each topic has required metadata
        returned_names = {t.topic for t in topics}
        for topic_name in topic_names:
            assert topic_name in returned_names
        
        for topic in topics:
            assert topic.topic  # Has name
            assert topic.agent == agent  # Has agent
            assert topic.created_at  # Has timestamp
            assert topic.updated_at  # Has timestamp
            assert isinstance(topic.seed_items, list)
            assert isinstance(topic.pinned_docs, list)
    finally:
        cleanup_temp_backlog(backlog_root)


# =============================================================================
# Additional Unit Tests for Export and List
# =============================================================================


def test_export_nonexistent_topic(temp_backlog):
    """Test that exporting a non-existent topic raises TopicNotFoundError."""
    with pytest.raises(TopicNotFoundError) as exc_info:
        export_topic_context("nonexistent-topic", backlog_root=temp_backlog)
    assert "nonexistent-topic" in str(exc_info.value)


def test_list_topics_empty(temp_backlog):
    """Test that list_topics returns empty list when no topics exist."""
    topics = list_topics(backlog_root=temp_backlog)
    assert topics == []


def test_list_topics_sorted(temp_backlog):
    """Test that list_topics returns topics in sorted order."""
    # Create topics in non-alphabetical order
    create_topic("zebra-topic", agent="test-agent", backlog_root=temp_backlog)
    create_topic("alpha-topic", agent="test-agent", backlog_root=temp_backlog)
    create_topic("middle-topic", agent="test-agent", backlog_root=temp_backlog)
    
    topics = list_topics(backlog_root=temp_backlog)
    
    # Should be sorted alphabetically
    topic_names = [t.topic for t in topics]
    assert topic_names == ["alpha-topic", "middle-topic", "zebra-topic"]


def test_export_empty_topic(temp_backlog):
    """Test that exporting an empty topic returns empty lists."""
    create_topic("empty-topic", agent="test-agent", backlog_root=temp_backlog)
    
    bundle = export_topic_context("empty-topic", backlog_root=temp_backlog)
    
    assert bundle.topic == "empty-topic"
    assert bundle.items == []
    assert bundle.pinned_docs == []
    assert bundle.generated_at  # Should still have timestamp


def test_export_with_missing_item(temp_backlog):
    """Test that export handles missing items gracefully."""
    # Create topic
    create_topic("test-topic", agent="test-agent", backlog_root=temp_backlog)
    
    # Manually add a non-existent item UID to the manifest
    topic_path = get_topic_path("test-topic", temp_backlog)
    manifest_path = topic_path / "manifest.json"
    manifest = TopicManifest.load(manifest_path)
    manifest.seed_items.append("nonexistent-uid-12345")
    manifest.save(manifest_path)
    
    # Export should handle gracefully
    bundle = export_topic_context("test-topic", backlog_root=temp_backlog)
    
    assert len(bundle.items) == 1
    assert bundle.items[0]["uid"] == "nonexistent-uid-12345"
    assert "error" in bundle.items[0]
