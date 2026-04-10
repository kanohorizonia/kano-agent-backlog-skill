"""
Evidence operations for managing evidence records attached to worksets.

Requirements: T5 (EvidenceRecord schema), T6 (workset integration)
"""

from __future__ import annotations

import json
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional

from kano_backlog_core.models import EvidenceRecord


# =============================================================================
# Error Types
# =============================================================================


class EvidenceError(Exception):
    """Base exception for evidence operations."""

    def __init__(self, message: str, suggestion: Optional[str] = None):
        self.message = message
        self.suggestion = suggestion
        super().__init__(message)


class EvidenceNotFoundError(EvidenceError):
    """Evidence record not found."""

    def __init__(self, evidence_id: str, item_id: str, suggestion: Optional[str] = None):
        self.evidence_id = evidence_id
        self.item_id = item_id
        super().__init__(
            f"Evidence record '{evidence_id}' not found for item '{item_id}'",
            suggestion=suggestion,
        )


class EvidenceValidationError(EvidenceError):
    """Evidence validation failed."""

    def __init__(self, errors: List[str]):
        self.errors = errors
        error_list = "\n".join(f"  - {e}" for e in errors)
        super().__init__(f"Evidence validation failed:\n{error_list}")


# =============================================================================
# Data Models
# =============================================================================


@dataclass
class EvidenceAddResult:
    """Result of adding an evidence record."""

    evidence_id: str
    item_id: str
    workset_path: Path
    created: bool  # True if newly added, False if already existed


@dataclass
class EvidenceListResult:
    """Result of listing evidence records."""

    item_id: str
    evidence_count: int
    evidence_records: List[EvidenceRecord]


# =============================================================================
# Evidence Storage
# =============================================================================


def _get_evidence_store_path(item_id: str, backlog_root: Optional[Path] = None) -> Path:
    """
    Get the path to an item's evidence store JSON file.

    Args:
        item_id: Item ID (e.g., KABSD-TSK-0124)
        backlog_root: Root path for backlog

    Returns:
        Path to _kano/backlog/.cache/worksets/items/<item-id>/evidence.json
    """
    from kano_backlog_ops.workset import get_item_workset_path

    if backlog_root is None:
        from kano_backlog_ops.workset import _find_backlog_root

        backlog_root = _find_backlog_root()

    workset_path = get_item_workset_path(item_id, backlog_root)
    return workset_path / "evidence.json"


def _load_evidence_store(path: Path) -> List[Dict[str, Any]]:
    """
    Load evidence store from JSON file.

    Args:
        path: Path to evidence.json

    Returns:
        List of evidence record dicts
    """
    if not path.exists():
        return []
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def _save_evidence_store(path: Path, records: List[Dict[str, Any]]) -> None:
    """
    Save evidence store to JSON file.

    Args:
        path: Path to evidence.json
        records: List of evidence record dicts
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(records, f, indent=2, ensure_ascii=False)


def _resolve_item_ref(item_ref: str, backlog_root: Optional[Path] = None) -> str:
    """
    Resolve an item reference to an item ID.

    Args:
        item_ref: Item reference (ID, UID, or path)
        backlog_root: Root path for backlog

    Returns:
        Resolved item ID
    """
    from kano_backlog_ops.workset import _resolve_item_ref as _resolve

    if backlog_root is None:
        from kano_backlog_ops.workset import _find_backlog_root

        backlog_root = _find_backlog_root()

    _, metadata = _resolve(item_ref, backlog_root)
    return metadata["id"]


# =============================================================================
# Public API
# =============================================================================


def add_evidence(
    item_ref: str,
    claim_id: str,
    source: str,
    content: str,
    *,
    relevance: float = 0.5,
    reliability: float = 0.5,
    sufficiency: float = 0.5,
    verifiability: float = 0.5,
    independence: float = 0.5,
    notes: Optional[str] = None,
    evidence_id: Optional[str] = None,
    backlog_root: Optional[Path] = None,
) -> EvidenceAddResult:
    """
    Add an evidence record to a workset's evidence store.

    Args:
        item_ref: Item reference (ID, UID, or path)
        claim_id: ID of the claim this evidence supports
        source: Source of the evidence (item ID, URL, etc.)
        content: The evidence content/text
        relevance: Relevance score (0.0-1.0)
        reliability: Reliability score (0.0-1.0)
        sufficiency: Sufficiency score (0.0-1.0)
        verifiability: Verifiability score (0.0-1.0)
        independence: Independence score (0.0-1.0)
        notes: Optional notes about this evidence
        evidence_id: Optional explicit evidence ID (auto-generated if not provided)
        backlog_root: Root path for backlog

    Returns:
        EvidenceAddResult with operation details
    """
    from kano_backlog_ops.workset import get_item_workset_path

    # Resolve item reference to item ID
    item_id = _resolve_item_ref(item_ref, backlog_root)

    if backlog_root is None:
        from kano_backlog_ops.workset import _find_backlog_root

        backlog_root = _find_backlog_root()

    workset_path = get_item_workset_path(item_id, backlog_root)
    evidence_path = workset_path / "evidence.json"

    # Generate evidence ID if not provided
    if evidence_id is None:
        evidence_id = str(uuid.uuid4())[:8]

    # Create evidence record
    record = EvidenceRecord(
        id=evidence_id,
        claim_id=claim_id,
        source=source,
        content=content,
        relevance=relevance,
        reliability=reliability,
        sufficiency=sufficiency,
        verifiability=verifiability,
        independence=independence,
        notes=notes,
    )

    # Load existing records
    records = _load_evidence_store(evidence_path)

    # Check for duplicate
    existing_ids = [r["id"] for r in records]
    created = evidence_id not in existing_ids

    # Add or replace
    if created:
        records.append(record.to_dict())
    else:
        # Replace existing record with same ID
        for i, r in enumerate(records):
            if r["id"] == evidence_id:
                records[i] = record.to_dict()
                break

    # Save
    _save_evidence_store(evidence_path, records)

    return EvidenceAddResult(
        evidence_id=evidence_id,
        item_id=item_id,
        workset_path=workset_path,
        created=created,
    )


def list_evidence(
    item_ref: str,
    claim_id: Optional[str] = None,
    backlog_root: Optional[Path] = None,
) -> EvidenceListResult:
    """
    List evidence records for a workset.

    Args:
        item_ref: Item reference (ID, UID, or path)
        claim_id: Optional claim ID to filter by
        backlog_root: Root path for backlog

    Returns:
        EvidenceListResult with matching records
    """
    from kano_backlog_ops.workset import get_item_workset_path

    # Resolve item reference to item ID
    item_id = _resolve_item_ref(item_ref, backlog_root)

    if backlog_root is None:
        from kano_backlog_ops.workset import _find_backlog_root

        backlog_root = _find_backlog_root()

    workset_path = get_item_workset_path(item_id, backlog_root)
    evidence_path = workset_path / "evidence.json"

    # Load records
    records = _load_evidence_store(evidence_path)

    # Convert to EvidenceRecord objects
    evidence_records: List[EvidenceRecord] = []
    for r in records:
        try:
            record = EvidenceRecord(
                id=r["id"],
                claim_id=r["claim_id"],
                source=r["source"],
                content=r["content"],
                relevance=r.get("relevance", 0.5),
                reliability=r.get("reliability", 0.5),
                sufficiency=r.get("sufficiency", 0.5),
                verifiability=r.get("verifiability", 0.5),
                independence=r.get("independence", 0.5),
                notes=r.get("notes"),
            )
            evidence_records.append(record)
        except (KeyError, TypeError) as exc:
            # Skip malformed records
            continue

    # Filter by claim_id if provided
    if claim_id is not None:
        evidence_records = [e for e in evidence_records if e.claim_id == claim_id]

    return EvidenceListResult(
        item_id=item_id,
        evidence_count=len(evidence_records),
        evidence_records=evidence_records,
    )


def get_evidence(
    item_ref: str,
    evidence_id: str,
    backlog_root: Optional[Path] = None,
) -> EvidenceRecord:
    """
    Get a specific evidence record by ID.

    Args:
        item_ref: Item reference (ID, UID, or path)
        evidence_id: Evidence record ID
        backlog_root: Root path for backlog

    Returns:
        EvidenceRecord

    Raises:
        EvidenceNotFoundError: If record not found
    """
    result = list_evidence(item_ref, backlog_root=backlog_root)

    for record in result.evidence_records:
        if record.id == evidence_id:
            return record

    item_id = _resolve_item_ref(item_ref, backlog_root)
    raise EvidenceNotFoundError(
        evidence_id,
        item_id,
        suggestion="Run 'evidence list' to see all evidence IDs for this item",
    )


def delete_evidence(
    item_ref: str,
    evidence_id: str,
    backlog_root: Optional[Path] = None,
) -> bool:
    """
    Delete an evidence record by ID.

    Args:
        item_ref: Item reference (ID, UID, or path)
        evidence_id: Evidence record ID
        backlog_root: Root path for backlog

    Returns:
        True if deleted, False if not found
    """
    from kano_backlog_ops.workset import get_item_workset_path

    # Resolve item reference to item ID
    item_id = _resolve_item_ref(item_ref, backlog_root)

    if backlog_root is None:
        from kano_backlog_ops.workset import _find_backlog_root

        backlog_root = _find_backlog_root()

    workset_path = get_item_workset_path(item_id, backlog_root)
    evidence_path = workset_path / "evidence.json"

    # Load records
    records = _load_evidence_store(evidence_path)

    # Find and remove
    original_count = len(records)
    records = [r for r in records if r["id"] != evidence_id]

    if len(records) == original_count:
        return False

    # Save
    _save_evidence_store(evidence_path, records)
    return True


def compute_evidence_summary(
    item_ref: str,
    backlog_root: Optional[Path] = None,
) -> Dict[str, Any]:
    """
    Compute summary statistics for all evidence records.

    Args:
        item_ref: Item reference (ID, UID, or path)
        backlog_root: Root path for backlog

    Returns:
        Dictionary with summary statistics
    """
    result = list_evidence(item_ref, backlog_root=backlog_root)

    if not result.evidence_records:
        return {
            "item_id": result.item_id,
            "evidence_count": 0,
            "avg_relevance": 0.0,
            "avg_reliability": 0.0,
            "avg_sufficiency": 0.0,
            "avg_verifiability": 0.0,
            "avg_independence": 0.0,
            "avg_overall": 0.0,
        }

    n = result.evidence_count
    return {
        "item_id": result.item_id,
        "evidence_count": n,
        "avg_relevance": sum(e.relevance for e in result.evidence_records) / n,
        "avg_reliability": sum(e.reliability for e in result.evidence_records) / n,
        "avg_sufficiency": sum(e.sufficiency for e in result.evidence_records) / n,
        "avg_verifiability": sum(e.verifiability for e in result.evidence_records) / n,
        "avg_independence": sum(e.independence for e in result.evidence_records) / n,
        "avg_overall": sum(e.overall_score() for e in result.evidence_records) / n,
    }
