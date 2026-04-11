"""
assumptions_registry.py - Assumptions registry and report generation.

This module provides use-case functions for managing assumptions/priors
across the backlog. Assumptions are collected from work items and
presented in a registry report for inspector consumption.

Per ADR-0037, assumptions support the inspector lane by tracking
validation status of stated assumptions.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional

from kano_backlog_core.errors import BacklogError
from kano_backlog_core.models import Assumption, AssumptionStatus


# =============================================================================
# Error Types
# =============================================================================


class AssumptionsError(BacklogError):
    """Base error for assumptions operations."""

    def __init__(self, message: str, suggestion: Optional[str] = None):
        self.message = message
        self.suggestion = suggestion
        super().__init__(message)


# =============================================================================
# Data Models
# =============================================================================


@dataclass
class AssumptionSummary:
    """Summary of assumptions for an item."""

    item_id: str
    item_title: str
    assumptions: List[Assumption]
    stated_count: int
    validated_count: int
    invalidated_count: int
    unknown_count: int


@dataclass
class RegistryReport:
    """Full assumptions registry report."""

    generated_at: str
    total_items: int
    total_assumptions: int
    by_status: Dict[str, int]
    item_summaries: List[AssumptionSummary]


# =============================================================================
# Assumption Extraction
# =============================================================================


def _parse_item_assumptions(item_path: Path) -> List[Assumption]:
    """
    Parse assumptions from an item file.

    Extracts assumptions from the item body by scanning for
    lines starting with "Assumption:" or "A:" markers.

    Args:
        item_path: Path to the item file

    Returns:
        List of Assumption objects found in the item
    """
    if not item_path.exists():
        return []

    content = item_path.read_text(encoding="utf-8")
    lines = content.splitlines()

    assumptions: List[Assumption] = []
    current_assumption: Optional[Dict[str, Any]] = None

    for line in lines:
        stripped = line.strip()

        # Start of a new assumption
        if stripped.lower().startswith("assumption:") or stripped.lower().startswith("a:"):
            # Save previous if exists
            if current_assumption:
                _finish_assumption(current_assumption, assumptions)

            # Extract text after marker
            text = stripped.split(":", 1)[1].strip() if ":" in stripped else stripped[2:].strip()
            current_assumption = {"statement": text, "source": str(item_path)}

        # Continuation of current assumption
        elif current_assumption and stripped and not stripped.startswith("#"):
            current_assumption["statement"] += " " + stripped

        # Section header or empty line ends assumption
        elif current_assumption:
            _finish_assumption(current_assumption, assumptions)
            current_assumption = None

        # Handle last assumption
        if current_assumption:
            _finish_assumption(current_assumption, assumptions)

    return assumptions


def _finish_assumption(data: Dict[str, Any], output_list: List[Assumption]) -> None:
    """Convert raw assumption dict to Assumption object and append."""
    import uuid

    if data.get("statement"):
        output_list.append(
            Assumption(
                id=str(uuid.uuid4())[:8],
                statement=data["statement"].strip(),
                status=AssumptionStatus.STATED,
                source=data.get("source", ""),
                notes=data.get("notes"),
            )
        )


# =============================================================================
# Registry Operations
# =============================================================================


def collect_assumptions(
    item_refs: Optional[List[str]] = None,
    *,
    backlog_root: Optional[Path] = None,
) -> List[Assumption]:
    """
    Collect assumptions from specified items or all items.

    Args:
        item_refs: Optional list of specific item references
        backlog_root: Root path for backlog

    Returns:
        List of all collected Assumption objects
    """
    if backlog_root is None:
        from kano_backlog_ops.workset import _find_backlog_root

        backlog_root = _find_backlog_root()

    all_assumptions: List[Assumption] = []

    if item_refs:
        # Collect from specific items
        for ref in item_refs:
            from kano_backlog_ops.workset import _resolve_item_ref

            item_path, _ = _resolve_item_ref(ref, backlog_root)
            assumptions = _parse_item_assumptions(item_path)
            all_assumptions.extend(assumptions)
    else:
        # Scan all items
        products_dir = backlog_root / "products"
        if products_dir.exists():
            for product_dir in products_dir.iterdir():
                if not product_dir.is_dir():
                    continue
                items_root = product_dir / "items"
                if not items_root.exists():
                    continue

                for type_dir in items_root.iterdir():
                    if not type_dir.is_dir():
                        continue
                    for bucket_dir in type_dir.iterdir():
                        if not bucket_dir.is_dir():
                            continue
                        for item_path in bucket_dir.glob("*.md"):
                            if item_path.name.endswith(".index.md"):
                                continue
                            assumptions = _parse_item_assumptions(item_path)
                            all_assumptions.extend(assumptions)

    return all_assumptions


def generate_registry_report(
    item_refs: Optional[List[str]] = None,
    *,
    backlog_root: Optional[Path] = None,
) -> RegistryReport:
    """
    Generate a full assumptions registry report.

    Args:
        item_refs: Optional list of specific item references
        backlog_root: Root path for backlog

    Returns:
        RegistryReport with all assumptions grouped by item
    """
    if backlog_root is None:
        from kano_backlog_ops.workset import _find_backlog_root

        backlog_root = _find_backlog_root()

    now = datetime.now(timezone.utc)
    timestamp = now.isoformat().replace("+00:00", "Z")

    # Collect assumptions
    assumptions = collect_assumptions(item_refs, backlog_root=backlog_root)

    # Group by item
    by_item: Dict[str, List[Assumption]] = {}
    for a in assumptions:
        # Extract item ID from source path
        item_id = _extract_item_id_from_path(a.source)
        if item_id not in by_item:
            by_item[item_id] = []
        by_item[item_id].append(a)

    # Build status counts
    by_status: Dict[str, int] = {
        "stated": 0,
        "validated": 0,
        "invalidated": 0,
        "unknown": 0,
    }

    item_summaries: List[AssumptionSummary] = []

    for item_id, item_assumptions in by_item.items():
        stated = validated = invalidated = unknown = 0
        for a in item_assumptions:
            status = a.status.value if hasattr(a.status, "value") else str(a.status)
            if status == "stated":
                stated += 1
            elif status == "validated":
                validated += 1
            elif status == "invalidated":
                invalidated += 1
            else:
                unknown += 1

            if status in by_status:
                by_status[status] += 1

        item_summaries.append(
            AssumptionSummary(
                item_id=item_id,
                item_title=item_assumptions[0].statement[:50],  # Use first assumption as title proxy
                assumptions=item_assumptions,
                stated_count=stated,
                validated_count=validated,
                invalidated_count=invalidated,
                unknown_count=unknown,
            )
        )

    return RegistryReport(
        generated_at=timestamp,
        total_items=len(item_summaries),
        total_assumptions=len(assumptions),
        by_status=by_status,
        item_summaries=item_summaries,
    )


def _extract_item_id_from_path(source_path: str) -> str:
    """Extract item ID from a source file path."""
    path = Path(source_path)
    # Pattern: KABSD-TSK-XXXX_some-slug.md
    for part in path.stem.split("_"):
        if part.startswith("KABSD-"):
            return part
    return path.stem[:20]


# =============================================================================
# Report Formatting
# =============================================================================


def format_registry_markdown(report: RegistryReport) -> str:
    """
    Format registry report as markdown.

    Args:
        report: The registry report to format

    Returns:
        Markdown string representation
    """
    lines = [
        "# Assumptions Registry",
        "",
        f"Generated: {report.generated_at}",
        "",
        "## Summary",
        "",
        f"- Total items with assumptions: {report.total_items}",
        f"- Total assumptions: {report.total_assumptions}",
        "",
        "### By Status",
        "",
    ]

    status_labels = {
        "stated": "Stated (unvalidated)",
        "validated": "Validated",
        "invalidated": "Invalidated",
        "unknown": "Unknown",
    }

    for status, count in report.by_status.items():
        label = status_labels.get(status, status)
        lines.append(f"- **{label}**: {count}")

    lines.append("")
    lines.append("## Items")

    for summary in report.item_summaries:
        lines.append("")
        lines.append(f"### {summary.item_id}")
        lines.append("")
        for a in summary.assumptions:
            status_badge = f"[{a.status.value}]" if hasattr(a.status, "value") else f"[{a.status}]"
            lines.append(f"- {status_badge} {a.statement}")
            if a.notes:
                lines.append(f"  - Note: {a.notes}")
            lines.append(f"  - Source: {a.source}")

    return "\n".join(lines)
