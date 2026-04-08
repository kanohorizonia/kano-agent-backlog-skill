from __future__ import annotations
from typing import Optional, Union, Dict, Any
import json
from pathlib import Path
from typing import List

import typer

from ..util import (
    ensure_core_on_path,
    resolve_backlog_root,
    resolve_product_root,
    find_item_path_by_id,
)

app = typer.Typer()


def _parse_tags(raw: str) -> List[str]:
    """Normalize a comma-separated tag list."""
    return [tag.strip() for tag in raw.split(",") if tag.strip()] if raw else []


def _summarize_sequence_status(status_map: Dict[str, Dict[str, Any]]) -> str:
    stale = [code for code, status in status_map.items() if status.get("status") == "STALE"]
    missing = [code for code, status in status_map.items() if status.get("status") == "MISSING"]
    labels: List[str] = []
    if stale:
        labels.append(f"stale: {', '.join(sorted(stale))}")
    if missing:
        labels.append(f"missing: {', '.join(sorted(missing))}")
    return "; ".join(labels)


@app.command()
def read(
    item_id: str = typer.Argument(..., help="Display ID, e.g., KABSD-TSK-0001"),
    product: Optional[str] = typer.Option(None, help="Product name under _kano/backlog/products"),
    backlog_root_override: Optional[Path] = typer.Option(
        None,
        "--backlog-root-override",
        help="Backlog root override (e.g., _kano/backlog_sandbox/<name>)",
    ),
    output_format: str = typer.Option("plain", "--format", help="plain|json"),
):
    """Read a backlog item from the canonical store."""
    ensure_core_on_path()
    from kano_backlog_core.canonical import CanonicalStore

    product_root = resolve_product_root(product, backlog_root_override=backlog_root_override)
    store = CanonicalStore(product_root)
    item_path = find_item_path_by_id(store.items_root, item_id)
    item = store.read(item_path)

    if output_format == "json":
        data = item.model_dump()
        data["file_path"] = str(data.get("file_path"))
        typer.echo(json.dumps(data, ensure_ascii=True))
    else:
        typer.echo(f"ID: {item.id}\nTitle: {item.title}\nState: {item.state.value}\nOwner: {item.owner}")


@app.command(name="check-ready")
def check_ready(
    item_id: str = typer.Argument(..., help="Display ID, e.g., KABSD-TSK-0001"),
    check_parent: bool = typer.Option(
        True,
        "--check-parent/--no-check-parent",
        help="Check parent item recursively",
    ),
    product: Optional[str] = typer.Option(None, "--product", help="Product name"),
    backlog_root_override: Optional[Path] = typer.Option(
        None,
        "--backlog-root-override",
        help="Backlog root override (e.g., _kano/backlog_sandbox/<name>)",
    ),
    output_format: str = typer.Option("plain", "--format", help="plain|json"),
):
    """Validate a work item against the Ready gate."""
    ensure_core_on_path()
    from kano_backlog_core.canonical import CanonicalStore
    from kano_backlog_core.validation import is_ready

    product_root = resolve_product_root(product, backlog_root_override=backlog_root_override)
    store = CanonicalStore(product_root)
    item_path = find_item_path_by_id(store.items_root, item_id)
    item = store.read(item_path)

    ready, gaps = is_ready(item)
    
    parent_ready = True
    parent_gaps = []
    parent_id = None
    
    if check_parent and item.parent:
        try:
            parent_path = find_item_path_by_id(store.items_root, item.parent)
            parent_item = store.read(parent_path)
            parent_id = parent_item.id
            parent_ready, parent_gaps = is_ready(parent_item)
        except Exception:
            parent_ready = False
            parent_gaps = ["Parent not found or unreadable"]

    overall_ready = ready and (not item.parent or not check_parent or parent_ready)

    if output_format == "json":
        result = {
            "id": item.id,
            "is_ready": overall_ready,
            "self": {"ready": ready, "missing": gaps},
            "parent": {
                "id": parent_id,
                "ready": parent_ready,
                "missing": parent_gaps
            } if check_parent and item.parent else None
        }
        typer.echo(json.dumps(result, ensure_ascii=True))
    else:
        if overall_ready:
            typer.echo(f"OK: {item.id} is READY")
        else:
            typer.echo(f"❌ {item.id} is NOT READY")
            
            if not ready:
                typer.echo(f"  Missing fields in {item.id}:")
                for field in gaps:
                    typer.echo(f"    - {field}")
            
            if check_parent and item.parent and not parent_ready:
                typer.echo(f"  Parent {item.parent} is NOT READY:")
                for field in parent_gaps:
                    typer.echo(f"    - {field}")
            
        if not overall_ready:
            raise typer.Exit(1)


@app.command("add-decision")
def add_decision(
    item_ref: str = typer.Argument(..., help="Item ID/UID/path"),
    decision: str = typer.Option(..., "--decision", help="Decision text (English)"),
    source: Optional[str] = typer.Option(None, "--source", help="Source path or reference"),
    agent: str = typer.Option(..., "--agent", help="Agent identity"),
    product: Optional[str] = typer.Option(None, "--product", help="Product name"),
    backlog_root_override: Optional[Path] = typer.Option(
        None,
        "--backlog-root-override",
        help="Backlog root override (e.g., _kano/backlog_sandbox/<name>)",
    ),
    output_format: str = typer.Option("plain", "--format", help="plain|json"),
):
    """Append a decision write-back entry to a work item."""
    ensure_core_on_path()
    from kano_backlog_ops.workitem import add_decision_writeback

    try:
        result = add_decision_writeback(
            item_ref,
            decision,
            source=source,
            agent=agent,
            product=product,
            backlog_root=backlog_root_override,
        )
    except Exception as exc:
        typer.echo(f"❌ {exc}", err=True)
        raise typer.Exit(1)

    if output_format == "json":
        payload = {
            "item_id": result.item_id,
            "path": str(result.path),
            "added": result.added,
            "updated": result.updated,
        }
        typer.echo(json.dumps(payload, ensure_ascii=False, indent=2))
    else:
        typer.echo(f"✓ Decision write-back added to {result.item_id}")
        typer.echo(f"  Path: {result.path}")


def _run_create_command(
    *,
    item_type: str,
    title: str,
    parent: Optional[str],
    priority: str,
    area: str,
    iteration: Optional[str],
    tags: str,
    agent: str,
    product: Optional[str],
    backlog_root_override: Optional[Path],
    force: bool,
    skip_sequence_check: bool,
    auto_sync: bool,
    output_format: str,
) -> None:
    """Invoke the ops-layer create implementation and handle formatting."""
    ensure_core_on_path()
    from kano_backlog_core.models import ItemType
    from kano_backlog_ops.workitem import create_item as ops_create_item

    type_map = {
        "epic": ItemType.EPIC,
        "feature": ItemType.FEATURE,
        "userstory": ItemType.USER_STORY,
        "task": ItemType.TASK,
        "bug": ItemType.BUG,
    }

    type_key = item_type.strip().lower()
    if type_key not in type_map:
        typer.echo("❌ Invalid item type. Use: epic|feature|userstory|task|bug", err=True)
        raise typer.Exit(1)

    tag_list = _parse_tags(tags)

    try:
        product_root = resolve_product_root(product, backlog_root_override=backlog_root_override)

        effective_product = product
        if effective_product is None:
            from kano_backlog_core.config import ConfigLoader

            ctx = ConfigLoader.from_path(product_root, product=product)
            effective_product = ctx.product_name
        product_to_use = effective_product or product

        if not skip_sequence_check and backlog_root_override is None:
            from kano_backlog_ops import item_utils

            db_path, status_map = item_utils.check_sequence_health(
                effective_product,
                product_root,
            )
            summary = _summarize_sequence_status(status_map)
            needs_attention = any(
                status.get("status") != "OK" for status in status_map.values()
            )
            if needs_attention:
                typer.echo(
                    "Warning: DB sequences are stale or missing; consider syncing sequences."
                )
                if summary:
                    typer.echo(f"  Details: {summary}")
                typer.echo(
                    "  Suggested: python skills/kano-agent-backlog-skill/scripts/kano-backlog admin "
                    f"sync-sequences --product {effective_product}"
                )
                if auto_sync:
                    item_utils.sync_id_sequences(
                        product=effective_product,
                        backlog_root=None,
                        dry_run=False,
                    )
                    typer.echo("DB sequences synced automatically")
                    try:
                        from kano_backlog_core.audit import AuditLog

                        AuditLog.log_file_operation(
                            operation="update",
                            path=str(db_path),
                            tool="kano-backlog workitem create",
                            agent=agent,
                            metadata={
                                "action": "auto-sync-sequences",
                                "product": effective_product,
                                "status": summary or "stale-or-missing",
                            },
                        )
                    except Exception:
                        pass

        result = ops_create_item(
            item_type=type_map[type_key],
            title=title,
            product=product_to_use,
            agent=agent,
            parent=parent,
            priority=priority,
            area=area,
            iteration=iteration,
            tags=tag_list,
            backlog_root=product_root,
            force=force,
        )
    except FileNotFoundError as exc:
        typer.echo(f"❌ {exc}", err=True)
        raise typer.Exit(1)
    except ValueError as exc:
        typer.echo(f"❌ {exc}", err=True)
        raise typer.Exit(1)
    except Exception as exc:
        typer.echo(f"❌ Unexpected error: {exc}", err=True)
        raise typer.Exit(2)

    if output_format == "json":
        payload = {
            "id": result.id,
            "uid": result.uid,
            "path": str(result.path),
            "type": result.type.value,
        }
        typer.echo(json.dumps(payload, ensure_ascii=True))
    else:
        typer.echo(f"OK: Created: {result.id}")
        typer.echo(f"  Path: {result.path.name}")
        typer.echo(f"  Type: {result.type.value}")


@app.command()
def create(
    item_type: str = typer.Option(..., "--type", help="epic|feature|userstory|task|bug"),
    title: str = typer.Option(..., "--title", help="Work item title"),
    parent: Optional[str] = typer.Option(None, "--parent", help="Parent item ID (optional)"),
    priority: str = typer.Option("P2", "--priority", help="Priority (P0-P4, default: P2)"),
    area: str = typer.Option("general", "--area", help="Area tag"),
    iteration: Optional[str] = typer.Option(None, "--iteration", help="Iteration name"),
    tags: str = typer.Option("", "--tags", help="Comma-separated tags"),
    agent: str = typer.Option(..., "--agent", help="Agent name (for audit trail)"),
    product: Optional[str] = typer.Option(None, "--product", help="Product name"),
    backlog_root_override: Optional[Path] = typer.Option(
        None,
        "--backlog-root-override",
        help="Backlog root override (e.g., _kano/backlog_sandbox/<name>)",
    ),
    force: bool = typer.Option(False, "--force", help="Bypass parent Ready gate check"),
    skip_sequence_check: bool = typer.Option(
        False,
        "--skip-sequence-check",
        help="Skip DB sequence staleness check",
    ),
    auto_sync: bool = typer.Option(
        True,
        "--auto-sync/--no-auto-sync",
        help="Auto-sync sequences when stale",
    ),
    output_format: str = typer.Option("plain", "--format", help="plain|json"),
):
    """Create a new backlog work item (ops-backed implementation)."""
    _run_create_command(
        item_type=item_type,
        title=title,
        parent=parent,
        priority=priority,
        area=area,
        iteration=iteration,
        tags=tags,
        agent=agent,
        product=product,
        backlog_root_override=backlog_root_override,
        force=force,
        skip_sequence_check=skip_sequence_check,
        auto_sync=auto_sync,
        output_format=output_format,
    )


@app.command(name="set-ready")
def set_ready(
    item_id: str = typer.Argument(..., help="Display ID, e.g., KABSD-TSK-0001"),
    context: Optional[str] = typer.Option(None, "--context", help="# Context body"),
    goal: Optional[str] = typer.Option(None, "--goal", help="# Goal body"),
    approach: Optional[str] = typer.Option(None, "--approach", help="# Approach body"),
    acceptance_criteria: Optional[str] = typer.Option(
        None,
        "--acceptance-criteria",
        help="# Acceptance Criteria body",
    ),
    risks: Optional[str] = typer.Option(
        None,
        "--risks",
        help="# Risks / Dependencies body",
    ),
    agent: str = typer.Option(..., "--agent", help="Agent name (for audit trail)"),
    product: Optional[str] = typer.Option(None, "--product", help="Product name"),
    backlog_root_override: Optional[Path] = typer.Option(
        None,
        "--backlog-root-override",
        help="Backlog root override (e.g., _kano/backlog_sandbox/<name>)",
    ),
):
    """Set Ready-gate body sections for an existing item."""
    ensure_core_on_path()
    from kano_backlog_core.canonical import CanonicalStore
    from kano_backlog_ops.worklog import append_worklog_entry

    product_root = resolve_product_root(product, backlog_root_override=backlog_root_override)
    store = CanonicalStore(product_root)
    item_path = find_item_path_by_id(store.items_root, item_id)
    item = store.read(item_path)

    updated_fields = []
    fields_str = ""
    if context is not None:
        item.context = context
        updated_fields.append("Context")
    if goal is not None:
        item.goal = goal
        updated_fields.append("Goal")
    if approach is not None:
        item.approach = approach
        updated_fields.append("Approach")
    if acceptance_criteria is not None:
        item.acceptance_criteria = acceptance_criteria
        updated_fields.append("Acceptance Criteria")
    if risks is not None:
        item.risks = risks
        updated_fields.append("Risks")

    if updated_fields:
        from datetime import datetime
        fields_str = ", ".join(updated_fields)
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M")
        entry = f"{timestamp} [agent={agent}] Updated Ready fields: {fields_str}"
        item.worklog.append(entry)

    try:
        store.write(item)
    except Exception as exc:
        typer.echo(f"❌ Failed to write item: {exc}", err=True)
        raise typer.Exit(2)

    typer.echo(f"OK: Updated Ready fields for {item.id}")
    if updated_fields:
        typer.echo(f"  Worklog: Updated {fields_str}")


@app.command(name="update-state")
def update_state_command(
    item_ref: str = typer.Argument(..., help="Item ID, UID, or path"),
    state: str = typer.Option(
        ...,
        "--state",
        help="Target state (New|Proposed|Planned|Ready|InProgress|Review|Done|Blocked|Dropped)",
    ),
    agent: str = typer.Option(..., "--agent", help="Agent name (for audit trail)"),
    message: str = typer.Option("", "--message", help="Worklog message"),
    model: Optional[str] = typer.Option(None, "--model", help="Model used by agent (e.g., claude-sonnet-4.5, gpt-5.1)"),
    product: Optional[str] = typer.Option(None, "--product", help="Product name"),
    backlog_root_override: Optional[Path] = typer.Option(
        None,
        "--backlog-root-override",
        help="Backlog root override (e.g., _kano/backlog_sandbox/<name>)",
    ),
    sync_parent: bool = typer.Option(True, "--sync-parent/--no-sync-parent", help="Sync parent state forward"),
    refresh_dashboards: bool = typer.Option(True, "--refresh/--no-refresh", help="Refresh dashboards after update"),
    force: bool = typer.Option(False, "--force", help="Bypass Ready gate validation"),
    output_format: str = typer.Option("plain", "--format", help="plain|json"),
):
    """Update work item state via the ops layer."""
    ensure_core_on_path()
    from kano_backlog_core.models import ItemState
    from kano_backlog_ops.workitem import update_state as ops_update_state

    normalized = state.strip().lower().replace(" ", "").replace("_", "").replace("-", "")
    state_map = {
        "new": ItemState.NEW,
        "proposed": ItemState.PROPOSED,
        "planned": ItemState.PLANNED,
        "ready": ItemState.READY,
        "inprogress": ItemState.IN_PROGRESS,
        "review": ItemState.REVIEW,
        "done": ItemState.DONE,
        "blocked": ItemState.BLOCKED,
        "dropped": ItemState.DROPPED,
    }
    item_state = state_map.get(normalized)
    if item_state is None:
        typer.echo(
            "❌ Invalid state. Use: New, Proposed, Planned, Ready, InProgress, Review, Done, Blocked, Dropped",
            err=True,
        )
        raise typer.Exit(1)

    try:
        from ..util import resolve_model

        resolved_model, _ = resolve_model(model)
        product_root = resolve_product_root(product, backlog_root_override=backlog_root_override)
        result = ops_update_state(
            item_ref=item_ref,
            new_state=item_state,
            agent=agent,
            message=message or None,
            model=resolved_model,
            product=product,
            sync_parent=sync_parent,
            refresh_dashboards=refresh_dashboards,
            backlog_root=product_root,
            force=force,
        )
    except RuntimeError as exc:
        typer.echo(f"❌ {exc}", err=True)
        raise typer.Exit(1)
    except Exception as exc:
        typer.echo(f"❌ Unexpected error: {exc}", err=True)
        raise typer.Exit(2)

    if output_format == "json":
        payload = {
            "id": result.id,
            "old_state": result.old_state.value,
            "new_state": result.new_state.value,
            "worklog_appended": result.worklog_appended,
            "parent_synced": result.parent_synced,
            "dashboards_refreshed": result.dashboards_refreshed,
        }
        typer.echo(json.dumps(payload, ensure_ascii=True))
    else:
        typer.echo(f"OK: Updated {result.id}: {result.old_state.value} -> {result.new_state.value}")
        if result.worklog_appended and message:
            typer.echo(f"  Worklog: {message}")
        if result.parent_synced:
            typer.echo("  Parent state synced")
        if result.dashboards_refreshed:
            typer.echo("  Dashboards refreshed")


@app.command(name="attach-artifact")
def attach_artifact_command(
    item_id: str = typer.Argument(..., help="Display ID, e.g., KABSD-TSK-0001"),
    path: str = typer.Option(..., "--path", help="Path to artifact file"),
    shared: bool = typer.Option(True, "--shared/--no-shared", help="Store under _shared/artifacts when true"),
    agent: str = typer.Option(..., "--agent", help="Agent name (for audit trail)"),
    product: Optional[str] = typer.Option(None, "--product", help="Product name"),
    backlog_root_override: Optional[Path] = typer.Option(
        None,
        "--backlog-root-override",
        help="Backlog root override (e.g., _kano/backlog_sandbox/<name>)",
    ),
    note: Optional[str] = typer.Option(None, "--note", help="Optional note to include in Worklog"),
    output_format: str = typer.Option("plain", "--format", help="plain|json"),
):
    """Attach an artifact file to a work item and append a Worklog link."""
    ensure_core_on_path()
    from kano_backlog_ops.artifacts import attach_artifact as ops_attach

    try:
        backlog_root = resolve_backlog_root(backlog_root_override=backlog_root_override)
        result = ops_attach(
            item_ref=item_id,
            artifact_path=path,
            product=product,
            shared=shared,
            agent=agent,
            note=note,
            backlog_root=backlog_root,
        )
    except FileNotFoundError as exc:
        typer.echo(f"❌ {exc}", err=True)
        raise typer.Exit(1)
    except Exception as exc:
        typer.echo(f"❌ Unexpected error: {exc}", err=True)
        raise typer.Exit(2)

    if output_format == "json":
        payload = {
            "id": result.id,
            "source": str(result.source),
            "destination": str(result.destination),
            "worklog_appended": result.worklog_appended,
            "shared": shared,
        }
        typer.echo(json.dumps(payload, ensure_ascii=True))
    else:
        typer.echo(f"OK: Attached artifact to {result.id}")
        typer.echo(f"  Source: {result.source.name}")
        typer.echo(f"  Dest: {result.destination}")
        if note:
            typer.echo(f"  Note: {note}")
