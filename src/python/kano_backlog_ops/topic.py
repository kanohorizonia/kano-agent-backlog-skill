"""
topic.py - Topic-based context management operations.

This module provides use-case functions for managing topic-based context groupings.
Topics provide a higher-level grouping mechanism that enables rapid context switching
when users change focus areas during a conversation.

Per ADR-0011 and ADR-0012, topics are derived data that can be deleted and rebuilt.
"""

from __future__ import annotations

import json
import os
import re
import uuid
from dataclasses import dataclass, field, asdict
from datetime import datetime, timezone, timedelta
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple
import hashlib
import subprocess

from kano_backlog_core.errors import BacklogError

try:
    from uuid import uuid7  # type: ignore[attr-defined]
except ImportError:
    from uuid6 import uuid7  # type: ignore


# =============================================================================
# Error Types (Task 8.2)
# =============================================================================


class TopicError(BacklogError):
    """Base error for topic operations."""

    def __init__(self, message: str, suggestion: Optional[str] = None):
        self.message = message
        self.suggestion = suggestion
        super().__init__(message)


class TopicNotFoundError(TopicError):
    """Topic does not exist."""

    def __init__(self, topic_name: str):
        self.topic_name = topic_name
        super().__init__(
            f"Topic not found: {topic_name}",
            suggestion="Run 'kano topic create <name>' first",
        )


class TopicExistsError(TopicError):
    """Topic already exists."""

    def __init__(self, topic_name: str):
        self.topic_name = topic_name
        super().__init__(
            f"Topic already exists: {topic_name}",
            suggestion="Use a different topic name or delete the existing topic",
        )


class TopicValidationError(TopicError):
    """Topic validation failed."""

    def __init__(self, errors: List[str]):
        self.errors = errors
        error_list = "\n".join(f"  - {e}" for e in errors)
        super().__init__(f"Topic validation failed:\n{error_list}")


# =============================================================================
# Data Models (Task 8.1)
# =============================================================================


@dataclass
class SnippetRef:
    """Reference to a code snippet (reference-first approach to avoid massive copy-paste)."""

    type: str = "snippet"  # Always "snippet"
    repo: str = "local"  # "local" or git remote URL
    revision: Optional[str] = None  # commit hash (None if not in git or dirty)
    file: str = ""  # Relative file path from repo root
    lines: List[int] = field(default_factory=list)  # [start, end] 1-based inclusive
    hash: str = ""  # sha256 of content for staleness check
    cached_text: Optional[str] = None  # Optional snapshot of content
    collected_at: Optional[str] = None  # ISO 8601 timestamp
    collector: Optional[str] = None  # Agent identity

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "SnippetRef":
        return cls(
            type=data.get("type", "snippet"),
            repo=data.get("repo", "local"),
            revision=data.get("revision"),
            file=data.get("file", ""),
            lines=data.get("lines", []),
            hash=data.get("hash", ""),
            cached_text=data.get("cached_text"),
            collected_at=data.get("collected_at"),
            collector=data.get("collector"),
        )


@dataclass
class TopicManifest:
    """Topic manifest.json structure."""

    topic: str  # Topic name (directory name)
    agent: str  # Agent who created
    seed_items: List[str] = field(default_factory=list)  # List of item UIDs
    pinned_docs: List[str] = field(default_factory=list)  # List of document paths
    snippet_refs: List[SnippetRef] = field(default_factory=list)  # List of code snippet refs
    related_topics: List[str] = field(default_factory=list)  # List of related topic names
    status: str = "open"  # open|closed
    closed_at: Optional[str] = None  # ISO 8601 timestamp
    created_at: str = ""  # ISO 8601 timestamp
    updated_at: str = ""  # ISO 8601 timestamp
    has_spec: bool = False  # verification flag

    def to_dict(self) -> Dict[str, Any]:
        d = asdict(self)
        # Convert SnippetRef objects to dicts for JSON serialization
        d["snippet_refs"] = [s.to_dict() if isinstance(s, SnippetRef) else s for s in self.snippet_refs]
        return d

    def to_dict_legacy(self) -> Dict[str, Any]:
        """Convert to dictionary for JSON serialization."""
        return asdict(self)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "TopicManifest":
        """Create from dictionary (JSON deserialization)."""
        raw_snippets = data.get("snippet_refs", [])
        snippet_refs = [SnippetRef.from_dict(s) if isinstance(s, dict) else s for s in raw_snippets]
        return cls(
            topic=data["topic"],
            agent=data["agent"],
            seed_items=data.get("seed_items", []),
            pinned_docs=data.get("pinned_docs", []),
            snippet_refs=snippet_refs,
            related_topics=data.get("related_topics", []),
            status=data.get("status", "open"),
            closed_at=data.get("closed_at"),
            created_at=data.get("created_at", ""),
            updated_at=data.get("updated_at", ""),
            has_spec=data.get("has_spec", False),
        )

    def save(self, path: Path) -> None:
        """Save manifest to JSON file."""
        with open(path, "w", encoding="utf-8") as f:
            json.dump(self.to_dict(), f, indent=2)

    @classmethod
    def load(cls, path: Path) -> "TopicManifest":
        """Load manifest from JSON file."""
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        return cls.from_dict(data)


@dataclass
class TopicCreateResult:
    """Result of creating a topic."""

    topic_path: Path
    manifest: TopicManifest


@dataclass
class TopicAddResult:
    """Result of adding item to topic."""

    topic: str
    item_uid: str
    added: bool  # False if already present


@dataclass
class TopicPinResult:
    """Result of pinning a document to topic."""

    topic: str
    doc_path: str
    pinned: bool  # False if already pinned


@dataclass
class TopicSwitchResult:
    """Result of switching active topic."""

    topic: str
    item_count: int
    pinned_doc_count: int
    previous_topic: Optional[str]


@dataclass
class TopicContextBundle:
    """Exported context bundle."""

    topic: str
    items: List[Dict[str, Any]]  # Item summaries
    pinned_docs: List[Dict[str, str]]  # Doc path + content
    generated_at: str


@dataclass
class TopicAddSnippetResult:
    topic: str
    snippet: SnippetRef
    added: bool


@dataclass
class TopicCloseResult:
    topic: str
    closed: bool
    closed_at: str


@dataclass
class TopicCleanupResult:
    topics_scanned: int
    topics_cleaned: int
    materials_deleted: int
    deleted_paths: List[Path]


@dataclass
class TopicReopenResult:
    topic: str
    reopened: bool
    reopened_at: str
    previous_status: str


@dataclass
class TopicAddReferenceResult:
    """Result of adding a topic reference."""
    topic: str
    referenced_topic: str
    added: bool  # False if already referenced


@dataclass
class TopicRemoveReferenceResult:
    """Result of removing a topic reference."""
    topic: str
    referenced_topic: str
    removed: bool  # False if not referenced


@dataclass
class TopicSnapshot:
    """Topic snapshot metadata and content."""
    name: str  # Snapshot name
    topic: str  # Topic name
    created_at: str  # ISO 8601 timestamp
    created_by: str  # Agent who created the snapshot
    description: str  # User-provided description
    manifest: TopicManifest  # Snapshot of manifest at time of creation
    brief_content: Optional[str] = None  # Snapshot of brief.generated.md content
    notes_content: Optional[str] = None  # Snapshot of notes.md content
    materials_index: Dict[str, str] = field(default_factory=dict)  # File path -> content hash


@dataclass
class TopicSnapshotResult:
    """Result of creating a topic snapshot."""
    topic: str
    snapshot_name: str
    snapshot_path: Path
    created_at: str


@dataclass
class TopicRestoreResult:
    """Result of restoring from a topic snapshot."""
    topic: str
    snapshot_name: str
    restored_at: str
    restored_components: List[str]  # List of components that were restored


@dataclass
class TopicSnapshotListResult:
    """Result of listing topic snapshots."""
    topic: str
    snapshots: List[Dict[str, Any]]  # List of snapshot metadata


@dataclass
class TopicSplitResult:
    """Result of splitting a topic."""
    source_topic: str
    new_topics: List[str]
    items_redistributed: Dict[str, List[str]]  # topic -> list of item UIDs
    materials_redistributed: Dict[str, List[str]]  # topic -> list of material paths
    references_updated: List[str]  # list of topics with updated references
    split_at: str


@dataclass
class TopicMergeResult:
    """Result of merging topics."""
    target_topic: str
    merged_topics: List[str]
    items_merged: Dict[str, List[str]]  # source topic -> list of item UIDs
    materials_merged: Dict[str, List[str]]  # source topic -> list of material paths
    references_updated: List[str]  # list of topics with updated references
    merged_at: str


@dataclass
class TopicSplitPlan:
    """Plan for splitting a topic (used in dry-run)."""
    source_topic: str
    new_topics: List[Dict[str, Any]]  # List of {name, items, materials}
    conflicts: List[str]  # List of potential conflicts
    references_to_update: List[str]  # Topics that will need reference updates


@dataclass
class TopicMergePlan:
    """Plan for merging topics (used in dry-run)."""
    target_topic: str
    source_topics: List[str]
    item_conflicts: List[str]  # Items that exist in multiple topics
    material_conflicts: List[str]  # Materials with same path but different content
    references_to_update: List[str]  # Topics that will need reference updates


@dataclass
class TopicDecisionAuditResult:
    """Result of auditing decision write-back for a topic."""

    topic: str
    report_path: Path
    decisions_found: int
    items_total: int
    items_with_writeback: List[str]
    items_missing_writeback: List[str]
    sources_scanned: List[str]


# =============================================================================
# Shared Topic State (KABSD-TSK-0257)
# =============================================================================


@dataclass
class AgentTopicState:
    """State of active topic for a single agent."""
    agent_id: str  # Stable Kano agent ID (e.g., 'copilot', 'codex')
    active_topic_id: Optional[str] = None  # UUIDv7 of active topic, or None
    updated_at: str = ""  # ISO 8601 timestamp (set automatically)

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "AgentTopicState":
        return cls(
            agent_id=data["agent_id"],
            active_topic_id=data.get("active_topic_id"),
            updated_at=data.get("updated_at", ""),
        )


@dataclass
class StateIndex:
    """Global index for shared topic state (state.json)."""
    version: int = 1  # Format version (future-proofing)
    repo_id: str = ""  # sha256(normalized absolute repo root)
    agents: Dict[str, AgentTopicState] = field(default_factory=dict)  # agent_id -> AgentTopicState

    def to_dict(self) -> Dict[str, Any]:
        return {
            "version": self.version,
            "repo_id": self.repo_id,
            "agents": {agent_id: state.to_dict() for agent_id, state in self.agents.items()},
        }

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "StateIndex":
        agents = {
            agent_id: AgentTopicState.from_dict(state_data)
            for agent_id, state_data in data.get("agents", {}).items()
        }
        return cls(
            version=data.get("version", 1),
            repo_id=data.get("repo_id", ""),
            agents=agents,
        )

    def save(self, path: Path) -> None:
        """Save state index to JSON file (atomic write)."""
        path.parent.mkdir(parents=True, exist_ok=True)
        # Write to temp file first, then atomic rename to avoid corruption
        temp_path = path.with_suffix(path.suffix + ".tmp")
        with open(temp_path, "w", encoding="utf-8") as f:
            json.dump(self.to_dict(), f, indent=2)
        # Atomic rename (on POSIX; on Windows, replaces target)
        temp_path.replace(path)

    @classmethod
    def load(cls, path: Path) -> "StateIndex":
        """Load state index from JSON file."""
        if not path.exists():
            return cls()
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        return cls.from_dict(data)


@dataclass
class TopicStateDocument:
    """Metadata for a topic in the shared state store (topics/{topic_id}.json)."""
    topic_id: str  # UUIDv7
    name: str  # Topic name (canonical, lowercase)
    participants: List[str] = field(default_factory=list)  # List of agent IDs
    status: str = "active"  # active | closed
    created_at: str = ""  # ISO 8601
    updated_at: str = ""  # ISO 8601
    created_by: str = ""  # Agent ID who created
    description: str = ""  # Optional user-provided description

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "TopicStateDocument":
        return cls(
            topic_id=data["topic_id"],
            name=data.get("name", ""),
            participants=data.get("participants", []),
            status=data.get("status", "active"),
            created_at=data.get("created_at", ""),
            updated_at=data.get("updated_at", ""),
            created_by=data.get("created_by", ""),
            description=data.get("description", ""),
        )

    def save(self, path: Path) -> None:
        """Save topic state document to JSON file (atomic write)."""
        path.parent.mkdir(parents=True, exist_ok=True)
        temp_path = path.with_suffix(path.suffix + ".tmp")
        with open(temp_path, "w", encoding="utf-8") as f:
            json.dump(self.to_dict(), f, indent=2)
        temp_path.replace(path)

    @classmethod
    def load(cls, path: Path) -> "TopicStateDocument":
        """Load topic state document from JSON file."""
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        return cls.from_dict(data)


# =============================================================================
# Topic Name Validation (Task 8.3)
# =============================================================================

# Valid topic name pattern: alphanumeric, hyphens, underscores
# Must start with a letter (not a number), no consecutive special chars
TOPIC_NAME_PATTERN = re.compile(r"^[a-zA-Z][a-zA-Z0-9_-]*$")


def _normalize_topic_name(topic_name: str) -> str:
    """Canonicalize to avoid collisions on case-insensitive filesystems (e.g., Windows)."""
    return (topic_name or "").strip().lower()


def validate_topic_name(topic_name: str) -> List[str]:
    """
    Validate a topic name.

    Valid topic names:
    - Start with a letter (a-z, A-Z)
    - Contain only alphanumeric characters, hyphens, and underscores
    - Are not empty
    - Are not too long (max 64 characters)

    Args:
        topic_name: Topic name to validate

    Returns:
        List of validation error messages (empty if valid)
    """
    errors = []

    topic_name = (topic_name or "").strip()

    if not topic_name:
        errors.append("Topic name cannot be empty")
        return errors

    if len(topic_name) > 64:
        errors.append(f"Topic name too long ({len(topic_name)} chars, max 64)")

    if not TOPIC_NAME_PATTERN.match(topic_name):
        errors.append(
            "Topic name must start with a letter and contain only "
            "alphanumeric characters, hyphens, and underscores"
        )

    # Check for reserved names
    reserved_names = {"items", "topics", "cache", "index", "meta"}
    if topic_name.lower() in reserved_names:
        errors.append(f"Topic name '{topic_name}' is reserved")

    return errors


def is_valid_topic_name(topic_name: str) -> bool:
    """
    Check if a topic name is valid.

    Args:
        topic_name: Topic name to check

    Returns:
        True if valid, False otherwise
    """
    return len(validate_topic_name(topic_name)) == 0


# =============================================================================
# Shared State Store Functions (KABSD-TSK-0257)
# =============================================================================

# Maximum slug length for topic state filenames (to avoid Windows MAX_PATH issues)
# Format: {slug}_{uuid}.json where slug is truncated to this length
# Total filename: 48 (slug) + 1 (_) + 36 (uuid) + 5 (.json) = 90 chars
MAX_TOPIC_SLUG_LENGTH = 48


def _truncate_slug(slug: str, max_length: int = MAX_TOPIC_SLUG_LENGTH) -> str:
    """
    Truncate slug to maximum length for safe filesystem operations.
    
    Prevents hitting Windows MAX_PATH (260 chars) or other filesystem limits.
    
    Args:
        slug: Topic name slug
        max_length: Maximum length (default: 48)
    
    Returns:
        Truncated slug (may be shorter than max_length if original was shorter)
    """
    if len(slug) <= max_length:
        return slug
    return slug[:max_length]


def _compute_repo_id(backlog_root: Path) -> str:
    """
    Compute deterministic repo identifier from normalized absolute repo root.

    Args:
        backlog_root: Path to _kano/backlog

    Returns:
        sha256 hash of normalized repo root path (hex string)

    Note:
        Normalization handles Windows case-insensitivity and UNC paths.
    """
    repo_root = backlog_root.parent.parent  # Go up from _kano/backlog to repo root
    # Normalize: absolute, case-lower on Windows, forward slashes
    normalized = str(repo_root.resolve()).lower().replace("\\", "/")
    return hashlib.sha256(normalized.encode("utf-8")).hexdigest()


def get_state_store_path(backlog_root: Optional[Path] = None) -> Path:
    """
    Get the path to the shared state store (state.json).

    Args:
        backlog_root: Root path for backlog

    Returns:
        Path to _kano/backlog/.cache/worksets/state.json
    """
    if backlog_root is None:
        backlog_root = Path.cwd() / "_kano" / "backlog"
    return backlog_root / ".cache" / "worksets" / "state.json"


def get_state_topics_dir(backlog_root: Optional[Path] = None) -> Path:
    """
    Get the directory for topic state documents.

    Args:
        backlog_root: Root path for backlog

    Returns:
        Path to _kano/backlog/.cache/worksets/topics/
    """
    if backlog_root is None:
        backlog_root = Path.cwd() / "_kano" / "backlog"
    return backlog_root / ".cache" / "worksets" / "topics"


def load_state_index(backlog_root: Optional[Path] = None) -> StateIndex:
    """
    Load the global state index from state.json.

    Args:
        backlog_root: Root path for backlog

    Returns:
        StateIndex (empty if file doesn't exist or is invalid)

    Note:
        Never raises; returns empty state on any error (graceful degradation).
    """
    if backlog_root is None:
        try:
            backlog_root = _find_backlog_root()
        except TopicError:
            return StateIndex()

    state_path = get_state_store_path(backlog_root)
    try:
        state = StateIndex.load(state_path)
        # Validate and set repo_id if missing
        if not state.repo_id:
            state.repo_id = _compute_repo_id(backlog_root)
        return state
    except Exception:
        # Gracefully return empty state on any read error
        return StateIndex(repo_id=_compute_repo_id(backlog_root))


def save_state_index(state: StateIndex, backlog_root: Optional[Path] = None) -> None:
    """
    Save the global state index to state.json (atomic write).

    Args:
        state: StateIndex to save
        backlog_root: Root path for backlog

    Note:
        Uses atomic rename to minimize corruption risk.
    """
    if backlog_root is None:
        backlog_root = _find_backlog_root()

    state_path = get_state_store_path(backlog_root)
    state.save(state_path)


def load_topic_state(topic_id: str, backlog_root: Optional[Path] = None) -> Optional[TopicStateDocument]:
    """
    Load a topic state document from topics/*_{topic_id}.json.

    Supports both formats:
    - New: {slug}_{uuid}.json
    - Legacy: {uuid}.json

    Args:
        topic_id: Topic UUID
        backlog_root: Root path for backlog

    Returns:
        TopicStateDocument or None if not found

    Note:
        Never raises; returns None on any error.
    """
    if backlog_root is None:
        backlog_root = Path.cwd() / "_kano" / "backlog"

    topics_dir = get_state_topics_dir(backlog_root)
    
    # Try new format first: *_{topic_id}.json
    try:
        for topic_file in topics_dir.glob(f"*_{topic_id}.json"):
            return TopicStateDocument.load(topic_file)
    except Exception:
        pass
    
    # Fallback to legacy format: {topic_id}.json
    try:
        legacy_path = topics_dir / f"{topic_id}.json"
        if legacy_path.exists():
            return TopicStateDocument.load(legacy_path)
    except Exception:
        pass
    
    return None


def save_topic_state(doc: TopicStateDocument, backlog_root: Optional[Path] = None) -> None:
    """
    Save a topic state document to topics/{slug}_{topic_id}.json (atomic write).

    Uses human-readable slug prefix for better discoverability.
    Format: {topic-name}_{uuid}.json (slug truncated to MAX_TOPIC_SLUG_LENGTH)

    Args:
        doc: TopicStateDocument to save
        backlog_root: Root path for backlog
    """
    if backlog_root is None:
        backlog_root = _find_backlog_root()

    topics_dir = get_state_topics_dir(backlog_root)
    # Truncate slug to prevent Windows MAX_PATH issues
    safe_slug = _truncate_slug(doc.name)
    topic_path = topics_dir / f"{safe_slug}_{doc.topic_id}.json"
    doc.save(topic_path)


def generate_topic_id() -> str:
    """
    Generate a new topic ID using UUIDv7 (time-based, sortable).

    Returns:
        UUIDv7 as string
    """
    return str(uuid7())


def migrate_legacy_active_topics(backlog_root: Optional[Path] = None) -> Dict[str, str]:
    """
    Migrate legacy active_topic.<agent>.txt files to shared state.json.

    This function:
    1. Scans for all active_topic.<agent>.txt files in .cache/worksets/
    2. Reads each file to get the active topic name
    3. Creates corresponding entries in state.json and topic state documents
    4. Does NOT delete legacy files (they coexist for compatibility)

    Args:
        backlog_root: Root path for backlog

    Returns:
        Dict mapping agent_id -> topic_name for agents that were migrated

    Note:
        Gracefully handles missing files or corrupt data; never raises.
    """
    if backlog_root is None:
        try:
            backlog_root = _find_backlog_root()
        except TopicError:
            return {}

    migrated: Dict[str, str] = {}
    worksets_dir = backlog_root / ".cache" / "worksets"

    if not worksets_dir.exists():
        return migrated

    # Find all active_topic.<agent>.txt files
    for legacy_file in worksets_dir.glob("active_topic.*.txt"):
        try:
            # Extract agent ID from filename
            filename = legacy_file.name  # e.g., "active_topic.copilot.txt"
            agent_id = filename.replace("active_topic.", "").replace(".txt", "")

            # Read topic name
            topic_name = _normalize_topic_name(legacy_file.read_text(encoding="utf-8"))
            if not topic_name:
                continue

            # Check if topic exists
            topic_path = get_topic_path(topic_name, backlog_root)
            if not topic_path.exists() or not (topic_path / "manifest.json").exists():
                continue

            # Load shared state
            state = load_state_index(backlog_root)

            # Find or create topic state document
            topic_id = None
            topics_dir = get_state_topics_dir(backlog_root)
            if topics_dir.exists():
                for topic_file in topics_dir.glob("*.json"):
                    try:
                        doc = TopicStateDocument.load(topic_file)
                        if doc.name == topic_name:
                            topic_id = doc.topic_id
                            break
                    except Exception:
                        pass

            # Create new topic state if not found
            if not topic_id:
                topic_id = generate_topic_id()
                now = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
                topic_doc = TopicStateDocument(
                    topic_id=topic_id,
                    name=topic_name,
                    participants=[agent_id],
                    status="active",
                    created_at=now,
                    updated_at=now,
                    created_by=agent_id,
                )
                save_topic_state(topic_doc, backlog_root)
            else:
                # Update existing topic state to add participant
                topic_doc = load_topic_state(topic_id, backlog_root)
                if topic_doc and agent_id not in topic_doc.participants:
                    topic_doc.participants.append(agent_id)
                    now = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
                    topic_doc.updated_at = now
                    save_topic_state(topic_doc, backlog_root)

            # Update shared state
            now = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
            agent_state = AgentTopicState(
                agent_id=agent_id,
                active_topic_id=topic_id,
                updated_at=now,
            )
            state.agents[agent_id] = agent_state
            save_state_index(state, backlog_root)

            migrated[agent_id] = topic_name
        except Exception:
            # Skip on any error for individual file
            continue

    return migrated


def cleanup_legacy_active_topics(backlog_root: Optional[Path] = None, dry_run: bool = False) -> List[Path]:
    """
    Remove all legacy active_topic.<agent>.txt files after migration.

    Args:
        backlog_root: Root path for backlog
        dry_run: If True, return paths but don't delete

    Returns:
        List of deleted (or would-be deleted) file paths

    Note:
        Safe to call even if files don't exist; gracefully handles errors.
    """
    if backlog_root is None:
        try:
            backlog_root = _find_backlog_root()
        except TopicError:
            return []

    deleted: List[Path] = []
    worksets_dir = backlog_root / ".cache" / "worksets"

    if not worksets_dir.exists():
        return deleted

    for legacy_file in worksets_dir.glob("active_topic.*.txt"):
        try:
            if not dry_run:
                legacy_file.unlink()
            deleted.append(legacy_file)
        except Exception:
            pass

    return deleted


def migrate_topic_state_filenames(backlog_root: Optional[Path] = None, dry_run: bool = False) -> Dict[str, str]:
    """
    Migrate topic state files from {uuid}.json to {slug}_{uuid}.json format.

    Slug is truncated to MAX_TOPIC_SLUG_LENGTH to avoid filesystem limits.

    Args:
        backlog_root: Root path for backlog
        dry_run: If True, return what would be renamed but don't actually rename

    Returns:
        Dict mapping old_filename -> new_filename for renamed files

    Note:
        Skips files already in new format; gracefully handles errors.
    """
    if backlog_root is None:
        try:
            backlog_root = _find_backlog_root()
        except TopicError:
            return {}

    renamed: Dict[str, str] = {}
    topics_dir = get_state_topics_dir(backlog_root)

    if not topics_dir.exists():
        return renamed

    for topic_file in topics_dir.glob("*.json"):
        try:
            # Skip if already in new format (contains underscore before .json)
            stem = topic_file.stem
            if "_" in stem:
                continue

            # Load document to get the topic name
            doc = TopicStateDocument.load(topic_file)
            
            # Generate new filename: {slug}_{uuid}.json (slug truncated for safety)
            safe_slug = _truncate_slug(doc.name)
            new_filename = f"{safe_slug}_{doc.topic_id}.json"
            new_path = topics_dir / new_filename

            # Skip if target already exists
            if new_path.exists():
                continue

            if not dry_run:
                topic_file.rename(new_path)
            
            renamed[topic_file.name] = new_filename
        except Exception:
            # Skip on any error for individual file
            continue

    return renamed


def list_active_topics(backlog_root: Optional[Path] = None) -> Dict[str, Dict[str, Any]]:
    """
    List all active topics across all agents in the shared state.

    Args:
        backlog_root: Root path for backlog

    Returns:
        Dict mapping agent_id -> {topic_name, topic_id, updated_at}
        or empty dict if no active topics

    Note:
        Returns empty dict gracefully if state is unreadable.
    """
    if backlog_root is None:
        try:
            backlog_root = _find_backlog_root()
        except TopicError:
            return {}

    result: Dict[str, Dict[str, Any]] = {}

    try:
        state = load_state_index(backlog_root)
        for agent_id, agent_state in state.agents.items():
            if agent_state.active_topic_id:
                topic_doc = load_topic_state(agent_state.active_topic_id, backlog_root)
                if topic_doc:
                    result[agent_id] = {
                        "topic_name": topic_doc.name,
                        "topic_id": agent_state.active_topic_id,
                        "updated_at": agent_state.updated_at,
                        "participants": topic_doc.participants,
                    }
    except Exception:
        pass

    return result


def get_topic_state_by_name(
    topic_name: str,
    backlog_root: Optional[Path] = None,
) -> Optional[TopicStateDocument]:
    """
    Get the topic state document for a given topic name.

    Optimized to use slug-based filename when available.

    Args:
        topic_name: Topic name (canonical)
        backlog_root: Root path for backlog

    Returns:
        TopicStateDocument or None if not found

    Note:
        Returns None gracefully if topic doesn't exist.
    """
    if backlog_root is None:
        try:
            backlog_root = _find_backlog_root()
        except TopicError:
            return None

    canonical_name = _normalize_topic_name(topic_name)
    topics_dir = get_state_topics_dir(backlog_root)

    if not topics_dir.exists():
        return None

    try:
        # Fast path: try slug-based filename first
        for topic_file in topics_dir.glob(f"{canonical_name}_*.json"):
            doc = TopicStateDocument.load(topic_file)
            if doc.name == canonical_name:
                return doc
        
        # Fallback: scan all files (for legacy format)
        for topic_file in topics_dir.glob("*.json"):
            # Skip already checked files
            if topic_file.stem.startswith(f"{canonical_name}_"):
                continue
            doc = TopicStateDocument.load(topic_file)
            if doc.name == canonical_name:
                return doc
    except Exception:
        pass

    return None


def update_agent_state(
    agent_id: str,
    active_topic_name: Optional[str] = None,
    backlog_root: Optional[Path] = None,
) -> AgentTopicState:
    """
    Update an agent's active topic state in the shared state.

    Args:
        agent_id: Agent identity
        active_topic_name: Topic name to activate, or None to deactivate
        backlog_root: Root path for backlog

    Returns:
        Updated AgentTopicState

    Raises:
        TopicNotFoundError: If topic_name is given but topic doesn't exist
    """
    if backlog_root is None:
        backlog_root = _find_backlog_root()

    # Load shared state
    state = load_state_index(backlog_root)

    # Resolve topic_id if topic_name is given
    topic_id = None
    if active_topic_name:
        canonical_name = _normalize_topic_name(active_topic_name)
        topic_doc = get_topic_state_by_name(canonical_name, backlog_root)
        if not topic_doc:
            raise TopicNotFoundError(canonical_name)
        topic_id = topic_doc.topic_id

    # Update agent state
    now = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
    agent_state = AgentTopicState(
        agent_id=agent_id,
        active_topic_id=topic_id,
        updated_at=now,
    )
    state.agents[agent_id] = agent_state
    save_state_index(state, backlog_root)

    return agent_state


# =============================================================================
# Directory Utilities (Task 8.3)
# =============================================================================


def _find_backlog_root(start: Optional[Path] = None) -> Path:
    """
    Find the backlog root directory.

    Args:
        start: Starting path for search (defaults to cwd)

    Returns:
        Path to _kano/backlog directory

    Raises:
        TopicError: If backlog root not found
    """
    current = (start or Path.cwd()).resolve()
    while current != current.parent:
        backlog_check = current / "_kano" / "backlog"
        if backlog_check.exists():
            return backlog_check
        current = current.parent
    raise TopicError(
        "Cannot find backlog root (_kano/backlog)",
        suggestion="Ensure you are in a directory with a _kano/backlog structure",
    )


def get_topics_root(backlog_root: Optional[Path] = None) -> Path:
    """
    Get the root directory for topics.

    Args:
        backlog_root: Root path for backlog (defaults to _kano/backlog)

    Returns:
        Path to _kano/backlog/topics/

    Note:
        Changed from .cache/worksets/topics/ to topics/ per KABSD-FTR-0037
        so that brief.generated.md can optionally be version-controlled.
    """
    if backlog_root is None:
        backlog_root = _find_backlog_root()
    return backlog_root / "topics"


def get_topic_path(topic_name: str, backlog_root: Optional[Path] = None) -> Path:
    """
    Get the topic directory path.

    Args:
        topic_name: Topic name
        backlog_root: Root path for backlog

    Returns:
        Path to _kano/backlog/topics/<topic-name>/
    """
    topics_root = get_topics_root(backlog_root)
    return topics_root / _normalize_topic_name(topic_name)


def get_active_topic_path(agent: str, backlog_root: Optional[Path] = None) -> Path:
    """
    Get the path to the active topic file for an agent.

    Args:
        agent: Agent identity
        backlog_root: Root path for backlog

    Returns:
        Path to _kano/backlog/.cache/worksets/active_topic.<agent>.txt
    """
    if backlog_root is None:
        backlog_root = _find_backlog_root()
    cache_root = backlog_root / ".cache" / "worksets"
    return cache_root / f"active_topic.{agent}.txt"


def ensure_topic_dirs(backlog_root: Optional[Path] = None) -> Path:
    """
    Ensure topic directory structure exists.

    Creates:
        _kano/backlog/topics/

    Args:
        backlog_root: Root path for backlog

    Returns:
        Path to the topics root
    """
    topics_root = get_topics_root(backlog_root)
    topics_root.mkdir(parents=True, exist_ok=True)
    return topics_root


# =============================================================================
# Stub Functions (to be implemented in later tasks)
# =============================================================================


def create_topic(
    topic_name: str,
    *,
    agent: str,
    backlog_root: Optional[Path] = None,
    create_notes: bool = True,
    create_brief: bool = True,
    create_spec: bool = False,
) -> TopicCreateResult:
    """
    Create a new topic with materials buffer structure.

    Args:
        topic_name: Name for the topic
        agent: Agent identity
        backlog_root: Root path for backlog
        create_notes: Whether to create notes.md (default: True)
        create_brief: Whether to create brief.md template and brief.generated.md (default: True)

    Returns:
        TopicCreateResult with creation details

    Raises:
        TopicExistsError: If topic already exists
        TopicValidationError: If topic name is invalid

    Directory structure created:
        topics/<topic>/
            manifest.json
            brief.md (if create_brief=True)
            brief.generated.md (if create_brief=True)
            spec/              (if create_spec=True)
                requirements.md
                design.md
                tasks.md
            notes.md           (if create_notes=True, for backward compat)
            materials/
                clips/         # code snippet refs + cached text
                links/         # urls / notes
                extracts/      # extracted paragraphs
                logs/          # build logs / command outputs
            synthesis/         # intermediate drafts
            publish/           # prepared write-backs
    """
    # Validate topic name (Requirement 6.5)
    validation_errors = validate_topic_name(topic_name)
    if validation_errors:
        raise TopicValidationError(validation_errors)

    canonical_name = _normalize_topic_name(topic_name)

    # Resolve backlog root
    if backlog_root is None:
        backlog_root = _find_backlog_root()

    # Get topic path
    topic_path = get_topic_path(canonical_name, backlog_root)

    # Check if topic already exists (Requirement 6.4)
    if topic_path.exists():
        raise TopicExistsError(canonical_name)

    # Ensure topic directories exist
    ensure_topic_dirs(backlog_root)

    # Create topic directory (Requirement 6.1)
    topic_path.mkdir(parents=True, exist_ok=True)

    # Create materials buffer subdirectories (KABSD-FTR-0037)
    materials_subdirs = ["clips", "links", "extracts", "logs"]
    materials_root = topic_path / "materials"
    for subdir in materials_subdirs:
        (materials_root / subdir).mkdir(parents=True, exist_ok=True)

    # Create synthesis and publish directories
    (topic_path / "synthesis").mkdir(parents=True, exist_ok=True)
    (topic_path / "publish").mkdir(parents=True, exist_ok=True)

    # Generate timestamps
    now = datetime.now(timezone.utc)
    timestamp = now.isoformat().replace("+00:00", "Z")

    # Create manifest (Requirement 6.2)
    manifest = TopicManifest(
        topic=canonical_name,
        agent=agent,
        seed_items=[],
        pinned_docs=[],
        snippet_refs=[],
        status="open",
        closed_at=None,
        created_at=timestamp,
        updated_at=timestamp,
        has_spec=create_spec,
    )

    # Save manifest.json
    manifest_path = topic_path / "manifest.json"
    manifest.save(manifest_path)

    # Optionally create brief.md (human) and brief.generated.md (tool).
    if create_brief:
        brief_content = _generate_topic_brief_template(canonical_name, timestamp)
        brief_path = topic_path / "brief.md"
        brief_path.write_text(brief_content, encoding="utf-8")
        distill_topic(canonical_name, backlog_root=backlog_root)

    # Optionally create notes.md (Requirement 6.3, backward compat)
    if create_notes:
        notes_content = _generate_topic_notes_template(canonical_name)
        notes_path = topic_path / "notes.md"
        notes_path.write_text(notes_content, encoding="utf-8")

    if create_spec:
        spec_root = topic_path / "spec"
        spec_root.mkdir(parents=True, exist_ok=True)
        (spec_root / "requirements.md").write_text(_generate_spec_requirements_template(canonical_name), encoding="utf-8")
        (spec_root / "design.md").write_text(_generate_spec_design_template(canonical_name), encoding="utf-8")
        (spec_root / "tasks.md").write_text(_generate_spec_tasks_template(canonical_name), encoding="utf-8")
        manifest.has_spec = True

    return TopicCreateResult(
        topic_path=topic_path,
        manifest=manifest,
    )


def _generate_topic_notes_template(topic_name: str) -> str:
    """
    Generate notes.md template for a topic.

    Args:
        topic_name: Topic name

    Returns:
        Notes template content
    """
    return f"""# Topic Notes: {topic_name}

## Overview

{{Brief description of this topic's focus area}}

## Related Items

{{Notes about the items in this topic}}

## Key Decisions

{{Important decisions related to this topic}}

## Open Questions

- {{questions to resolve}}
"""


def _generate_topic_brief_template(topic_name: str, timestamp: str) -> str:
    """
    Generate brief.md template for a topic (human-maintained briefing).

    The brief is the synthesized output that helps agents quickly understand
    task context without re-collecting materials.

    Args:
        topic_name: Topic name
        timestamp: ISO 8601 timestamp

    Returns:
        Brief template content (human-maintained; safe to edit)
    """
    return f"""# Topic Brief: {topic_name}

Generated: {timestamp}

## Facts

<!-- Verified facts with citations to materials/items/docs -->
- [ ] {{fact}} — [source](ref)

## Unknowns / Risks

<!-- Open questions and potential blockers -->
- [ ] {{unknown or risk}}

## Proposed Actions

<!-- Concrete next steps, linked to workitems -->
- [ ] {{action}} → {{workitem ref or "new ticket needed"}}

## Decision Candidates

<!-- Tradeoffs requiring ADR -->
- [ ] {{decision}} → {{ADR ref or "draft needed"}}

---
_This brief is human-maintained. `topic distill` writes to `brief.generated.md`._
"""


def _generate_spec_requirements_template(topic_name: str) -> str:
    return f"""# Requirements: {topic_name}

## User Stories

- As a [role], I want [feature], so that [benefit].

## Acceptance Criteria

### Requirement 1: [Feature Name]

- [ ] 1.1 [Criterion]
- [ ] 1.2 [Criterion]
"""


def _generate_spec_design_template(topic_name: str) -> str:
    return f"""# Design: {topic_name}

## Data Models

```python
# class Model: ...
```

## Interfaces

```python
# def function(): ...
```

## Correctness Properties (Invariants)

- **Property 1**: [Description]
  *Validates: Requirement 1.1*
"""


def _generate_spec_tasks_template(topic_name: str) -> str:
    return f"""# Tasks: {topic_name}

## Implementation Plan

- [ ] 1. [Task Name]
  - [ ] 1.1 [Subtask]
  - Validates: Requirement 1
"""


def _now_timestamp() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _workspace_root(backlog_root: Path) -> Path:
    # backlog_root is _kano/backlog, workspace root is two levels up.
    return backlog_root.parent.parent


def _workspace_relative_path(path: Path, workspace_root: Path) -> str:
    """Return a stable workspace-relative path across Windows short/long temp paths."""
    try:
        return str(path.relative_to(workspace_root)).replace("\\", "/")
    except ValueError:
        return os.path.relpath(str(path), str(workspace_root)).replace("\\", "/")


def _try_git_revision(workspace_root: Path) -> Optional[str]:
    git_dir = workspace_root / ".git"
    if not git_dir.exists():
        return None
    try:
        out = subprocess.check_output(
            ["git", "rev-parse", "HEAD"],
            cwd=str(workspace_root),
            stderr=subprocess.DEVNULL,
        )
        rev = out.decode("utf-8").strip()
        return rev or None
    except Exception:
        return None


def add_snippet_to_topic(
    topic_name: str,
    *,
    file_path: str,
    start_line: int,
    end_line: int,
    agent: Optional[str] = None,
    include_snapshot: bool = False,
    backlog_root: Optional[Path] = None,
) -> TopicAddSnippetResult:
    """Collect a code snippet reference into the topic materials buffer.

    Reference-first: stores file+line range+hash (+optional snapshot) in manifest.json.
    """
    if start_line <= 0 or end_line <= 0 or end_line < start_line:
        raise TopicError(
            f"Invalid line range: {start_line}-{end_line}",
            suggestion="Use 1-based inclusive line numbers where end >= start",
        )

    if backlog_root is None:
        backlog_root = _find_backlog_root()

    canonical_name = _normalize_topic_name(topic_name)
    topic_path = get_topic_path(canonical_name, backlog_root)
    manifest_path = topic_path / "manifest.json"
    if not manifest_path.exists():
        raise TopicNotFoundError(canonical_name)

    ws_root = _workspace_root(backlog_root)
    abs_path = Path(file_path)
    if not abs_path.is_absolute():
        abs_path = (ws_root / file_path).resolve()
    if not abs_path.exists() or not abs_path.is_file():
        raise TopicError(
            f"Snippet file not found: {file_path}",
            suggestion="Provide a workspace-relative path or an absolute path",
        )

    # Read snippet text
    raw_lines = abs_path.read_text(encoding="utf-8").splitlines()
    if start_line > len(raw_lines):
        raise TopicError(
            f"Start line out of range: {start_line} > {len(raw_lines)}",
            suggestion="Check the file length and line numbers",
        )
    end_line = min(end_line, len(raw_lines))
    snippet_text = "\n".join(raw_lines[start_line - 1 : end_line])

    sha = hashlib.sha256(snippet_text.encode("utf-8")).hexdigest()
    rel_file = _workspace_relative_path(abs_path, ws_root)

    snippet = SnippetRef(
        repo="local",
        revision=_try_git_revision(ws_root),
        file=rel_file,
        lines=[start_line, end_line],
        hash=f"sha256:{sha}",
        cached_text=snippet_text if include_snapshot else None,
        collected_at=_now_timestamp(),
        collector=(agent.strip() if agent and agent.strip() else None),
    )

    manifest = TopicManifest.load(manifest_path)

    def _same(a: SnippetRef, b: SnippetRef) -> bool:
        return (
            a.repo == b.repo
            and a.revision == b.revision
            and a.file == b.file
            and a.lines == b.lines
            and a.hash == b.hash
        )

    for existing in manifest.snippet_refs:
        if isinstance(existing, SnippetRef) and _same(existing, snippet):
            return TopicAddSnippetResult(topic=canonical_name, snippet=existing, added=False)

    manifest.snippet_refs.append(snippet)
    manifest.updated_at = _now_timestamp()
    manifest.save(manifest_path)

    # Ensure materials dir exists (collector can also drop raw logs/files there).
    (topic_path / "materials").mkdir(parents=True, exist_ok=True)
    return TopicAddSnippetResult(topic=canonical_name, snippet=snippet, added=True)


def distill_topic(
    topic_name: str,
    *,
    backlog_root: Optional[Path] = None,
) -> Path:
    """Generate/overwrite a deterministic brief.generated.md from the manifest.

    brief.generated.md is tool-owned and should remain stable and repeatable. Human-facing
    narrative content belongs in brief.md and/or notes.md.
    """
    if backlog_root is None:
        backlog_root = _find_backlog_root()

    canonical_name = _normalize_topic_name(topic_name)
    topic_path = get_topic_path(canonical_name, backlog_root)
    manifest_path = topic_path / "manifest.json"
    if not manifest_path.exists():
        raise TopicNotFoundError(canonical_name)

    manifest = TopicManifest.load(manifest_path)
    generated_at = _now_timestamp()

    def _to_posix_path(path: Path) -> str:
        return str(path).replace("\\", "/")

    def _try_render_seed_item(uid: str) -> Optional[Tuple[str, str]]:
        """Render a seed item as a human-readable line for brief.generated.md.

        Returns:
            Tuple of (sort_key, line) or None if item cannot be resolved.
        """
        from kano_backlog_ops.workset import _resolve_item_ref

        try:
            item_path, metadata = _resolve_item_ref(uid, backlog_root)
        except Exception:
            return None

        item_id = str(metadata.get("id") or "").strip()
        title = str(metadata.get("title") or "").strip()
        item_type = str(metadata.get("type") or "").strip()
        state = str(metadata.get("state") or "").strip()

        # Prefer repo-relative paths for readability/clickability.
        try:
            repo_root = backlog_root.parent.parent
            item_path = item_path.relative_to(repo_root)
        except Exception:
            pass

        details = ", ".join([p for p in [item_type or None, state or None] if p])
        details_suffix = f" ({details})" if details else ""

        if item_id and title:
            headline = f"{item_id}: {title}"
            sort_key = item_id
        elif item_id:
            headline = item_id
            sort_key = item_id
        elif title:
            headline = title
            sort_key = title
        else:
            return None

        # Keep UID available for deterministic cross-reference without cluttering
        # the human-readable listing.
        line = f"- {headline}{details_suffix} - `{_to_posix_path(item_path)}` <!-- uid: {uid} -->"
        return sort_key, line

    rendered_items: List[Tuple[str, str]] = []
    unresolved_uids: List[str] = []
    for uid in sorted(manifest.seed_items):
        rendered = _try_render_seed_item(uid)
        if rendered is None:
            unresolved_uids.append(uid)
        else:
            rendered_items.append(rendered)

    rendered_items_sorted = [line for _, line in sorted(rendered_items, key=lambda x: x[0])]
    if unresolved_uids:
        rendered_items_sorted.extend([f"- {uid}" for uid in sorted(unresolved_uids)])

    items = "\n".join(rendered_items_sorted) or "- (none)"
    docs = "\n".join(f"- {p}" for p in sorted(manifest.pinned_docs)) or "- (none)"
    snippets_lines: List[str] = []
    for s in sorted(
        manifest.snippet_refs,
        key=lambda x: (x.file, x.lines[0] if x.lines else 0, x.lines[1] if len(x.lines) > 1 else 0, x.hash),
    ):
        rng = "" if not s.lines else f"#L{s.lines[0]}-L{s.lines[1]}"
        snippets_lines.append(f"- {s.file}{rng} ({s.hash})")
    snippets = "\n".join(snippets_lines) or "- (none)"

    # Generate related topics section
    related_topics = "\n".join(f"- {topic}" for topic in sorted(manifest.related_topics)) or "- (none)"

    # Detect specs
    spec_section = ""
    spec_dir = topic_path / "spec"
    if spec_dir.exists():
        req_path = spec_dir / "requirements.md"
        des_path = spec_dir / "design.md"
        tsk_path = spec_dir / "tasks.md"
        
        spec_links = []
        if req_path.exists(): spec_links.append("[Requirements](spec/requirements.md)")
        if des_path.exists(): spec_links.append("[Design](spec/design.md)")
        if tsk_path.exists(): spec_links.append("[Tasks](spec/tasks.md)")
        
        if spec_links:
            spec_section = (
                "## Specification\n\n"
                f"{' | '.join(spec_links)}\n\n"
            )
            # Update manifest if needed
            if not manifest.has_spec:
                 manifest.has_spec = True

    brief = (
        f"# Topic Brief (Generated): {canonical_name}\n\n"
        f"Generated: {generated_at}\n\n"
        "Note: This file is generated by `topic distill` and is overwritten on every run.\n"
        "Put narrative summaries and decisions in `brief.md` (human) and/or `notes.md`.\n\n"
        "## Related Topics\n\n"
        f"{related_topics}\n\n"
        "## Materials Index (Deterministic)\n\n"
        "### Items\n"
        f"{items}\n\n"
        "### Pinned Docs\n"
        f"{docs}\n\n"
        f"{spec_section}"
        "### Snippet Refs\n"
        f"{snippets}\n"
    )

    brief_path = topic_path / "brief.generated.md"
    brief_path.write_text(brief, encoding="utf-8")

    manifest.updated_at = generated_at
    manifest.save(manifest_path)
    return brief_path


def _extract_decisions_from_markdown(text: str) -> List[str]:
    """Extract decision bullet lines from markdown under Decision headings."""
    decisions: List[str] = []
    in_section = False
    for raw in text.splitlines():
        line = raw.rstrip()
        heading_match = re.match(r"^(#{2,6})\s+(.*)$", line)
        if heading_match:
            title = heading_match.group(2).strip().lower()
            if "decision" in title:
                in_section = True
                continue
            if in_section and heading_match.group(1).count("#") >= 3:
                decisions.append(heading_match.group(2).strip())
                continue
            in_section = False
            continue
        if not in_section:
            continue
        bullet_match = re.match(r"^[-*]\s+(.+)$", line)
        if bullet_match:
            decisions.append(bullet_match.group(1).strip())
            continue
        decision_line = re.match(r"^\*\*Decision\*\*:\s*(.+)$", line)
        if decision_line:
            decisions.append(decision_line.group(1).strip())
    return decisions


def _extract_frontmatter_decisions(text: str) -> List[str]:
    """Extract decisions list from YAML frontmatter (minimal parser)."""
    lines = text.splitlines()
    if not lines or not lines[0].strip() == "---":
        return []
    decisions: List[str] = []
    in_frontmatter = True
    in_decisions = False
    for line in lines[1:]:
        if line.strip() == "---":
            break
        if re.match(r"^decisions:\s*$", line.strip()):
            in_decisions = True
            continue
        if in_decisions:
            if re.match(r"^\w+?:", line):
                in_decisions = False
                continue
            item_match = re.match(r"^\s*-\s*(.+)$", line)
            if item_match:
                decisions.append(item_match.group(1).strip())
    return [d for d in decisions if d]


def _has_decisions_in_body(text: str) -> bool:
    """Check if workitem body contains a Decisions section with content."""
    lines = text.splitlines()
    in_section = False
    for raw in lines:
        line = raw.rstrip()
        heading_match = re.match(r"^(#{2,6})\s+(.*)$", line)
        if heading_match:
            title = heading_match.group(2).strip().lower()
            in_section = title.startswith("decisions")
            continue
        if not in_section:
            continue
        if line.strip() == "":
            continue
        if line.strip().startswith("#"):
            in_section = False
            continue
        return True
    return False


def generate_decision_audit_report(
    topic_name: str,
    *,
    backlog_root: Optional[Path] = None,
) -> TopicDecisionAuditResult:
    """Generate a deterministic decision write-back audit report for a topic."""
    if backlog_root is None:
        backlog_root = _find_backlog_root()

    canonical_name = _normalize_topic_name(topic_name)
    topic_path = get_topic_path(canonical_name, backlog_root)
    manifest_path = topic_path / "manifest.json"
    if not manifest_path.exists():
        raise TopicNotFoundError(canonical_name)

    manifest = TopicManifest.load(manifest_path)
    repo_root = backlog_root.parent.parent

    # Collect decisions from synthesis markdowns
    synthesis_dir = topic_path / "synthesis"
    decisions: List[Tuple[str, str]] = []
    sources_scanned: List[str] = []
    if synthesis_dir.exists():
        for md_path in sorted(synthesis_dir.glob("*.md")):
            sources_scanned.append(str(md_path.relative_to(repo_root)))
            text = md_path.read_text(encoding="utf-8")
            for d in _extract_decisions_from_markdown(text):
                decisions.append((d, str(md_path.relative_to(repo_root))))

    # Resolve workitems from seed_items
    from kano_backlog_ops.workset import _resolve_item_ref

    items_with_writeback: List[str] = []
    items_missing_writeback: List[str] = []
    item_paths: List[str] = []
    for uid in sorted(manifest.seed_items):
        try:
            item_path, _metadata = _resolve_item_ref(uid, backlog_root)
        except Exception:
            continue
        try:
            rel_path = str(item_path.relative_to(repo_root))
        except Exception:
            rel_path = str(item_path)
        item_paths.append(rel_path)
        content = item_path.read_text(encoding="utf-8")
        frontmatter_decisions = _extract_frontmatter_decisions(content)
        has_body_decisions = _has_decisions_in_body(content)
        if frontmatter_decisions or has_body_decisions:
            items_with_writeback.append(rel_path)
        else:
            items_missing_writeback.append(rel_path)

    report_lines: List[str] = []
    report_lines.append(f"# Decision Write-back Audit: {canonical_name}\n")
    report_lines.append(f"Generated: {_now_timestamp()}\n")
    report_lines.append("## Summary\n")
    report_lines.append(f"- Decisions found in synthesis: {len(decisions)}")
    report_lines.append(f"- Workitems checked: {len(item_paths)}")
    report_lines.append(f"- Workitems with decisions: {len(items_with_writeback)}")
    report_lines.append(f"- Workitems missing decisions: {len(items_missing_writeback)}\n")

    report_lines.append("## Decisions (from synthesis)\n")
    if decisions:
        for idx, (text, src) in enumerate(decisions, start=1):
            report_lines.append(f"{idx}. {text} (source: `{src}`)")
    else:
        report_lines.append("- (none)\n")

    report_lines.append("\n## Workitems Missing Decision Write-back\n")
    if items_missing_writeback:
        for path in items_missing_writeback:
            report_lines.append(f"- `{path}`")
    else:
        report_lines.append("- (none)\n")

    report_lines.append("\n## Workitems With Decision Write-back\n")
    if items_with_writeback:
        for path in items_with_writeback:
            report_lines.append(f"- `{path}`")
    else:
        report_lines.append("- (none)\n")

    publish_dir = topic_path / "publish"
    publish_dir.mkdir(parents=True, exist_ok=True)
    report_path = publish_dir / "decision-audit.md"
    report_path.write_text("\n".join(report_lines).strip() + "\n", encoding="utf-8")

    return TopicDecisionAuditResult(
        topic=canonical_name,
        report_path=report_path,
        decisions_found=len(decisions),
        items_total=len(item_paths),
        items_with_writeback=items_with_writeback,
        items_missing_writeback=items_missing_writeback,
        sources_scanned=sources_scanned,
    )


def reopen_topic(
    topic_name: str,
    *,
    agent: Optional[str] = None,
    backlog_root: Optional[Path] = None,
) -> TopicReopenResult:
    """Reopen a closed topic for additional work.
    
    This allows resuming work on a previously closed topic,
    useful for iterative development or when requirements change.
    """
    if backlog_root is None:
        backlog_root = _find_backlog_root()

    canonical_name = _normalize_topic_name(topic_name)
    topic_path = get_topic_path(canonical_name, backlog_root)
    manifest_path = topic_path / "manifest.json"
    if not manifest_path.exists():
        raise TopicNotFoundError(canonical_name)

    manifest = TopicManifest.load(manifest_path)
    previous_status = manifest.status
    
    if manifest.status == "open":
        return TopicReopenResult(
            topic=canonical_name, 
            reopened=False, 
            reopened_at=manifest.updated_at,
            previous_status=previous_status
        )

    ts = _now_timestamp()
    manifest.status = "open"
    manifest.closed_at = None  # Clear closed timestamp
    manifest.updated_at = ts
    manifest.save(manifest_path)
    
    return TopicReopenResult(
        topic=canonical_name, 
        reopened=True, 
        reopened_at=ts,
        previous_status=previous_status
    )


def close_topic(
    topic_name: str,
    *,
    agent: Optional[str] = None,
    backlog_root: Optional[Path] = None,
) -> TopicCloseResult:
    if backlog_root is None:
        backlog_root = _find_backlog_root()

    canonical_name = _normalize_topic_name(topic_name)
    topic_path = get_topic_path(canonical_name, backlog_root)
    manifest_path = topic_path / "manifest.json"
    if not manifest_path.exists():
        raise TopicNotFoundError(canonical_name)

    manifest = TopicManifest.load(manifest_path)
    if manifest.status == "closed":
        return TopicCloseResult(topic=canonical_name, closed=False, closed_at=manifest.closed_at or "")

    ts = _now_timestamp()
    manifest.status = "closed"
    manifest.closed_at = ts
    manifest.updated_at = ts
    manifest.save(manifest_path)
    return TopicCloseResult(topic=canonical_name, closed=True, closed_at=ts)


def cleanup_topics(
    *,
    ttl_days: int,
    backlog_root: Optional[Path] = None,
    dry_run: bool = True,
    delete_topic_dir: bool = False,
) -> TopicCleanupResult:
    """Cleanup raw materials for closed topics older than ttl_days.

    Default behavior deletes materials/ only; optionally deletes the whole topic dir.
    """
    if ttl_days <= 0:
        raise TopicError("ttl_days must be > 0")

    if backlog_root is None:
        backlog_root = _find_backlog_root()

    topics_root = get_topics_root(backlog_root)
    if not topics_root.exists():
        return TopicCleanupResult(0, 0, 0, [])

    now = datetime.now(timezone.utc)
    cutoff = now.timestamp() - (ttl_days * 24 * 3600)

    topics_scanned = 0
    topics_cleaned = 0
    materials_deleted = 0
    deleted_paths: List[Path] = []

    for topic_dir in sorted([p for p in topics_root.iterdir() if p.is_dir()]):
        topics_scanned += 1
        manifest_path = topic_dir / "manifest.json"
        if not manifest_path.exists():
            continue
        try:
            manifest = TopicManifest.load(manifest_path)
        except Exception:
            continue
        if manifest.status != "closed" or not manifest.closed_at:
            continue
        try:
            closed_dt = datetime.fromisoformat(manifest.closed_at.replace("Z", "+00:00"))
        except Exception:
            continue
        if closed_dt.timestamp() > cutoff:
            continue

        targets: List[Path] = []
        materials_dir = topic_dir / "materials"
        if materials_dir.exists():
            targets.append(materials_dir)
        if delete_topic_dir:
            targets = [topic_dir]

        if not targets:
            continue

        topics_cleaned += 1
        for target in targets:
            if dry_run:
                deleted_paths.append(target)
                continue
            # Delete directory tree
            for child in target.rglob("*"):
                pass
            import shutil

            shutil.rmtree(target, ignore_errors=True)
            deleted_paths.append(target)
            materials_deleted += 1

    return TopicCleanupResult(
        topics_scanned=topics_scanned,
        topics_cleaned=topics_cleaned,
        materials_deleted=materials_deleted,
        deleted_paths=deleted_paths,
    )


def add_item_to_topic(
    topic_name: str,
    item_ref: str,
    *,
    backlog_root: Optional[Path] = None,
) -> TopicAddResult:
    """
    Add an item to a topic.

    Args:
        topic_name: Topic name
        item_ref: Item reference (ID, UID, or path)
        backlog_root: Root path for backlog

    Returns:
        TopicAddResult with add details

    Raises:
        TopicNotFoundError: If topic does not exist
        ItemNotFoundError: If item does not exist
    """
    # Import here to avoid circular imports
    from kano_backlog_ops.workset import _resolve_item_ref, ItemNotFoundError as WorksetItemNotFoundError

    # Resolve backlog root
    if backlog_root is None:
        backlog_root = _find_backlog_root()

    # Get topic path and verify it exists (Requirement 7.5)
    canonical_name = _normalize_topic_name(topic_name)
    topic_path = get_topic_path(canonical_name, backlog_root)
    manifest_path = topic_path / "manifest.json"

    if not manifest_path.exists():
        raise TopicNotFoundError(canonical_name)

    # Verify item exists (Requirement 7.2)
    try:
        _, item_metadata = _resolve_item_ref(item_ref, backlog_root)
    except WorksetItemNotFoundError as e:
        # Re-raise with topic-specific error
        raise TopicError(
            f"Item not found: {item_ref}",
            suggestion="Check the item ID, UID, or path is correct",
        ) from e

    # Get item UID (prefer UID, fallback to ID)
    item_uid = item_metadata.get("uid") or item_metadata.get("id", item_ref)

    # Load manifest
    manifest = TopicManifest.load(manifest_path)

    # Check if item is already in topic (Requirement 7.4)
    if item_uid in manifest.seed_items:
        return TopicAddResult(
            topic=canonical_name,
            item_uid=item_uid,
            added=False,
        )

    # Add item UID to seed_items (Requirement 7.1)
    manifest.seed_items.append(item_uid)

    # Update timestamp (Requirement 7.3)
    now = datetime.now(timezone.utc)
    manifest.updated_at = now.isoformat().replace("+00:00", "Z")

    # Save updated manifest
    manifest.save(manifest_path)

    return TopicAddResult(
        topic=canonical_name,
        item_uid=item_uid,
        added=True,
    )


def pin_document(
    topic_name: str,
    doc_path: str,
    *,
    backlog_root: Optional[Path] = None,
) -> TopicPinResult:
    """
    Pin a document to a topic.

    Args:
        topic_name: Topic name
        doc_path: Document path (relative to backlog root)
        backlog_root: Root path for backlog

    Returns:
        TopicPinResult with pin details

    Raises:
        TopicNotFoundError: If topic does not exist
        TopicError: If document does not exist
    """
    # Resolve backlog root
    if backlog_root is None:
        backlog_root = _find_backlog_root()

    # Get topic path and verify it exists (Requirement 8.1)
    canonical_name = _normalize_topic_name(topic_name)
    topic_path = get_topic_path(canonical_name, backlog_root)
    manifest_path = topic_path / "manifest.json"

    if not manifest_path.exists():
        raise TopicNotFoundError(canonical_name)

    # Normalize document path (support relative paths from backlog root)
    # Requirement 8.4: Support relative paths from backlog root
    if Path(doc_path).is_absolute():
        full_doc_path = Path(doc_path)
        # Convert to relative path for storage
        try:
            relative_doc_path = str(full_doc_path.relative_to(backlog_root.parent.parent))
        except ValueError:
            relative_doc_path = doc_path
    else:
        relative_doc_path = doc_path
        # Resolve full path for existence check
        full_doc_path = backlog_root.parent.parent / doc_path

    # Verify document exists (Requirement 8.2)
    if not full_doc_path.exists():
        raise TopicError(
            f"Document not found: {doc_path}",
            suggestion="Check the document path is correct (relative to workspace root)",
        )

    # Load manifest
    manifest = TopicManifest.load(manifest_path)

    # Check if document is already pinned (Requirement 8.3)
    if relative_doc_path in manifest.pinned_docs:
        return TopicPinResult(
            topic=canonical_name,
            doc_path=relative_doc_path,
            pinned=False,
        )

    # Add document path to pinned_docs (Requirement 8.1)
    manifest.pinned_docs.append(relative_doc_path)

    # Update timestamp
    now = datetime.now(timezone.utc)
    manifest.updated_at = now.isoformat().replace("+00:00", "Z")

    # Save updated manifest
    manifest.save(manifest_path)

    return TopicPinResult(
        topic=canonical_name,
        doc_path=relative_doc_path,
        pinned=True,
    )


def switch_topic(
    topic_name: str,
    *,
    agent: str,
    backlog_root: Optional[Path] = None,
) -> TopicSwitchResult:
    """
    Switch active topic for an agent (KABSD-TSK-0257).

    Updates both the shared state.json and legacy active_topic.<agent>.txt
    for backward compatibility.

    Args:
        topic_name: Topic name to switch to
        agent: Agent identity (e.g., 'copilot', 'codex')
        backlog_root: Root path for backlog

    Returns:
        TopicSwitchResult with switch details

    Raises:
        TopicNotFoundError: If topic does not exist
    """
    # Resolve backlog root
    if backlog_root is None:
        backlog_root = _find_backlog_root()

    # Get topic path and verify it exists
    canonical_name = _normalize_topic_name(topic_name)
    topic_path = get_topic_path(canonical_name, backlog_root)
    manifest_path = topic_path / "manifest.json"

    if not manifest_path.exists():
        raise TopicNotFoundError(canonical_name)

    # Load manifest to get counts
    manifest = TopicManifest.load(manifest_path)

    # Get previous active topic (if any)
    previous_topic = get_active_topic(agent, backlog_root=backlog_root)

    # Load shared state (KABSD-TSK-0257)
    state = load_state_index(backlog_root)

    # Find or create topic state document
    # First check if there's already a state doc for this topic
    topic_id = None
    topics_dir = get_state_topics_dir(backlog_root)
    if topics_dir.exists():
        for topic_file in topics_dir.glob("*.json"):
            try:
                doc = TopicStateDocument.load(topic_file)
                if doc.name == canonical_name:
                    topic_id = doc.topic_id
                    break
            except Exception:
                pass

    # If no existing topic state, create new one
    if not topic_id:
        topic_id = generate_topic_id()
        now = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
        topic_doc = TopicStateDocument(
            topic_id=topic_id,
            name=canonical_name,
            participants=[agent],
            status="active",
            created_at=now,
            updated_at=now,
            created_by=agent,
        )
        save_topic_state(topic_doc, backlog_root)
    else:
        # Update existing topic state to add participant if needed
        topic_doc = load_topic_state(topic_id, backlog_root)
        if topic_doc and agent not in topic_doc.participants:
            topic_doc.participants.append(agent)
            now = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
            topic_doc.updated_at = now
            save_topic_state(topic_doc, backlog_root)

    # Update agent state in shared state.json
    now = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
    agent_state = AgentTopicState(
        agent_id=agent,
        active_topic_id=topic_id,
        updated_at=now,
    )
    state.agents[agent] = agent_state
    save_state_index(state, backlog_root)

    # Also update legacy active_topic.<agent>.txt for backward compatibility
    active_topic_path = get_active_topic_path(agent, backlog_root)
    active_topic_path.parent.mkdir(parents=True, exist_ok=True)
    active_topic_path.write_text(canonical_name, encoding="utf-8")

    # Return summary with item count and pinned doc count
    return TopicSwitchResult(
        topic=canonical_name,
        item_count=len(manifest.seed_items),
        pinned_doc_count=len(manifest.pinned_docs),
        previous_topic=previous_topic,
    )


def get_active_topic(
    agent: str,
    *,
    backlog_root: Optional[Path] = None,
) -> Optional[str]:
    """
    Get current active topic for an agent (KABSD-TSK-0257).

    Tries to read from shared state.json first, then falls back to
    legacy active_topic.<agent>.txt for backward compatibility.

    Args:
        agent: Agent identity (e.g., 'copilot', 'codex')
        backlog_root: Root path for backlog

    Returns:
        Topic name or None if no active topic

    Note:
        Returns None gracefully if state is unreadable; never raises.
    """
    # Resolve backlog root
    if backlog_root is None:
        try:
            backlog_root = _find_backlog_root()
        except TopicError:
            return None

    # Try to load from shared state.json first (KABSD-TSK-0257)
    try:
        state = load_state_index(backlog_root)
        if agent in state.agents:
            agent_state = state.agents[agent]
            if agent_state.active_topic_id:
                # Load the topic state document to get the name
                topic_doc = load_topic_state(agent_state.active_topic_id, backlog_root)
                if topic_doc:
                    return topic_doc.name
    except Exception:
        pass  # Fallback to legacy file

    # Fallback to legacy active_topic.<agent>.txt file
    active_topic_path = get_active_topic_path(agent, backlog_root)
    if not active_topic_path.exists():
        return None

    try:
        topic_name = _normalize_topic_name(active_topic_path.read_text(encoding="utf-8"))
        return topic_name if topic_name else None
    except Exception:
        return None


def export_topic_context(
    topic_name: str,
    *,
    backlog_root: Optional[Path] = None,
    format: str = "markdown",
) -> TopicContextBundle:
    """
    Export topic context as a bundle.

    Args:
        topic_name: Topic name
        backlog_root: Root path for backlog
        format: Output format ("markdown" or "json")

    Returns:
        TopicContextBundle with exported context

    Raises:
        TopicNotFoundError: If topic does not exist
    """
    # Import here to avoid circular imports
    from kano_backlog_ops.workset import _resolve_item_ref, _parse_item_frontmatter

    # Resolve backlog root
    if backlog_root is None:
        backlog_root = _find_backlog_root()

    # Get topic path and verify it exists
    canonical_name = _normalize_topic_name(topic_name)
    topic_path = get_topic_path(canonical_name, backlog_root)
    manifest_path = topic_path / "manifest.json"

    if not manifest_path.exists():
        raise TopicNotFoundError(canonical_name)

    # Load manifest
    manifest = TopicManifest.load(manifest_path)

    # Generate timestamp
    now = datetime.now(timezone.utc)
    generated_at = now.isoformat().replace("+00:00", "Z")

    # Load item summaries (Requirement 10.2)
    items: List[Dict[str, Any]] = []
    for item_uid in sorted(manifest.seed_items):  # Sort for deterministic output
        try:
            item_path, metadata = _resolve_item_ref(item_uid, backlog_root)
            
            # Extract summary (title, state, key sections)
            item_summary = {
                "uid": item_uid,
                "id": metadata.get("id", ""),
                "title": metadata.get("title", ""),
                "type": metadata.get("type", ""),
                "state": metadata.get("state", ""),
                "priority": metadata.get("priority", ""),
                "path": str(item_path.relative_to(backlog_root.parent.parent)),
            }
            items.append(item_summary)
        except Exception:
            # Skip items that can't be resolved (may have been deleted)
            items.append({
                "uid": item_uid,
                "error": "Item not found or could not be loaded",
            })

    # Load pinned document content (Requirement 10.3)
    pinned_docs: List[Dict[str, str]] = []
    workspace_root = backlog_root.parent.parent
    for doc_path_str in sorted(manifest.pinned_docs):  # Sort for deterministic output
        doc_path = workspace_root / doc_path_str
        doc_entry = {
            "path": doc_path_str,
        }
        if doc_path.exists():
            try:
                doc_entry["content"] = doc_path.read_text(encoding="utf-8")
            except Exception as e:
                doc_entry["error"] = f"Could not read: {e}"
        else:
            doc_entry["error"] = "Document not found"
        pinned_docs.append(doc_entry)

    return TopicContextBundle(
        topic=canonical_name,
        items=items,
        pinned_docs=pinned_docs,
        generated_at=generated_at,
    )


def list_topics(
    *,
    backlog_root: Optional[Path] = None,
    agent: Optional[str] = None,
) -> List[TopicManifest]:
    """
    List all topics.

    Args:
        backlog_root: Root path for backlog
        agent: Optional agent to check active topic for

    Returns:
        List of TopicManifest for all topics (sorted by topic name)
    """
    # Resolve backlog root
    if backlog_root is None:
        try:
            backlog_root = _find_backlog_root()
        except TopicError:
            return []

    # Get topics root
    topics_root = get_topics_root(backlog_root)

    if not topics_root.exists():
        return []

    # Scan for topic directories
    topics: List[TopicManifest] = []
    for topic_dir in sorted(topics_root.iterdir()):  # Sort for deterministic output
        if not topic_dir.is_dir():
            continue
        
        manifest_path = topic_dir / "manifest.json"
        if not manifest_path.exists():
            continue
        
        try:
            manifest = TopicManifest.load(manifest_path)
            topics.append(manifest)
        except Exception:
            # Skip invalid manifests
            continue

    return topics


def add_topic_reference(
    topic_name: str,
    referenced_topic: str,
    *,
    backlog_root: Optional[Path] = None,
) -> TopicAddReferenceResult:
    """
    Add a reference from one topic to another with bidirectional linking.

    Args:
        topic_name: Source topic name
        referenced_topic: Target topic name to reference
        backlog_root: Root path for backlog

    Returns:
        TopicAddReferenceResult with reference details

    Raises:
        TopicNotFoundError: If either topic does not exist
        TopicError: If trying to reference self or reference limit exceeded
    """
    # Resolve backlog root
    if backlog_root is None:
        backlog_root = _find_backlog_root()

    # Normalize topic names
    canonical_source = _normalize_topic_name(topic_name)
    canonical_target = _normalize_topic_name(referenced_topic)

    # Prevent self-reference
    if canonical_source == canonical_target:
        raise TopicError(
            f"Cannot reference self: {topic_name}",
            suggestion="Reference a different topic",
        )

    # Get topic paths and verify both exist
    source_path = get_topic_path(canonical_source, backlog_root)
    target_path = get_topic_path(canonical_target, backlog_root)
    
    source_manifest_path = source_path / "manifest.json"
    target_manifest_path = target_path / "manifest.json"

    if not source_manifest_path.exists():
        raise TopicNotFoundError(canonical_source)
    
    if not target_manifest_path.exists():
        raise TopicNotFoundError(canonical_target)

    # Load both manifests
    source_manifest = TopicManifest.load(source_manifest_path)
    target_manifest = TopicManifest.load(target_manifest_path)

    # Check if reference already exists
    if canonical_target in source_manifest.related_topics:
        return TopicAddReferenceResult(
            topic=canonical_source,
            referenced_topic=canonical_target,
            added=False,
        )

    # Check reference limit (5-10 references to prevent overuse)
    MAX_REFERENCES = 10
    if len(source_manifest.related_topics) >= MAX_REFERENCES:
        raise TopicError(
            f"Reference limit exceeded: {len(source_manifest.related_topics)}/{MAX_REFERENCES}",
            suggestion=f"Remove some references before adding new ones",
        )

    # Add bidirectional references
    now = _now_timestamp()
    
    # Add forward reference (source -> target)
    source_manifest.related_topics.append(canonical_target)
    source_manifest.updated_at = now
    source_manifest.save(source_manifest_path)

    # Add backward reference (target -> source) if not already present
    if canonical_source not in target_manifest.related_topics:
        # Check target reference limit too
        if len(target_manifest.related_topics) < MAX_REFERENCES:
            target_manifest.related_topics.append(canonical_source)
            target_manifest.updated_at = now
            target_manifest.save(target_manifest_path)

    return TopicAddReferenceResult(
        topic=canonical_source,
        referenced_topic=canonical_target,
        added=True,
    )


def get_topic_snapshots_path(topic_name: str, backlog_root: Optional[Path] = None) -> Path:
    """
    Get the snapshots directory path for a topic.

    Args:
        topic_name: Topic name
        backlog_root: Root path for backlog

    Returns:
        Path to _kano/backlog/topics/<topic-name>/snapshots/
    """
    topic_path = get_topic_path(topic_name, backlog_root)
    return topic_path / "snapshots"


def _generate_snapshot_filename(snapshot_name: str, timestamp: str) -> str:
    """Generate a filename for a snapshot."""
    # Use timestamp prefix for chronological ordering
    timestamp_prefix = timestamp.replace(":", "").replace("-", "").replace(".", "")[:15]
    safe_name = re.sub(r'[^\w\-_]', '_', snapshot_name)
    return f"{timestamp_prefix}_{safe_name}.json"


def _compress_content(content: str) -> bytes:
    """Compress content using gzip."""
    import gzip
    return gzip.compress(content.encode('utf-8'))


def _decompress_content(compressed_data: bytes) -> str:
    """Decompress gzipped content."""
    import gzip
    return gzip.decompress(compressed_data).decode('utf-8')


def get_topic_snapshots_path(topic_name: str, backlog_root: Optional[Path] = None) -> Path:
    """
    Get the snapshots directory path for a topic.

    Args:
        topic_name: Topic name
        backlog_root: Root path for backlog

    Returns:
        Path to _kano/backlog/topics/<topic-name>/snapshots/
    """
    topic_path = get_topic_path(topic_name, backlog_root)
    return topic_path / "snapshots"


def _generate_snapshot_filename(snapshot_name: str, timestamp: str) -> str:
    """Generate a filename for a snapshot."""
    # Use timestamp prefix for chronological ordering
    timestamp_prefix = timestamp.replace(":", "").replace("-", "").replace(".", "")[:15]
    safe_name = re.sub(r'[^\w\-_]', '_', snapshot_name)
    return f"{timestamp_prefix}_{safe_name}.json"


def remove_topic_reference(
    topic_name: str,
    referenced_topic: str,
    *,
    backlog_root: Optional[Path] = None,
) -> TopicRemoveReferenceResult:
    """
    Remove a reference from one topic to another with bidirectional cleanup.

    Args:
        topic_name: Source topic name
        referenced_topic: Target topic name to unreference
        backlog_root: Root path for backlog

    Returns:
        TopicRemoveReferenceResult with removal details

    Raises:
        TopicNotFoundError: If source topic does not exist
    """
    # Resolve backlog root
    if backlog_root is None:
        backlog_root = _find_backlog_root()

    # Normalize topic names
    canonical_source = _normalize_topic_name(topic_name)
    canonical_target = _normalize_topic_name(referenced_topic)

    # Get source topic path and verify it exists
    source_path = get_topic_path(canonical_source, backlog_root)
    source_manifest_path = source_path / "manifest.json"

    if not source_manifest_path.exists():
        raise TopicNotFoundError(canonical_source)

    # Load source manifest
    source_manifest = TopicManifest.load(source_manifest_path)

    # Check if reference exists
    if canonical_target not in source_manifest.related_topics:
        return TopicRemoveReferenceResult(
            topic=canonical_source,
            referenced_topic=canonical_target,
            removed=False,
        )

    # Remove forward reference (source -> target)
    source_manifest.related_topics.remove(canonical_target)
    source_manifest.updated_at = _now_timestamp()
    source_manifest.save(source_manifest_path)

    # Remove backward reference (target -> source) if target exists
    target_path = get_topic_path(canonical_target, backlog_root)
    target_manifest_path = target_path / "manifest.json"
    
    if target_manifest_path.exists():
        try:
            target_manifest = TopicManifest.load(target_manifest_path)
            if canonical_source in target_manifest.related_topics:
                target_manifest.related_topics.remove(canonical_source)
                target_manifest.updated_at = _now_timestamp()
                target_manifest.save(target_manifest_path)
        except Exception:
            # Ignore errors in backward cleanup - forward removal is primary
            pass

    return TopicRemoveReferenceResult(
        topic=canonical_source,
        referenced_topic=canonical_target,
        removed=True,
    )


def create_topic_snapshot(
    topic_name: str,
    snapshot_name: str,
    *,
    description: str = "",
    agent: str,
    backlog_root: Optional[Path] = None,
    include_materials: bool = True,
) -> TopicSnapshotResult:
    """
    Create a snapshot of a topic's current state.

    Args:
        topic_name: Topic name
        snapshot_name: Name for the snapshot
        description: Optional description of the snapshot
        agent: Agent creating the snapshot
        backlog_root: Root path for backlog
        include_materials: Whether to include materials in snapshot

    Returns:
        TopicSnapshotResult with snapshot details

    Raises:
        TopicNotFoundError: If topic does not exist
        TopicError: If snapshot name is invalid or already exists
    """
    # Validate snapshot name
    if not snapshot_name or not snapshot_name.strip():
        raise TopicError("Snapshot name cannot be empty")
    
    snapshot_name = snapshot_name.strip()
    if len(snapshot_name) > 64:
        raise TopicError(f"Snapshot name too long ({len(snapshot_name)} chars, max 64)")

    # Resolve backlog root
    if backlog_root is None:
        backlog_root = _find_backlog_root()

    # Get topic path and verify it exists
    canonical_name = _normalize_topic_name(topic_name)
    topic_path = get_topic_path(canonical_name, backlog_root)
    manifest_path = topic_path / "manifest.json"

    if not manifest_path.exists():
        raise TopicNotFoundError(canonical_name)

    # Create snapshots directory
    snapshots_path = get_topic_snapshots_path(canonical_name, backlog_root)
    snapshots_path.mkdir(parents=True, exist_ok=True)

    # Generate snapshot metadata
    now = _now_timestamp()

    # Enforce unique snapshot_name within a topic.
    # Filenames include timestamps, so we must check by the sanitized name.
    safe_name = re.sub(r"[^\w\-_]", "_", snapshot_name)
    existing = list(snapshots_path.glob(f"*_{safe_name}.json"))
    if existing:
        raise TopicError(f"Snapshot '{snapshot_name}' already exists")

    snapshot_filename = _generate_snapshot_filename(snapshot_name, now)
    snapshot_path = snapshots_path / snapshot_filename

    # Check if snapshot already exists
    if snapshot_path.exists():
        raise TopicError(f"Snapshot '{snapshot_name}' already exists")

    # Load current manifest
    manifest = TopicManifest.load(manifest_path)

    # Collect content
    brief_content = None
    brief_path = topic_path / "brief.generated.md"
    if brief_path.exists():
        brief_content = brief_path.read_text(encoding="utf-8")

    notes_content = None
    notes_path = topic_path / "notes.md"
    if notes_path.exists():
        notes_content = notes_path.read_text(encoding="utf-8")

    # Collect materials index if requested
    materials_index = {}
    if include_materials:
        materials_path = topic_path / "materials"
        if materials_path.exists():
            for file_path in materials_path.rglob("*"):
                if file_path.is_file():
                    try:
                        content = file_path.read_text(encoding="utf-8")
                        content_hash = hashlib.sha256(content.encode("utf-8")).hexdigest()
                        rel_path = str(file_path.relative_to(materials_path))
                        materials_index[rel_path] = content_hash
                    except Exception:
                        # Skip files that can't be read as text
                        pass

    # Create snapshot object
    snapshot = TopicSnapshot(
        name=snapshot_name,
        topic=canonical_name,
        created_at=now,
        created_by=agent,
        description=description,
        manifest=manifest,
        brief_content=brief_content,
        notes_content=notes_content,
        materials_index=materials_index,
    )

    # Save snapshot
    snapshot_data = {
        "name": snapshot.name,
        "topic": snapshot.topic,
        "created_at": snapshot.created_at,
        "created_by": snapshot.created_by,
        "description": snapshot.description,
        "manifest": snapshot.manifest.to_dict(),
        "brief_content": snapshot.brief_content,
        "notes_content": snapshot.notes_content,
        "materials_index": snapshot.materials_index,
    }

    with open(snapshot_path, "w", encoding="utf-8") as f:
        json.dump(snapshot_data, f, indent=2, ensure_ascii=False)

    return TopicSnapshotResult(
        topic=canonical_name,
        snapshot_name=snapshot_name,
        snapshot_path=snapshot_path,
        created_at=now,
    )


def list_topic_snapshots(
    topic_name: str,
    *,
    backlog_root: Optional[Path] = None,
) -> TopicSnapshotListResult:
    """
    List all snapshots for a topic.

    Args:
        topic_name: Topic name
        backlog_root: Root path for backlog

    Returns:
        TopicSnapshotListResult with snapshot metadata

    Raises:
        TopicNotFoundError: If topic does not exist
    """
    # Resolve backlog root
    if backlog_root is None:
        backlog_root = _find_backlog_root()

    # Get topic path and verify it exists
    canonical_name = _normalize_topic_name(topic_name)
    topic_path = get_topic_path(canonical_name, backlog_root)
    manifest_path = topic_path / "manifest.json"

    if not manifest_path.exists():
        raise TopicNotFoundError(canonical_name)

    # Get snapshots directory
    snapshots_path = get_topic_snapshots_path(canonical_name, backlog_root)
    
    snapshots = []
    if snapshots_path.exists():
        for snapshot_file in sorted(snapshots_path.glob("*.json")):
            try:
                with open(snapshot_file, "r", encoding="utf-8") as f:
                    snapshot_data = json.load(f)
                
                snapshots.append({
                    "name": snapshot_data.get("name", ""),
                    "created_at": snapshot_data.get("created_at", ""),
                    "created_by": snapshot_data.get("created_by", ""),
                    "description": snapshot_data.get("description", ""),
                    "file_path": str(snapshot_file),
                })
            except Exception:
                # Skip corrupted snapshot files
                continue

    return TopicSnapshotListResult(
        topic=canonical_name,
        snapshots=snapshots,
    )


def restore_topic_snapshot(
    topic_name: str,
    snapshot_name: str,
    *,
    agent: str,
    backlog_root: Optional[Path] = None,
    restore_manifest: bool = True,
    restore_brief: bool = True,
    restore_notes: bool = True,
    backup_current: bool = True,
) -> TopicRestoreResult:
    """
    Restore a topic from a snapshot.

    Args:
        topic_name: Topic name
        snapshot_name: Name of the snapshot to restore
        agent: Agent performing the restore
        backlog_root: Root path for backlog
        restore_manifest: Whether to restore manifest.json
        restore_brief: Whether to restore brief.generated.md
        restore_notes: Whether to restore notes.md
        backup_current: Whether to create a backup before restoring

    Returns:
        TopicRestoreResult with restore details

    Raises:
        TopicNotFoundError: If topic or snapshot does not exist
        TopicError: If restore operation fails
    """
    # Resolve backlog root
    if backlog_root is None:
        backlog_root = _find_backlog_root()

    # Get topic path and verify it exists
    canonical_name = _normalize_topic_name(topic_name)
    topic_path = get_topic_path(canonical_name, backlog_root)
    manifest_path = topic_path / "manifest.json"

    if not manifest_path.exists():
        raise TopicNotFoundError(canonical_name)

    # Find snapshot file
    snapshots_path = get_topic_snapshots_path(canonical_name, backlog_root)
    snapshot_file = None
    
    if snapshots_path.exists():
        for candidate in snapshots_path.glob("*.json"):
            try:
                with open(candidate, "r", encoding="utf-8") as f:
                    snapshot_data = json.load(f)
                if snapshot_data.get("name") == snapshot_name:
                    snapshot_file = candidate
                    break
            except Exception:
                continue

    if not snapshot_file:
        raise TopicError(f"Snapshot '{snapshot_name}' not found")

    # Load snapshot data
    try:
        with open(snapshot_file, "r", encoding="utf-8") as f:
            snapshot_data = json.load(f)
    except Exception as e:
        raise TopicError(f"Failed to load snapshot: {e}")

    # Create backup if requested
    if backup_current:
        backup_name = f"auto_backup_before_{snapshot_name}_{_now_timestamp().replace(':', '').replace('-', '')[:15]}"
        try:
            create_topic_snapshot(
                topic_name,
                backup_name,
                description=f"Automatic backup before restoring '{snapshot_name}'",
                agent=agent,
                backlog_root=backlog_root,
            )
        except Exception:
            # Continue with restore even if backup fails
            pass

    # Perform restore operations
    restored_components = []
    now = _now_timestamp()

    try:
        # Restore manifest
        if restore_manifest and "manifest" in snapshot_data:
            manifest_data = snapshot_data["manifest"]
            # Update timestamps to reflect restore
            manifest_data["updated_at"] = now
            
            restored_manifest = TopicManifest.from_dict(manifest_data)
            restored_manifest.save(manifest_path)
            restored_components.append("manifest")

        # Restore brief.generated.md
        if restore_brief and snapshot_data.get("brief_content"):
            brief_path = topic_path / "brief.generated.md"
            brief_path.write_text(snapshot_data["brief_content"], encoding="utf-8")
            restored_components.append("brief")

        # Restore notes.md
        if restore_notes and snapshot_data.get("notes_content"):
            notes_path = topic_path / "notes.md"
            notes_path.write_text(snapshot_data["notes_content"], encoding="utf-8")
            restored_components.append("notes")

    except Exception as e:
        raise TopicError(f"Restore operation failed: {e}")

    return TopicRestoreResult(
        topic=canonical_name,
        snapshot_name=snapshot_name,
        restored_at=now,
        restored_components=restored_components,
    )


def cleanup_topic_snapshots(
    topic_name: str,
    *,
    ttl_days: int = 30,
    keep_latest: int = 5,
    backlog_root: Optional[Path] = None,
    dry_run: bool = True,
) -> Dict[str, Any]:
    """
    Clean up old topic snapshots based on TTL and count limits.

    Args:
        topic_name: Topic name
        ttl_days: Delete snapshots older than N days
        keep_latest: Always keep N most recent snapshots
        backlog_root: Root path for backlog
        dry_run: If True, only report what would be deleted

    Returns:
        Dictionary with cleanup results

    Raises:
        TopicNotFoundError: If topic does not exist
    """
    # Resolve backlog root
    if backlog_root is None:
        backlog_root = _find_backlog_root()

    # Get topic path and verify it exists
    canonical_name = _normalize_topic_name(topic_name)
    topic_path = get_topic_path(canonical_name, backlog_root)
    manifest_path = topic_path / "manifest.json"

    if not manifest_path.exists():
        raise TopicNotFoundError(canonical_name)

    # Get snapshots directory
    snapshots_path = get_topic_snapshots_path(canonical_name, backlog_root)
    
    if not snapshots_path.exists():
        return {
            "topic": canonical_name,
            "snapshots_scanned": 0,
            "snapshots_deleted": 0,
            "deleted_files": [],
            "dry_run": dry_run,
        }

    # Collect snapshot files with metadata
    snapshots = []
    for snapshot_file in snapshots_path.glob("*.json"):
        try:
            with open(snapshot_file, "r", encoding="utf-8") as f:
                snapshot_data = json.load(f)
            
            created_at = snapshot_data.get("created_at", "")
            if created_at:
                created_dt = datetime.fromisoformat(created_at.replace("Z", "+00:00"))
                snapshots.append({
                    "file": snapshot_file,
                    "name": snapshot_data.get("name", ""),
                    "created_at": created_dt,
                })
        except Exception:
            # Skip corrupted files
            continue

    # Sort by creation time (newest first)
    snapshots.sort(key=lambda x: x["created_at"], reverse=True)

    # Determine which snapshots to delete
    now = datetime.now(timezone.utc)
    cutoff = now - timedelta(days=ttl_days)
    
    to_delete = []
    for i, snapshot in enumerate(snapshots):
        # Always keep the latest N snapshots
        if i < keep_latest:
            continue
        
        # Delete if older than TTL
        if snapshot["created_at"] < cutoff:
            to_delete.append(snapshot)

    # Perform deletion
    deleted_files = []
    if not dry_run:
        for snapshot in to_delete:
            try:
                snapshot["file"].unlink()
                deleted_files.append(str(snapshot["file"]))
            except Exception:
                # Continue with other deletions
                pass
    else:
        deleted_files = [str(s["file"]) for s in to_delete]

    return {
        "topic": canonical_name,
        "snapshots_scanned": len(snapshots),
        "snapshots_deleted": len(deleted_files),
        "deleted_files": deleted_files,
        "dry_run": dry_run,
    }

def split_topic(
    source_topic: str,
    split_config: Dict[str, List[str]],
    *,
    agent: str,
    backlog_root: Optional[Path] = None,
    dry_run: bool = False,
    create_snapshots: bool = True,
) -> TopicSplitResult:
    """
    Split a topic into multiple focused subtopics.

    Args:
        source_topic: Name of the topic to split
        split_config: Dict mapping new topic names to lists of item UIDs
        agent: Agent performing the split
        backlog_root: Root path for backlog
        dry_run: If True, only return what would be done
        create_snapshots: Whether to create snapshots before splitting

    Returns:
        TopicSplitResult with split details

    Raises:
        TopicNotFoundError: If source topic does not exist
        TopicError: If split configuration is invalid or operation fails
    """
    # Validate split configuration
    if not split_config:
        raise TopicError("Split configuration cannot be empty")
    
    for new_topic, items in split_config.items():
        if not new_topic or not new_topic.strip():
            raise TopicError("New topic names cannot be empty")
        validation_errors = validate_topic_name(new_topic)
        if validation_errors:
            raise TopicError(f"Invalid topic name '{new_topic}': {validation_errors[0]}")

    # Resolve backlog root
    if backlog_root is None:
        backlog_root = _find_backlog_root()

    # Get source topic and verify it exists
    canonical_source = _normalize_topic_name(source_topic)
    source_path = get_topic_path(canonical_source, backlog_root)
    source_manifest_path = source_path / "manifest.json"

    if not source_manifest_path.exists():
        raise TopicNotFoundError(canonical_source)

    # Load source manifest
    source_manifest = TopicManifest.load(source_manifest_path)

    # Validate that all specified items exist in source topic
    all_split_items = set()
    for items in split_config.values():
        all_split_items.update(items)
    
    missing_items = all_split_items - set(source_manifest.seed_items)
    if missing_items:
        raise TopicError(f"Items not found in source topic: {', '.join(missing_items)}")

    # Check for new topic name conflicts
    for new_topic in split_config.keys():
        canonical_new = _normalize_topic_name(new_topic)
        new_path = get_topic_path(canonical_new, backlog_root)
        if new_path.exists():
            raise TopicError(f"Target topic already exists: {new_topic}")

    if dry_run:
        # Return plan without making changes
        new_topics_info = []
        for new_topic, items in split_config.items():
            new_topics_info.append({
                "name": new_topic,
                "items": items,
                "materials": [],  # Would need to implement material distribution logic
            })
        
        return TopicSplitPlan(
            source_topic=canonical_source,
            new_topics=new_topics_info,
            conflicts=[],
            references_to_update=[],
        )

    # Create snapshots if requested
    if create_snapshots:
        try:
            create_topic_snapshot(
                canonical_source,
                f"before_split_{_now_timestamp().replace(':', '').replace('-', '')[:15]}",
                description=f"Automatic snapshot before splitting into {len(split_config)} topics",
                agent=agent,
                backlog_root=backlog_root,
            )
        except Exception:
            # Continue with split even if snapshot fails
            pass

    now = _now_timestamp()
    items_redistributed = {}
    materials_redistributed = {}
    references_updated = []

    try:
        # Create new topics and redistribute items
        for new_topic, items_to_move in split_config.items():
            canonical_new = _normalize_topic_name(new_topic)
            
            # Create new topic
            create_result = create_topic(
                new_topic,
                agent=agent,
                backlog_root=backlog_root,
                create_notes=True,
                create_brief=True,
            )
            
            # Update new topic's manifest with assigned items
            new_manifest = create_result.manifest
            new_manifest.seed_items = items_to_move.copy()
            new_manifest.updated_at = now
            new_manifest.save(create_result.topic_path / "manifest.json")
            
            items_redistributed[canonical_new] = items_to_move.copy()
            materials_redistributed[canonical_new] = []

        # Update source topic manifest (remove split items)
        remaining_items = [item for item in source_manifest.seed_items 
                          if item not in all_split_items]
        source_manifest.seed_items = remaining_items
        source_manifest.updated_at = now
        
        # Add worklog entry to source topic
        split_summary = ", ".join(f"{topic}({len(items)})" for topic, items in split_config.items())
        worklog_entry = f"{now} [agent={agent}] Split topic into: {split_summary}"
        
        # Update source manifest
        source_manifest.save(source_manifest_path)

        # Update cross-references if source topic was referenced
        topics_list = list_topics(backlog_root=backlog_root)
        for topic_manifest in topics_list:
            if canonical_source in topic_manifest.related_topics:
                # Add references to all new topics
                for new_topic in split_config.keys():
                    canonical_new = _normalize_topic_name(new_topic)
                    if canonical_new not in topic_manifest.related_topics:
                        topic_manifest.related_topics.append(canonical_new)
                
                topic_manifest.updated_at = now
                topic_path = get_topic_path(topic_manifest.topic, backlog_root)
                topic_manifest.save(topic_path / "manifest.json")
                references_updated.append(topic_manifest.topic)

    except Exception as e:
        raise TopicError(f"Split operation failed: {e}")

    return TopicSplitResult(
        source_topic=canonical_source,
        new_topics=list(split_config.keys()),
        items_redistributed=items_redistributed,
        materials_redistributed=materials_redistributed,
        references_updated=references_updated,
        split_at=now,
    )


def merge_topics(
    target_topic: str,
    source_topics: List[str],
    *,
    agent: str,
    backlog_root: Optional[Path] = None,
    dry_run: bool = False,
    create_snapshots: bool = True,
    delete_source_topics: bool = False,
) -> TopicMergeResult:
    """
    Merge multiple topics into a target topic.

    Args:
        target_topic: Name of the topic to merge into
        source_topics: List of topic names to merge from
        agent: Agent performing the merge
        backlog_root: Root path for backlog
        dry_run: If True, only return what would be done
        create_snapshots: Whether to create snapshots before merging
        delete_source_topics: Whether to delete source topics after merge

    Returns:
        TopicMergeResult with merge details

    Raises:
        TopicNotFoundError: If any topic does not exist
        TopicError: If merge operation fails
    """
    if not source_topics:
        raise TopicError("Source topics list cannot be empty")

    # Resolve backlog root
    if backlog_root is None:
        backlog_root = _find_backlog_root()

    # Validate all topics exist
    canonical_target = _normalize_topic_name(target_topic)
    target_path = get_topic_path(canonical_target, backlog_root)
    target_manifest_path = target_path / "manifest.json"

    if not target_manifest_path.exists():
        raise TopicNotFoundError(canonical_target)

    canonical_sources = []
    source_manifests = []
    
    for source_topic in source_topics:
        canonical_source = _normalize_topic_name(source_topic)
        source_path = get_topic_path(canonical_source, backlog_root)
        source_manifest_path = source_path / "manifest.json"
        
        if not source_manifest_path.exists():
            raise TopicNotFoundError(canonical_source)
        
        canonical_sources.append(canonical_source)
        source_manifests.append(TopicManifest.load(source_manifest_path))

    # Load target manifest
    target_manifest = TopicManifest.load(target_manifest_path)

    if dry_run:
        # Analyze conflicts
        item_conflicts = []
        material_conflicts = []
        
        # Check for item conflicts
        target_items = set(target_manifest.seed_items)
        for source_manifest in source_manifests:
            conflicts = target_items.intersection(set(source_manifest.seed_items))
            item_conflicts.extend(conflicts)
        
        return TopicMergePlan(
            target_topic=canonical_target,
            source_topics=canonical_sources,
            item_conflicts=item_conflicts,
            material_conflicts=material_conflicts,
            references_to_update=[],
        )

    # Create snapshots if requested
    if create_snapshots:
        all_topics = [canonical_target] + canonical_sources
        for topic in all_topics:
            try:
                create_topic_snapshot(
                    topic,
                    f"before_merge_{_now_timestamp().replace(':', '').replace('-', '')[:15]}",
                    description=f"Automatic snapshot before merge operation",
                    agent=agent,
                    backlog_root=backlog_root,
                )
            except Exception:
                # Continue with merge even if snapshot fails
                pass

    now = _now_timestamp()
    items_merged = {}
    materials_merged = {}
    references_updated = []

    try:
        # Merge items from source topics into target
        for i, source_manifest in enumerate(source_manifests):
            canonical_source = canonical_sources[i]
            
            # Merge items (avoid duplicates)
            source_items = source_manifest.seed_items.copy()
            new_items = [item for item in source_items if item not in target_manifest.seed_items]
            target_manifest.seed_items.extend(new_items)
            
            items_merged[canonical_source] = source_items
            materials_merged[canonical_source] = []

        # Update target manifest
        target_manifest.updated_at = now
        target_manifest.save(target_manifest_path)

        # Update cross-references
        topics_list = list_topics(backlog_root=backlog_root)
        for topic_manifest in topics_list:
            updated = False
            
            # Replace references to source topics with target topic
            for canonical_source in canonical_sources:
                if canonical_source in topic_manifest.related_topics:
                    topic_manifest.related_topics.remove(canonical_source)
                    if canonical_target not in topic_manifest.related_topics:
                        topic_manifest.related_topics.append(canonical_target)
                    updated = True
            
            if updated:
                topic_manifest.updated_at = now
                topic_path = get_topic_path(topic_manifest.topic, backlog_root)
                topic_manifest.save(topic_path / "manifest.json")
                references_updated.append(topic_manifest.topic)

        # Delete source topics if requested
        if delete_source_topics:
            for canonical_source in canonical_sources:
                source_path = get_topic_path(canonical_source, backlog_root)
                try:
                    import shutil
                    shutil.rmtree(source_path)
                except Exception:
                    # Continue even if deletion fails
                    pass

    except Exception as e:
        raise TopicError(f"Merge operation failed: {e}")

    return TopicMergeResult(
        target_topic=canonical_target,
        merged_topics=canonical_sources,
        items_merged=items_merged,
        materials_merged=materials_merged,
        references_updated=references_updated,
        merged_at=now,
    )


def update_worksets_after_merge(
    target_topic: str,
    source_topics: List[str],
    *,
    backlog_root: Optional[Path] = None,
) -> None:
    """Update shared worksets state after merging topics.

    - Union participants into target topic state document
    - Mark source topic state documents as closed
    - Repoint any agent active_topic_id from source -> target
    - Persist state.json and topic state documents
    """
    if backlog_root is None:
        backlog_root = _find_backlog_root()

    canonical_target = _normalize_topic_name(target_topic)
    canonical_sources = [_normalize_topic_name(s) for s in source_topics]

    # Load state index
    state = load_state_index(backlog_root)

    # Map topic name -> topic_id via topics state docs
    topics_dir = get_state_topics_dir(backlog_root)
    name_to_doc: Dict[str, TopicStateDocument] = {}
    if topics_dir.exists():
        for jf in topics_dir.glob("*.json"):
            try:
                doc = TopicStateDocument.load(jf)
                name_to_doc[doc.name] = doc
            except Exception:
                pass

    target_doc = name_to_doc.get(canonical_target)
    if not target_doc:
        # Create target doc if missing
        target_doc = TopicStateDocument(
            topic_id=generate_topic_id(),
            name=canonical_target,
            participants=[],
            status="active",
            created_at=_now_timestamp(),
            updated_at=_now_timestamp(),
            created_by="",
        )
        save_topic_state(target_doc, backlog_root)

    # Union participants and mark sources closed
    target_participants = set(target_doc.participants)
    source_ids: List[str] = []
    now = _now_timestamp()
    for src in canonical_sources:
        src_doc = name_to_doc.get(src)
        if src_doc:
            source_ids.append(src_doc.topic_id)
            for p in src_doc.participants:
                target_participants.add(p)
            src_doc.status = "closed"
            src_doc.updated_at = now
            save_topic_state(src_doc, backlog_root)
    target_doc.participants = sorted(target_participants)
    target_doc.updated_at = now
    save_topic_state(target_doc, backlog_root)

    # Repoint agents from source topic IDs to target topic ID
    for agent_id, agent_state in list(state.agents.items()):
        if agent_state.active_topic_id and agent_state.active_topic_id in source_ids:
            agent_state.active_topic_id = target_doc.topic_id
            agent_state.updated_at = now
            state.agents[agent_id] = agent_state
            # Also update legacy txt
            atxt = get_active_topic_path(agent_id, backlog_root)
            atxt.parent.mkdir(parents=True, exist_ok=True)
            atxt.write_text(canonical_target, encoding="utf-8")

    save_state_index(state, backlog_root)
