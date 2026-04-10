"""
Evidence CLI commands for managing evidence records attached to worksets.

This module provides the `kano-backlog evidence` command group for:
- Adding evidence records to worksets
- Listing evidence records
- Getting specific evidence records
- Deleting evidence records
- Computing evidence summary statistics

Requirements: T5, T6 (evidence schema + workset integration)
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Optional

import typer

from ..util import ensure_core_on_path

app = typer.Typer(help="Manage evidence records for workset metadata")


@app.command("add")
def add(
    item: str = typer.Option(..., "--item", help="Item ID, UID, or path"),
    claim_id: str = typer.Option(..., "--claim-id", help="ID of the claim this evidence supports"),
    source: str = typer.Option(..., "--source", help="Source of the evidence (item ID, URL, etc.)"),
    content: str = typer.Option(..., "--content", help="The evidence content/text"),
    relevance: float = typer.Option(0.5, "--relevance", help="Relevance score (0.0-1.0)"),
    reliability: float = typer.Option(0.5, "--reliability", help="Reliability score (0.0-1.0)"),
    sufficiency: float = typer.Option(0.5, "--sufficiency", help="Sufficiency score (0.0-1.0)"),
    verifiability: float = typer.Option(0.5, "--verifiability", help="Verifiability score (0.0-1.0)"),
    independence: float = typer.Option(0.5, "--independence", help="Independence score (0.0-1.0)"),
    notes: Optional[str] = typer.Option(None, "--notes", help="Optional notes about this evidence"),
    evidence_id: Optional[str] = typer.Option(None, "--evidence-id", help="Explicit evidence ID (auto-generated if not provided)"),
    backlog_root: Optional[Path] = typer.Option(
        None,
        "--backlog-root",
        help="Path to _kano/backlog (auto-detected if omitted)",
    ),
    output_format: str = typer.Option("plain", "--format", help="Output format: plain|json"),
):
    """Add an evidence record to a workset's evidence store."""
    ensure_core_on_path()
    from kano_backlog_ops.evidence import (
        add_evidence,
        EvidenceValidationError,
        EvidenceError,
    )

    try:
        result = add_evidence(
            item,
            claim_id=claim_id,
            source=source,
            content=content,
            relevance=relevance,
            reliability=reliability,
            sufficiency=sufficiency,
            verifiability=verifiability,
            independence=independence,
            notes=notes,
            evidence_id=evidence_id,
            backlog_root=backlog_root,
        )
    except EvidenceValidationError as exc:
        typer.echo(f"FAIL: {exc.message}", err=True)
        raise typer.Exit(1)
    except EvidenceError as exc:
        typer.echo(f"FAIL: {exc.message}", err=True)
        if exc.suggestion:
            typer.echo(f"   Suggestion: {exc.suggestion}", err=True)
        raise typer.Exit(1)
    except Exception as exc:
        typer.echo(f"FAIL: Unexpected error: {exc}", err=True)
        raise typer.Exit(2)

    if output_format == "json":
        payload = {
            "evidence_id": result.evidence_id,
            "item_id": result.item_id,
            "workset_path": str(result.workset_path),
            "created": result.created,
        }
        typer.echo(json.dumps(payload, ensure_ascii=False))
    else:
        action = "Added" if result.created else "Updated"
        typer.echo(f"OK: {action} evidence {result.evidence_id} for {result.item_id}")


@app.command("list")
def list_cmd(
    item: str = typer.Option(..., "--item", help="Item ID, UID, or path"),
    claim_id: Optional[str] = typer.Option(None, "--claim-id", help="Filter by claim ID"),
    backlog_root: Optional[Path] = typer.Option(
        None,
        "--backlog-root",
        help="Path to _kano/backlog (auto-detected if omitted)",
    ),
    output_format: str = typer.Option("plain", "--format", help="Output format: plain|json"),
):
    """List evidence records for a workset."""
    ensure_core_on_path()
    from kano_backlog_ops.evidence import (
        list_evidence,
        EvidenceError,
    )

    try:
        result = list_evidence(item, claim_id=claim_id, backlog_root=backlog_root)
    except EvidenceError as exc:
        typer.echo(f"FAIL: {exc.message}", err=True)
        if exc.suggestion:
            typer.echo(f"   Suggestion: {exc.suggestion}", err=True)
        raise typer.Exit(1)
    except Exception as exc:
        typer.echo(f"FAIL: Unexpected error: {exc}", err=True)
        raise typer.Exit(2)

    if output_format == "json":
        payload = {
            "item_id": result.item_id,
            "evidence_count": result.evidence_count,
            "evidence_records": [e.to_dict() for e in result.evidence_records],
        }
        typer.echo(json.dumps(payload, ensure_ascii=False, indent=2))
    else:
        if result.evidence_count == 0:
            typer.echo(f"No evidence records found for {result.item_id}")
        else:
            typer.echo(f"Evidence records for {result.item_id} ({result.evidence_count}):")
            for e in result.evidence_records:
                typer.echo(f"  [{e.id}] {e.source}")
                typer.echo(f"    Claim: {e.claim_id}")
                typer.echo(f"    Content: {e.content[:60]}{'...' if len(e.content) > 60 else ''}")
                typer.echo(f"    Scores: rel={e.relevance:.2f} relb={e.reliability:.2f} suff={e.sufficiency:.2f} verif={e.verifiability:.2f} indep={e.independence:.2f}")
                typer.echo(f"    Overall: {e.overall_score():.3f}")
                if e.notes:
                    typer.echo(f"    Notes: {e.notes}")
                typer.echo("")


@app.command("get")
def get(
    item: str = typer.Option(..., "--item", help="Item ID, UID, or path"),
    evidence_id: str = typer.Option(..., "--evidence-id", help="Evidence record ID"),
    output_format: str = typer.Option("plain", "--format", help="Output format: plain|json"),
):
    """Get a specific evidence record by ID."""
    ensure_core_on_path()
    from kano_backlog_ops.evidence import (
        get_evidence,
        EvidenceNotFoundError,
        EvidenceError,
    )

    try:
        record = get_evidence(item, evidence_id)
    except EvidenceNotFoundError as exc:
        typer.echo(f"FAIL: {exc.message}", err=True)
        if exc.suggestion:
            typer.echo(f"   Suggestion: {exc.suggestion}", err=True)
        raise typer.Exit(1)
    except EvidenceError as exc:
        typer.echo(f"FAIL: {exc.message}", err=True)
        if exc.suggestion:
            typer.echo(f"   Suggestion: {exc.suggestion}", err=True)
        raise typer.Exit(1)
    except Exception as exc:
        typer.echo(f"FAIL: Unexpected error: {exc}", err=True)
        raise typer.Exit(2)

    if output_format == "json":
        typer.echo(json.dumps(record.to_dict(), ensure_ascii=False, indent=2))
    else:
        typer.echo(f"Evidence: {record.id}")
        typer.echo(f"  Claim: {record.claim_id}")
        typer.echo(f"  Source: {record.source}")
        typer.echo(f"  Content: {record.content}")
        typer.echo(f"  Scores:")
        typer.echo(f"    Relevance:    {record.relevance:.3f}")
        typer.echo(f"    Reliability:  {record.reliability:.3f}")
        typer.echo(f"    Sufficiency:  {record.sufficiency:.3f}")
        typer.echo(f"    Verifiability: {record.verifiability:.3f}")
        typer.echo(f"    Independence: {record.independence:.3f}")
        typer.echo(f"    Overall:     {record.overall_score():.3f}")
        if record.notes:
            typer.echo(f"  Notes: {record.notes}")


@app.command("delete")
def delete(
    item: str = typer.Option(..., "--item", help="Item ID, UID, or path"),
    evidence_id: str = typer.Option(..., "--evidence-id", help="Evidence record ID"),
    output_format: str = typer.Option("plain", "--format", help="Output format: plain|json"),
):
    """Delete an evidence record by ID."""
    ensure_core_on_path()
    from kano_backlog_ops.evidence import (
        delete_evidence,
        EvidenceError,
    )

    try:
        deleted = delete_evidence(item, evidence_id)
    except EvidenceError as exc:
        typer.echo(f"FAIL: {exc.message}", err=True)
        if exc.suggestion:
            typer.echo(f"   Suggestion: {exc.suggestion}", err=True)
        raise typer.Exit(1)
    except Exception as exc:
        typer.echo(f"FAIL: Unexpected error: {exc}", err=True)
        raise typer.Exit(2)

    if output_format == "json":
        typer.echo(json.dumps({"deleted": deleted}, ensure_ascii=False))
    else:
        if deleted:
            typer.echo(f"OK: Deleted evidence {evidence_id}")
        else:
            typer.echo(f"Evidence {evidence_id} not found")


@app.command("summary")
def summary(
    item: str = typer.Option(..., "--item", help="Item ID, UID, or path"),
    output_format: str = typer.Option("plain", "--format", help="Output format: plain|json"),
):
    """Compute summary statistics for all evidence records."""
    ensure_core_on_path()
    from kano_backlog_ops.evidence import (
        compute_evidence_summary,
        EvidenceError,
    )

    try:
        stats = compute_evidence_summary(item)
    except EvidenceError as exc:
        typer.echo(f"FAIL: {exc.message}", err=True)
        if exc.suggestion:
            typer.echo(f"   Suggestion: {exc.suggestion}", err=True)
        raise typer.Exit(1)
    except Exception as exc:
        typer.echo(f"FAIL: Unexpected error: {exc}", err=True)
        raise typer.Exit(2)

    if output_format == "json":
        typer.echo(json.dumps(stats, ensure_ascii=False, indent=2))
    else:
        typer.echo(f"Evidence summary for {stats['item_id']}:")
        typer.echo(f"  Count: {stats['evidence_count']}")
        if stats["evidence_count"] > 0:
            typer.echo(f"  Avg Scores:")
            typer.echo(f"    Relevance:     {stats['avg_relevance']:.3f}")
            typer.echo(f"    Reliability:   {stats['avg_reliability']:.3f}")
            typer.echo(f"    Sufficiency:   {stats['avg_sufficiency']:.3f}")
            typer.echo(f"    Verifiability: {stats['avg_verifiability']:.3f}")
            typer.echo(f"    Independence:  {stats['avg_independence']:.3f}")
            typer.echo(f"    Overall:      {stats['avg_overall']:.3f}")