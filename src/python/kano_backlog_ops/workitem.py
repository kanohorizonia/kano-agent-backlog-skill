"""
workitem.py - Work item CRUD operations (direct implementation, no subprocess).

Per ADR-0013: This module provides use-case functions for creating, reading, updating,
and listing backlog work items (Epic, Feature, UserStory, Task, Bug).
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Optional
from datetime import datetime
import sys
import uuid
import re

from kano_backlog_core.models import BacklogItem, ItemType, ItemState
try:
    from uuid import uuid7  # type: ignore[attr-defined]
except ImportError:
    from uuid6 import uuid7  # type: ignore
from kano_backlog_core.config import BacklogContext, ConfigLoader
from kano_backlog_core.validation import is_ready

from . import item_utils
from . import item_templates
from . import frontmatter
from . import worklog
from . import view


@dataclass
class CreateItemResult:
    """Result of creating a work item."""
    id: str
    uid: str
    path: Path
    type: ItemType


@dataclass
class UpdateStateResult:
    """Result of updating item state."""
    id: str
    old_state: ItemState
    new_state: ItemState
    worklog_appended: bool
    parent_synced: bool
    dashboards_refreshed: bool


@dataclass
class ValidationResult:
    """Result of Ready gate validation."""
    id: str
    is_valid: bool
    missing_sections: List[str]
    warnings: List[str]


@dataclass
class DecisionWritebackResult:
    """Result of writing back a decision to a work item."""

    item_id: str
    path: Path
    added: bool
    updated: bool


@dataclass
class RemapIdResult:
    """Result of remapping an item ID."""
    old_id: str
    new_id: str
    old_path: Path
    new_path: Path
    updated_files: int


@dataclass
class TrashItemResult:
    """Result of trashing an item."""
    item_ref: str
    source_path: Path
    trashed_path: Path
    status: str
    reason: Optional[str]


@dataclass
class ParentUpdateResult:
    """Result of updating parent link."""
    item_ref: str
    old_parent: Optional[str]
    new_parent: Optional[str]
    path: Path
    status: str


_UUID_RE = re.compile(
    r"^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"
)


def _looks_like_path(value: str) -> bool:
    if not value:
        return False
    if value.startswith((".", "/", "\\")):
        return True
    return ("/" in value) or ("\\" in value) or (":\\" in value)


def _resolve_target_root(*, product: Optional[str], backlog_root: Optional[Path]) -> Path:
    """
    Resolve the "target root" that contains an items/ directory.

    In product-scoped operation this is `_kano/backlog/products/<product>`.
    For legacy/single-product operation this may be `_kano/backlog`.
    """
    if backlog_root is not None:
        resolved = backlog_root.resolve()
        if not resolved.exists():
            raise FileNotFoundError(f"Backlog root not found: {resolved}")
        if product:
            candidate = resolved / "products" / product
            if candidate.exists():
                return candidate
        return resolved

    try:
        ctx = ConfigLoader.from_path(Path.cwd(), product=product)
        return ctx.product_root
    except Exception:
        current = Path.cwd().resolve()
        while current != current.parent:
            backlog_check = current / "_kano" / "backlog"
            if backlog_check.exists():
                if product:
                    candidate = backlog_check / "products" / product
                    if candidate.exists():
                        return candidate
                return backlog_check
            current = current.parent
        raise ValueError("Cannot find backlog root")


def _replace_id_tokens(text: str, old_id: str, new_id: str) -> str:
    pattern = re.compile(rf"(?<![A-Z0-9-]){re.escape(old_id)}(?![A-Z0-9-])")
    return pattern.sub(new_id, text)


def _infer_item_type_from_path(item_path: Path) -> ItemType:
    parts = [p.lower() for p in item_path.parts]
    if "items" in parts:
        idx = parts.index("items")
        if idx + 1 < len(parts):
            item_dir = parts[idx + 1]
            mapping = {
                "epic": ItemType.EPIC,
                "feature": ItemType.FEATURE,
                "userstory": ItemType.USER_STORY,
                "task": ItemType.TASK,
                "bug": ItemType.BUG,
            }
            if item_dir in mapping:
                return mapping[item_dir]
    return ItemType.TASK


def _iter_reference_files(product_root: Path) -> Iterable[Path]:
    roots = [
        product_root / "items",
        product_root / "decisions",
        product_root / "_meta",
    ]
    for root in roots:
        if not root.exists():
            continue
        for path in root.rglob("*.md"):
            if ".cache" in path.parts:
                continue
            yield path

    readme = product_root / "README.md"
    if readme.exists():
        yield readme


def _iter_item_files(items_root: Path) -> Iterable[Path]:
    for path in items_root.rglob("*.md"):
        if path.name.endswith(".index.md") or path.name == "README.md":
            continue
        yield path


def _find_item_paths_by_id(items_root: Path, item_id: str) -> List[Path]:
    matches = [
        path
        for path in items_root.rglob(f"{item_id}_*.md")
        if not path.name.endswith(".index.md")
    ]
    if matches:
        return sorted({m.resolve() for m in matches})

    try:
        import frontmatter as py_frontmatter
    except Exception:
        return []

    found: List[Path] = []
    for path in _iter_item_files(items_root):
        try:
            post = py_frontmatter.load(str(path))
        except Exception:
            continue
        if post.get("id") == item_id:
            found.append(path.resolve())
    return sorted({m for m in found})


def _find_item_paths_by_uid(items_root: Path, uid: str) -> List[Path]:
    try:
        import frontmatter as py_frontmatter
    except Exception:
        return []

    found: List[Path] = []
    for path in _iter_item_files(items_root):
        try:
            post = py_frontmatter.load(str(path))
        except Exception:
            continue
        if str(post.get("uid") or "").strip() == uid:
            found.append(path.resolve())
    return sorted({m for m in found})


def _resolve_item_path(
    item_ref: str,
    *,
    product: Optional[str],
    backlog_root: Optional[Path],
) -> tuple[Path, Path]:
    target_root = _resolve_target_root(product=product, backlog_root=backlog_root)
    items_root = target_root / "items"

    if _looks_like_path(item_ref):
        item_path = Path(item_ref).resolve()
        if not item_path.exists():
            raise FileNotFoundError(f"Item not found: {item_path}")
        return target_root, item_path

    candidates = _find_item_paths_by_id(items_root, item_ref)
    if not candidates and _UUID_RE.match(item_ref):
        candidates = _find_item_paths_by_uid(items_root, item_ref)

    if not candidates:
        raise FileNotFoundError(f"Item not found: {item_ref}")
    if len(candidates) > 1:
        raise ValueError(f"Ambiguous item reference '{item_ref}': {len(candidates)} matches")
    return target_root, candidates[0]


def _insert_decision_section(lines: List[str], decision_line: str) -> tuple[List[str], bool]:
    """Insert or append a decision line under a ## Decisions section."""
    # Find Worklog section to insert before it if needed
    worklog_idx = worklog.find_worklog_section(lines)
    insert_limit = worklog_idx if worklog_idx != -1 else len(lines)

    # Find existing Decisions section
    header_idx = -1
    for idx, line in enumerate(lines[:insert_limit]):
        if line.strip().lower() == "## decisions":
            header_idx = idx
            break

    if header_idx == -1:
        # Insert new Decisions section before Worklog (or end)
        block = ["", "## Decisions", "", f"- {decision_line}"]
        lines[insert_limit:insert_limit] = block
        return lines, True

    # Append under existing Decisions section
    next_heading = insert_limit
    for idx in range(header_idx + 1, insert_limit):
        if lines[idx].strip().startswith("#"):
            next_heading = idx
            break

    # Avoid duplicate entry
    for idx in range(header_idx + 1, next_heading):
        if lines[idx].strip() == f"- {decision_line}":
            return lines, False

    lines.insert(next_heading, f"- {decision_line}")
    return lines, True


def add_decision_writeback(
    item_ref: str,
    decision: str,
    *,
    source: Optional[str] = None,
    agent: str,
    model: Optional[str] = None,
    product: Optional[str] = None,
    backlog_root: Optional[Path] = None,
) -> DecisionWritebackResult:
    """Append a decision entry to a work item and log the write-back."""
    if not decision or not decision.strip():
        raise ValueError("Decision text cannot be empty")

    target_root, item_path = _resolve_item_path(item_ref, product=product, backlog_root=backlog_root)
    lines = frontmatter.load_lines(item_path)
    fm = frontmatter.parse_frontmatter(lines)
    item_id = fm.get("id", item_ref)

    decision_text = decision.strip()
    if source:
        decision_text = f"{decision_text} (source: {source})"

    lines, added = _insert_decision_section(lines, decision_text)

    # Update updated date
    today = datetime.now().strftime("%Y-%m-%d")
    try:
        lines = frontmatter.update_frontmatter_field(lines, "updated", today)
    except Exception:
        try:
            lines = frontmatter.add_frontmatter_field_before_closing(lines, "updated", today)
        except Exception:
            pass

    # Append worklog entry
    message = f"Decision write-back added: {decision_text}"
    lines = worklog.append_worklog_entry(lines, message, agent, model=model)
    frontmatter.write_lines(item_path, lines)

    return DecisionWritebackResult(
        item_id=item_id,
        path=item_path,
        added=added,
        updated=True,
    )


def _state_rank(state: ItemState) -> int:
    if state in (ItemState.NEW, ItemState.PROPOSED, ItemState.PLANNED, ItemState.READY):
        return 0
    if state in (ItemState.IN_PROGRESS, ItemState.REVIEW, ItemState.BLOCKED):
        return 1
    if state in (ItemState.DONE, ItemState.DROPPED):
        return 2
    return 0


def _update_item_file_state(
    item_path: Path,
    *,
    new_state: ItemState,
    agent: str,
    model: Optional[str],
    message: str,
    owner_to_set: Optional[str] = None,
) -> None:
    lines = frontmatter.load_lines(item_path)
    today = item_utils.get_today()
    lines = frontmatter.update_frontmatter(
        lines, new_state.value, today, owner=owner_to_set
    )
    lines = worklog.append_worklog_entry(lines, message, agent, model=model)
    frontmatter.write_lines(item_path, lines)


def update_parent(
    item_ref: str,
    *,
    parent: Optional[str],
    agent: str,
    model: Optional[str],
    product: Optional[str],
    backlog_root: Optional[Path],
    apply: bool = False,
) -> ParentUpdateResult:
    """Update parent frontmatter field and append worklog."""
    target_root, item_path = _resolve_item_path(
        item_ref, product=product, backlog_root=backlog_root
    )
    lines = frontmatter.load_lines(item_path)
    fm = frontmatter.parse_frontmatter(lines)
    old_parent = fm.get("parent")
    new_parent = parent or None
    new_value = "null" if new_parent in (None, "", "null") else new_parent
    status = "noop"

    if (old_parent or "null") != new_value:
        try:
            lines = frontmatter.update_frontmatter_field(lines, "parent", new_value)
        except ValueError:
            lines = frontmatter.add_frontmatter_field_before_closing(lines, "parent", new_value)
        today = item_utils.get_today()
        try:
            lines = frontmatter.update_frontmatter_field(lines, "updated", today)
        except ValueError:
            lines = frontmatter.add_frontmatter_field_before_closing(lines, "updated", today)
        message = f"Parent updated: {old_parent or 'null'} -> {new_value}."
        lines = worklog.append_worklog_entry(lines, message, agent, model=model)
        status = "would-update"
        if apply:
            frontmatter.write_lines(item_path, lines)
            status = "updated"

    return ParentUpdateResult(
        item_ref=item_ref,
        old_parent=(old_parent if old_parent != "null" else None),
        new_parent=(new_parent if new_value != "null" else None),
        path=item_path,
        status=status,
    )


def _check_uid_uniqueness(uid: str, *, product_root: Path) -> bool:
    """Check if a UID is unique across the product (SQLite index or file scan)."""
    index_path = product_root / ".cache" / "index.sqlite3"
    if index_path.exists():
        try:
            import sqlite3
            conn = sqlite3.connect(index_path)
            try:
                cur = conn.cursor()
                cur.execute("SELECT COUNT(*) FROM items WHERE uid = ?", (uid,))
                count = cur.fetchone()[0]
                return count == 0
            finally:
                conn.close()
        except Exception:
            pass
    
    items_root = product_root / "items"
    if not items_root.exists():
        return True
    
    try:
        import frontmatter as py_frontmatter
    except Exception:
        return True
    
    for path in items_root.rglob("*.md"):
        if path.name.endswith(".index.md") or path.name == "README.md":
            continue
        try:
            post = py_frontmatter.load(str(path))
            existing_uid = str(post.get("uid") or "").strip()
            if existing_uid == uid:
                return False
        except Exception:
            continue
    
    return True


def create_item(
    item_type: ItemType,
    title: str,
    *,
    product: Optional[str] = None,
    agent: str,
    parent: Optional[str] = None,
    priority: str = "P2",
    tags: Optional[List[str]] = None,
    area: Optional[str] = None,
    iteration: Optional[str] = None,
    backlog_root: Optional[Path] = None,
    worklog_message: str = "Created item",
    force: bool = False,
) -> CreateItemResult:
    """
    Create a new work item (direct implementation).

    Args:
        item_type: Type of item (epic, feature, userstory, task, bug)
        title: Item title
        product: Product name (default: "demo" for testing/local development)
        agent: Agent identity for audit logging
        parent: Parent item ID (optional)
        priority: Priority level (P0-P3, default P2)
        tags: List of tags
        area: Area/component
        iteration: Sprint/iteration
        backlog_root: Root path for backlog
        worklog_message: Initial worklog message
        force: Bypass parent Ready gate check if True

    Returns:
        CreateItemResult with created item details

    Raises:
        ValueError: If title is empty or type is invalid
        FileNotFoundError: If backlog not initialized
        OSError: If unable to write item file
    """
    if not title or not title.strip():
        raise ValueError("Title cannot be empty")
    
    # Setup
    title = title.strip()
    tags = tags or []
    area = area or "general"
    iteration = iteration or "backlog"
    product = product or "demo"  # Default for testing
    
    # Resolve context (backlog root + product root + prefix)
    if backlog_root is None:
        try:
            ctx = ConfigLoader.from_path(Path.cwd(), product=product)
            backlog_root = ctx.product_root  # Use product root, not project root
            
            # Load prefix from project config
            from kano_backlog_core.project_config import ProjectConfigLoader
            project_config = ProjectConfigLoader.load_project_config_optional(Path.cwd())
            if project_config:
                product_def = project_config.get_product(product)
                prefix = product_def.prefix if product_def else item_utils.derive_prefix(product)
            else:
                prefix = item_utils.derive_prefix(product)
        except Exception as e:
            raise ValueError(
                f"Cannot resolve backlog context for product '{product}'. "
                f"Ensure the product is defined in .kano/backlog_config.toml. "
                f"Error: {e}"
            )
    else:
        # Explicit backlog_root provided; try to load prefix from project config
        try:
            from kano_backlog_core.project_config import ProjectConfigLoader
            project_config = ProjectConfigLoader.load_project_config_optional(backlog_root)
            if project_config:
                product_def = project_config.get_product(product)
                prefix = product_def.prefix if product_def else item_utils.derive_prefix(product)
            else:
                prefix = item_utils.derive_prefix(product)
        except Exception:
            prefix = item_utils.derive_prefix(product)
            
    if parent and parent.lower() != "null" and not force:
        try:
            parent_item = get_item(parent, backlog_root=backlog_root)
            ready, gaps = is_ready(parent_item)
            if not ready:
                raise ValueError(
                    f"Parent item {parent_item.id} is not Ready. "
                    f"Missing fields: {', '.join(gaps)}. "
                    f"Parent must be Ready before adding children, or use --force to bypass."
                )
        except FileNotFoundError:
            raise ValueError(f"Parent item {parent} not found. Cannot verify Ready gate.")
            
    if parent and parent.lower() != "null":
        if force:
            worklog_message += " [Parent Ready gate bypassed via --force]"
        else:
            worklog_message += " [Parent Ready gate validated]"
    
    # Generate IDs
    type_code_map = {
        ItemType.EPIC: "EPIC",
        ItemType.FEATURE: "FTR",
        ItemType.USER_STORY: "USR",
        ItemType.TASK: "TSK",
        ItemType.BUG: "BUG",
    }
    type_code = type_code_map[item_type]
    
    next_id = 0
    try:
        ctx, effective = ConfigLoader.load_effective_config(backlog_root, product=product)
        cache_dir = ConfigLoader.get_chunks_cache_root(ctx.backlog_root, effective)
    except Exception:
        cache_dir = ConfigLoader.get_chunks_cache_root(backlog_root)
    db_path = cache_dir / f"backlog.{product}.chunks.v1.db"
    
    try:
        next_id = item_utils.get_next_id_from_db(db_path, prefix, type_code)
    except Exception as e:
        # Fallback to filesystem scan if DB not ready or locked
        # This warning is expected during bootstrap/migration
        print(f"Warning: DB ID generation failed ({e}). Falling back to filesystem scan.")
        items_root = backlog_root / "items"
        next_id = item_utils.find_next_number(items_root, prefix, type_code)
        
    item_id = f"{prefix}-{type_code}-{next_id:04d}"
    uid = str(uuid7())
    
    if not _check_uid_uniqueness(uid, product_root=backlog_root):
        raise ValueError(
            f"UID collision detected: {uid} already exists in product '{product}'. "
            f"This is extremely rare with UUIDv7. Please retry the operation."
        )
    
    # Calculate storage path
    bucket = item_utils.calculate_bucket(next_id)
    
    # Determine subdirectory based on item type
    type_subdir_map = {
        ItemType.EPIC: "epic",
        ItemType.FEATURE: "feature",
        ItemType.USER_STORY: "userstory",
        ItemType.TASK: "task",
        ItemType.BUG: "bug",
    }
    subdir = type_subdir_map[item_type]
    
    item_dir = backlog_root / "items" / subdir / bucket
    
    # Construct item filename: <ID>_<slugified_title>.md
    slug = item_utils.slugify(title)
    item_path = item_dir / f"{item_id}_{slug}.md"
    
    # Create directory if needed
    item_dir.mkdir(parents=True, exist_ok=True)
    
    # Create parent reference
    parent_ref = parent or "null"
    
    # Create timestamps
    today = item_utils.get_today()
    
    # Generate content
    content = item_templates.render_item_body(
        item_id=item_id,
        uid=uid,
        item_type=item_type.value,
        title=title,
        priority=priority,
        parent=parent_ref,
        area=area,
        iteration=iteration,
        tags=tags,
        created=today,
        updated=today,
        owner=None,
        agent=agent,
        worklog_message=worklog_message,
    )
    
    # Write item file
    try:
        item_path.write_text(content, encoding="utf-8")
    except OSError as e:
        raise OSError(f"Failed to write item file {item_path}: {e}") from e
    
    # If Epic, create index MOC
    if item_type == ItemType.EPIC:
        index_path = item_path.parent / f"{item_id}_{item_utils.slugify(title)}.index.md"
        index_content = item_templates.render_epic_index(
            item_id=item_id,
            title=title,
            updated=today,
            backlog_root_label="../../..",  # Relative path from index to backlog root
        )
        try:
            index_path.write_text(index_content, encoding="utf-8")
        except OSError as e:
            raise OSError(f"Failed to write index file {index_path}: {e}") from e
        
        # Update _meta/indexes.md registry
        _update_index_registry(backlog_root, item_id, title, "add")
    
    return CreateItemResult(
        id=item_id,
        uid=uid,
        path=item_path,
        type=item_type,
    )


def remap_item_id(
    item_ref: str,
    *,
    agent: str,
    model: Optional[str] = None,
    product: Optional[str] = None,
    backlog_root: Optional[Path] = None,
    new_id: Optional[str] = None,
    update_refs: bool = True,
    apply: bool = True,
) -> RemapIdResult:
    """Remap an item ID and optionally update references across the product."""
    target_root, item_path = _resolve_item_path(
        item_ref, product=product, backlog_root=backlog_root
    )

    from kano_backlog_core.canonical import CanonicalStore

    store = CanonicalStore(target_root)
    item = None
    try:
        item = store.read(item_path)
    except Exception:
        item = None

    file_id = item_path.stem.split("_", 1)[0]
    if item:
        old_id = item.id if item.id and item.id == file_id else file_id
        item_type = item.type
        title = item.title or item_path.stem.split("_", 1)[-1]
    else:
        old_id = file_id
        item_type = _infer_item_type_from_path(item_path)
        title = item_path.stem.split("_", 1)[-1]

    type_code_map = {
        ItemType.EPIC: "EPIC",
        ItemType.FEATURE: "FTR",
        ItemType.USER_STORY: "USR",
        ItemType.TASK: "TSK",
        ItemType.BUG: "BUG",
    }
    type_code = type_code_map[item_type]

    prefix = old_id.split("-", 1)[0] if old_id else "KABSD"
    if new_id is None:
        items_root = target_root / "items"
        next_id = item_utils.find_next_number(items_root, prefix, type_code)
        new_id = f"{prefix}-{type_code}-{next_id:04d}"

    new_number = int(new_id.split("-")[-1])
    bucket = item_utils.calculate_bucket(new_number)
    type_subdir_map = {
        ItemType.EPIC: "epic",
        ItemType.FEATURE: "feature",
        ItemType.USER_STORY: "userstory",
        ItemType.TASK: "task",
        ItemType.BUG: "bug",
    }
    subdir = type_subdir_map[item_type]
    new_dir = target_root / "items" / subdir / bucket
    slug = item_utils.slugify(title)
    new_path = new_dir / f"{new_id}_{slug}.md"

    updated_files = 0
    if apply:
        content = item_path.read_text(encoding="utf-8")
        content = _replace_id_tokens(content, old_id, new_id)
        lines = content.splitlines()
        lines = worklog.append_worklog_entry(
            lines,
            f"Remapped ID: {old_id} -> {new_id}.",
            agent,
            model=model,
        )
        new_dir.mkdir(parents=True, exist_ok=True)
        updated_text = "\n".join(lines) + "\n"
        item_path.write_text(updated_text, encoding="utf-8")
        if new_path.resolve() != item_path.resolve():
            try:
                item_path.replace(new_path)
            except PermissionError:
                new_path.write_text(updated_text, encoding="utf-8")
                try:
                    item_path.unlink()
                except PermissionError:
                    try:
                        item_path.chmod(0o666)
                        item_path.unlink()
                    except PermissionError:
                        pass
        updated_files += 1

    if update_refs and apply:
        for path in _iter_reference_files(target_root):
            if path.resolve() == new_path.resolve():
                continue
            text = path.read_text(encoding="utf-8")
            updated = _replace_id_tokens(text, old_id, new_id)
            if updated != text:
                path.write_text(updated, encoding="utf-8")
                updated_files += 1

    return RemapIdResult(
        old_id=old_id,
        new_id=new_id,
        old_path=item_path,
        new_path=new_path,
        updated_files=updated_files,
    )


def update_state(
    item_ref: str,
    new_state: ItemState,
    *,
    agent: str,
    message: Optional[str] = None,
    model: Optional[str] = None,
    product: Optional[str] = None,
    sync_parent: bool = True,
    refresh_dashboards: bool = True,
    backlog_root: Optional[Path] = None,
    force: bool = False,
) -> UpdateStateResult:
    """
    Update work item state (direct implementation).

    Args:
        item_ref: Item reference (ID, UID, or path)
        new_state: Target state
        agent: Agent identity for audit logging
        message: Worklog message (optional)
        product: Product name (for disambiguation)
        sync_parent: Whether to sync parent state forward
        refresh_dashboards: Whether to refresh dashboards after update
        backlog_root: Root path for backlog
        force: Bypass Ready gate validation if True

    Returns:
        UpdateStateResult with transition details

    Raises:
        FileNotFoundError: If item not found
        ValueError: If state transition is invalid or Ready gate check fails
    """
    target_root, item_path = _resolve_item_path(
        item_ref, product=product, backlog_root=backlog_root
    )

    item = get_item(str(item_path), backlog_root=target_root)
    old_state = item.state

    # Ready Gate Validation
    # Only check when moving to InProgress (starting work)
    if new_state == ItemState.IN_PROGRESS and not force:
        # 1. Check item itself
        ready, gaps = is_ready(item)
        if not ready:
            raise ValueError(
                f"Item {item.id} is not Ready. "
                f"Missing fields: {', '.join(gaps)}. "
                f"Fill required fields or use --force to bypass."
            )
        
        # 2. Check parent (if exists)
        if item.parent:
            try:
                parent_item = get_item(item.parent, backlog_root=target_root)
                parent_ready, parent_gaps = is_ready(parent_item)
                if not parent_ready:
                    raise ValueError(
                        f"Parent item {parent_item.id} is not Ready. "
                        f"Missing fields: {', '.join(parent_gaps)}. "
                        f"Parent must be Ready before child can start, or use --force to bypass."
                    )
            except FileNotFoundError:
                # If parent ID exists but file not found, warn but don't block? 
                # Or block because context is broken? Let's block to be safe.
                raise ValueError(f"Parent item {item.parent} not found. Cannot verify Ready gate.")

    owner_to_set = None
    if new_state == ItemState.IN_PROGRESS:
        current_owner = (item.owner or "").strip()
        if not current_owner or current_owner.lower() in ("null", "none"):
            owner_to_set = agent
        elif current_owner == agent:
            owner_to_set = agent

    validation_note = ""
    if new_state == ItemState.IN_PROGRESS:
        if force:
            validation_note = " [Ready gate bypassed via --force]"
        else:
            validation_note = " [Ready gate validated]"

    worklog_message = (message or f"State -> {new_state.value}.") + validation_note
    _update_item_file_state(
        item_path,
        new_state=new_state,
        agent=agent,
        model=model,
        message=worklog_message,
        owner_to_set=owner_to_set,
    )

    parent_synced = False
    if sync_parent and item.parent:
        try:
            parent_item = get_item(item.parent, backlog_root=target_root)
        except FileNotFoundError:
            parent_item = None

        if parent_item and parent_item.file_path:
            parent_next_state: Optional[ItemState] = None

            if new_state in (ItemState.IN_PROGRESS, ItemState.REVIEW, ItemState.BLOCKED):
                if _state_rank(parent_item.state) < _state_rank(ItemState.IN_PROGRESS):
                    parent_next_state = ItemState.IN_PROGRESS
            elif new_state in (ItemState.DONE, ItemState.DROPPED):
                siblings = list_items(parent=item.parent, backlog_root=target_root)
                if siblings and all(
                    s.state in (ItemState.DONE, ItemState.DROPPED) for s in siblings
                ):
                    if parent_item.state not in (ItemState.DONE, ItemState.DROPPED):
                        parent_next_state = ItemState.DONE

            if parent_next_state and parent_next_state != parent_item.state:
                parent_message = (
                    f"Auto parent sync: child {item.id} -> {new_state.value}; "
                    f"parent -> {parent_next_state.value}."
                )
                _update_item_file_state(
                    parent_item.file_path,
                    new_state=parent_next_state,
                    agent=agent,
                    model=model,
                    message=parent_message,
                )
                parent_synced = True

    dashboards_refreshed = False
    if refresh_dashboards:
        view.refresh_dashboards(agent=agent, backlog_root=target_root, product=None)
        dashboards_refreshed = True

    return UpdateStateResult(
        id=item.id,
        old_state=old_state,
        new_state=new_state,
        worklog_appended=True,
        parent_synced=parent_synced,
        dashboards_refreshed=dashboards_refreshed,
    )


def validate_ready(
    item_ref: str,
    *,
    product: Optional[str] = None,
    backlog_root: Optional[Path] = None,
) -> ValidationResult:
    """
    Validate item meets Ready gate criteria (direct implementation).

    For Task/Bug, checks that these sections are non-empty:
    - Context
    - Goal
    - Approach
    - Acceptance Criteria
    - Risks / Dependencies

    Args:
        item_ref: Item reference (ID, UID, or path)
        product: Product name (for disambiguation)
        backlog_root: Root path for backlog

    Returns:
        ValidationResult with validation details

    Raises:
        FileNotFoundError: If item not found
    """
    # Resolve item path
    if item_ref.startswith("/") or ":\\" in item_ref:
        # It's a path
        item_path = Path(item_ref).resolve()
    else:
        # It's an ID - search for it
        if backlog_root is None:
            # Prefer product-aware context resolution
            if product:
                try:
                    ctx = ConfigLoader.from_path(Path.cwd(), product=product)
                    backlog_root = ctx.product_root
                except Exception as e:
                    raise ValueError(f"Cannot resolve product root for '{product}': {e}")
            else:
                current = Path.cwd()
                while current != current.parent:
                    backlog_check = current / "_kano" / "backlog"
                    if backlog_check.exists():
                        backlog_root = backlog_check
                        break
                    current = current.parent
                if backlog_root is None:
                    raise ValueError("Cannot find backlog root")
        
        # Search for item by ID
        items_root = backlog_root / "items"
        item_path = None
        for path in items_root.rglob("*.md"):
            if path.name.endswith(".index.md"):
                continue
            stem = path.stem
            file_id = stem.split("_", 1)[0] if "_" in stem else stem
            if file_id == item_ref:
                item_path = path
                break
        
        if item_path is None:
            raise FileNotFoundError(f"Item not found: {item_ref}")
    
    if not item_path.exists():
        raise FileNotFoundError(f"Item not found: {item_path}")
    
    # Load and parse
    lines = frontmatter.load_lines(item_path)
    fm = frontmatter.parse_frontmatter(lines)
    item_type = fm.get("type", "").strip()
    item_id = fm.get("id", item_ref)
    
    # Ready gate sections (required for Task/Bug)
    required_sections = {"Context", "Goal", "Approach", "Acceptance Criteria", "Risks / Dependencies"}
    
    # Extract sections from content
    sections_found = set()
    for line in lines:
        if line.startswith("# "):
            section_name = line[2:].strip()
            sections_found.add(section_name)
    
    # Check for required sections (apply to Task/Bug)
    missing_sections = []
    if item_type in ("Task", "Bug"):
        for section in required_sections:
            if section not in sections_found:
                missing_sections.append(section)
    
    is_valid = len(missing_sections) == 0
    
    return ValidationResult(
        id=item_id,
        is_valid=is_valid,
        missing_sections=missing_sections,
        warnings=[],
    )


def list_items(
    *,
    product: Optional[str] = None,
    item_type: Optional[ItemType] = None,
    state: Optional[ItemState] = None,
    parent: Optional[str] = None,
    tags: Optional[List[str]] = None,
    backlog_root: Optional[Path] = None,
) -> List[BacklogItem]:
    """
    List work items with optional filters.

    Args:
        product: Filter by product
        item_type: Filter by type
        state: Filter by state
        parent: Filter by parent ID
        tags: Filter by tags (AND)
        backlog_root: Root path for backlog

    Returns:
        List of matching BacklogItem objects
    """
    target_root = _resolve_target_root(product=product, backlog_root=backlog_root)
    items_root = target_root / "items"
    if not items_root.exists():
        raise FileNotFoundError(f"Items directory not found: {items_root}")

    from kano_backlog_core.canonical import CanonicalStore

    store = CanonicalStore(target_root)
    tags = tags or []

    candidates = store.list_items(item_type=item_type)
    results: List[BacklogItem] = []

    for path in candidates:
        if path.name.endswith(".index.md") or path.name == "README.md":
            continue
        try:
            item = store.read(path)
        except Exception:
            continue

        if item_type and item.type != item_type:
            continue
        if state and item.state != state:
            continue
        if parent is not None:
            if (item.parent or "") != parent:
                continue
        if tags:
            item_tags = set(item.tags or [])
            if not all(t in item_tags for t in tags):
                continue

        results.append(item)

    results.sort(key=lambda it: it.id)
    return results


def get_item(
    item_ref: str,
    *,
    product: Optional[str] = None,
    backlog_root: Optional[Path] = None,
) -> BacklogItem:
    """
    Get a single work item by reference.

    Args:
        item_ref: Item reference (ID, UID, or path)
        product: Product name (for disambiguation)
        backlog_root: Root path for backlog

    Returns:
        BacklogItem object

    Raises:
        FileNotFoundError: If item not found
        ValueError: If reference is ambiguous
    """
    target_root, item_path = _resolve_item_path(
        item_ref, product=product, backlog_root=backlog_root
    )

    from kano_backlog_core.canonical import CanonicalStore

    store = CanonicalStore(target_root)
    item = store.read(item_path)
    if item.file_path is None:
        item.file_path = item_path
    return item


def trash_item(
    item_ref: str,
    *,
    agent: str,
    reason: Optional[str] = None,
    model: Optional[str] = None,
    product: Optional[str] = None,
    backlog_root: Optional[Path] = None,
    apply: bool = False,
) -> TrashItemResult:
    """Move a backlog item file into a per-product _trash folder."""
    target_root, item_path = _resolve_item_path(
        item_ref, product=product, backlog_root=backlog_root
    )

    try:
        rel_path = item_path.resolve().relative_to(target_root.resolve())
    except ValueError:
        raise ValueError(f"Item path is outside product root: {item_path}") from None

    stamp = datetime.now().strftime("%Y%m%d")
    trash_root = target_root / "_trash" / stamp
    trashed_path = trash_root / rel_path
    status = "would-trash"

    if apply:
        text = item_path.read_text(encoding="utf-8")
        if "items" in item_path.parts:
            lines = text.splitlines()
            message = f"Trashed item: {reason or 'duplicate or obsolete'}."
            lines = worklog.append_worklog_entry(lines, message, agent, model=model)
            text = "\n".join(lines) + "\n"
        trashed_path.parent.mkdir(parents=True, exist_ok=True)
        if trashed_path.resolve() != item_path.resolve():
            try:
                item_path.replace(trashed_path)
            except PermissionError:
                trashed_path.write_text(text, encoding="utf-8")
                try:
                    item_path.unlink()
                except PermissionError:
                    pass
        else:
            trashed_path.write_text(text, encoding="utf-8")
        status = "trashed"

    return TrashItemResult(
        item_ref=item_ref,
        source_path=item_path,
        trashed_path=trashed_path,
        status=status,
        reason=reason,
    )


def _update_index_registry(
    backlog_root: Path,
    item_id: str,
    title: str,
    action: str,
) -> None:
    """
    Update _meta/indexes.md registry when epic index is created/deleted.
    
    Args:
        backlog_root: Root of backlog
        item_id: Epic ID
        title: Epic title
        action: "add" or "remove"
    """
    registry_path = backlog_root / "_meta" / "indexes.md"
    
    if not registry_path.exists():
        # Create basic registry if missing
        content = "# Index Registry\n\n## Epic\n"
        registry_path.parent.mkdir(parents=True, exist_ok=True)
        registry_path.write_text(content, encoding="utf-8")
    
    content = registry_path.read_text(encoding="utf-8")
    
    if action == "add":
        # Add entry if not already present
        entry = f"- [{title}]({item_id}_{item_utils.slugify(title)}.index.md) (ID: {item_id})"
        if entry not in content:
            lines = content.rstrip().split("\n")
            lines.append(entry)
            content = "\n".join(lines) + "\n"
    elif action == "remove":
        # Remove entry
        lines = [
            line for line in content.split("\n")
            if item_id not in line
        ]
        content = "\n".join(lines) + "\n"
    
    registry_path.write_text(content, encoding="utf-8")
