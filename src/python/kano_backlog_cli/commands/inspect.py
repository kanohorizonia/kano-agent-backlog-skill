from __future__ import annotations

from pathlib import Path
from typing import List, Optional
import json

import typer

from ..util import ensure_core_on_path

app = typer.Typer(help="Inspector operations")

__all__ = ["app"]


@app.command("health")
def health(
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
    """Run the health review inspector and produce a trust gap report."""
    ensure_core_on_path()
    from kano_backlog_ops.health_inspector import (
        format_health_markdown,
        HealthReport,
        TrustGapFinding,
    )

    # Import the scanner function lazily to avoid circular imports
    from kano_backlog_ops.health_inspector import (
        _check_jargon_credentialism,
        _check_missing_counter_examples,
        _check_single_source_dependency,
        _check_unverifiable_claims,
    )
    from kano_backlog_ops.evidence import list_evidence as _list_evidence
    from kano_backlog_ops.workset import (
        _find_backlog_root as _find_root,
        _resolve_item_ref as _resolve,
    )

    try:
        # Resolve backlog root
        if backlog_root is None:
            backlog_root = _find_root()

        # Collect all item IDs to scan
        if item_refs:
            item_ids = []
            for ref in item_refs:
                _, metadata = _resolve(ref, backlog_root)
                item_ids.append(metadata["id"])
        else:
            # Scan all items
            item_ids = []
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
                                stem = item_path.stem
                                for part in stem.split("_"):
                                    if part.startswith("KABSD-"):
                                        item_ids.append(part)
                                        break

        # Build findings
        all_findings: List[TrustGapFinding] = []
        items_with_issues = set()

        for item_id in item_ids:
            try:
                result = _list_evidence(item_id, backlog_root=backlog_root)
            except Exception:
                result = None

            # Get claim text
            claim_text = _get_item_claim_text(item_id, backlog_root)
            verification_steps = _get_item_verification_steps(item_id, backlog_root)

            # Unpack EvidenceListResult to a list
            if result is None:
                evidence_records_list: List[Any] = []
            else:
                evidence_records_list = result.evidence_records if hasattr(result, 'evidence_records') else list(result)

            # Run checks
            findings = []
            findings.extend(_check_single_source_dependency(item_id, evidence_records_list))
            findings.extend(_check_jargon_credentialism(item_id, claim_text))
            findings.extend(_check_missing_counter_examples(item_id, claim_text, evidence_records_list))
            findings.extend(_check_unverifiable_claims(item_id, claim_text, verification_steps))

            all_findings.extend(findings)
            if findings:
                items_with_issues.add(item_id)

        # Aggregate
        from datetime import datetime, timezone

        now = datetime.now(timezone.utc)
        timestamp = now.isoformat().replace("+00:00", "Z")

        by_severity = {"critical": 0, "warning": 0, "info": 0}
        by_check = {
            "Single Source Dependency": 0,
            "Jargon Credentialism": 0,
            "Missing Counter-examples": 0,
            "Unverifiable Claims": 0,
        }

        for f in all_findings:
            if f.severity in by_severity:
                by_severity[f.severity] += 1
            if f.check_name in by_check:
                by_check[f.check_name] += 1

        report = HealthReport(
            generated_at=timestamp,
            inspector_version="1.0.0",
            total_items_scanned=len(item_ids),
            items_with_issues=len(items_with_issues),
            findings=all_findings,
            by_severity=by_severity,
            by_check=by_check,
        )

    except Exception as exc:
        typer.echo(f"❌ Unexpected error: {exc}", err=True)
        raise typer.Exit(2)

    if format == "json":
        payload = {
            "generated_at": report.generated_at,
            "inspector_version": report.inspector_version,
            "total_items_scanned": report.total_items_scanned,
            "items_with_issues": report.items_with_issues,
            "by_severity": report.by_severity,
            "by_check": report.by_check,
            "findings": [
                {
                    "finding_id": f.finding_id,
                    "check_name": f.check_name,
                    "severity": f.severity,
                    "item_id": f.item_id,
                    "claim": f.claim,
                    "evidence_ref": f.evidence_ref,
                    "recommendation": f.recommendation,
                }
                for f in report.findings
            ],
        }
        typer.echo(json.dumps(payload, ensure_ascii=True, indent=2))
        return

    markdown = format_health_markdown(report)

    if output:
        output.write_text(markdown, encoding="utf-8")
        typer.echo(f"✓ Health report written to {output}")
    else:
        typer.echo(markdown)


def _get_item_claim_text(item_id: str, backlog_root: Path) -> str:
    """Extract the main claim/description from an item."""
    try:
        from kano_backlog_ops.workset import _resolve_item_ref as _resolve

        item_path, _ = _resolve(item_id, backlog_root)
        content = item_path.read_text(encoding="utf-8")
        lines = content.splitlines()
        in_body = False
        body_lines: List[str] = []
        for line in lines:
            if in_body:
                body_lines.append(line)
            elif line.strip() == "---":
                if not in_body:
                    in_body = True
        return "\n".join(body_lines)
    except Exception:
        return ""


def _get_item_verification_steps(item_id: str, backlog_root: Path) -> Optional[List[str]]:
    """Extract verification steps from an item if present."""
    try:
        from kano_backlog_ops.workset import _resolve_item_ref as _resolve

        item_path, _ = _resolve(item_id, backlog_root)
        content = item_path.read_text(encoding="utf-8")
        steps: List[str] = []
        in_steps = False
        for line in content.splitlines():
            stripped = line.strip().lower()
            if "verification" in stripped or "acceptance criteria" in stripped:
                in_steps = True
            elif in_steps and stripped.startswith("#"):
                break
            elif in_steps and stripped:
                steps.append(stripped)
        return steps if steps else None
    except Exception:
        return None