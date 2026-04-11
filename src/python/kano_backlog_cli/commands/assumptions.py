from __future__ import annotations

from pathlib import Path
from typing import List, Optional
import json

import typer

from ..util import ensure_core_on_path

app = typer.Typer(help="Assumptions registry operations")

__all__ = ["app"]


@app.command("list")
def list_assumptions(
    *,
    item_refs: List[str] = typer.Option(
        [],
        "--item",
        help="Specific item references to scan (repeatable)",
    ),
    backlog_root: Optional[Path] = typer.Option(
        None,
        "--backlog-root",
        help="Path to _kano/backlog (auto-detected if omitted)",
    ),
    format: str = typer.Option("plain", "--format", help="Output format: plain|json|markdown"),
) -> None:
    """List assumptions extracted from work items."""
    ensure_core_on_path()
    from kano_backlog_ops.assumptions_registry import (
        collect_assumptions,
        _extract_item_id_from_path,
    )

    try:
        assumptions = collect_assumptions(
            item_refs=list(item_refs) if item_refs else None,
            backlog_root=backlog_root,
        )
    except Exception as exc:
        typer.echo(f"❌ Unexpected error: {exc}", err=True)
        raise typer.Exit(2)

    if format == "json":
        payload = []
        for a in assumptions:
            payload.append(
                {
                    "id": a.id,
                    "statement": a.statement,
                    "status": a.status.value if hasattr(a.status, "value") else str(a.status),
                    "source": a.source,
                    "notes": a.notes,
                }
            )
        typer.echo(json.dumps(payload, ensure_ascii=True, indent=2))
        return

    if not assumptions:
        typer.echo("No assumptions found.")
        return

    # Group by item
    by_item: dict[str, list] = {}
    for a in assumptions:
        item_id = _extract_item_id_from_path(a.source)
        if item_id not in by_item:
            by_item[item_id] = []
        by_item[item_id].append(a)

    for item_id, item_assumptions in by_item.items():
        typer.echo(f"\n## {item_id}")
        for a in item_assumptions:
            status_badge = f"[{a.status.value}]" if hasattr(a.status, "value") else f"[{a.status}]"
            typer.echo(f"  {status_badge} {a.statement}")
            if a.notes:
                typer.echo(f"    Note: {a.notes}")


@app.command("generate")
def generate(
    *,
    item_refs: List[str] = typer.Option(
        [],
        "--item",
        help="Specific item references to scan (repeatable)",
    ),
    backlog_root: Optional[Path] = typer.Option(
        None,
        "--backlog-root",
        help="Path to _kano/backlog (auto-detected if omitted)",
    ),
    output: Optional[Path] = typer.Option(
        None,
        "--output",
        "-o",
        help="Output file path (print to stdout if omitted)",
    ),
    format: str = typer.Option("markdown", "--format", help="Output format: markdown|json"),
) -> None:
    """Generate an assumptions registry report."""
    ensure_core_on_path()
    from kano_backlog_ops.assumptions_registry import (
        generate_registry_report,
        format_registry_markdown,
    )

    try:
        report = generate_registry_report(
            item_refs=list(item_refs) if item_refs else None,
            backlog_root=backlog_root,
        )
    except Exception as exc:
        typer.echo(f"❌ Unexpected error: {exc}", err=True)
        raise typer.Exit(2)

    if format == "json":
        payload = {
            "generated_at": report.generated_at,
            "total_items": report.total_items,
            "total_assumptions": report.total_assumptions,
            "by_status": report.by_status,
            "items": [
                {
                    "item_id": s.item_id,
                    "item_title": s.item_title,
                    "stated_count": s.stated_count,
                    "validated_count": s.validated_count,
                    "invalidated_count": s.invalidated_count,
                    "unknown_count": s.unknown_count,
                    "assumptions": [
                        {
                            "id": a.id,
                            "statement": a.statement,
                            "status": a.status.value if hasattr(a.status, "value") else str(a.status),
                            "source": a.source,
                            "notes": a.notes,
                        }
                        for a in s.assumptions
                    ],
                }
                for s in report.item_summaries
            ],
        }
        typer.echo(json.dumps(payload, ensure_ascii=True, indent=2))
        return

    markdown = format_registry_markdown(report)

    if output:
        output.write_text(markdown, encoding="utf-8")
        typer.echo(f"✓ Registry report written to {output}")
    else:
        typer.echo(markdown)