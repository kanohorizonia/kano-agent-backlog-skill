from __future__ import annotations

import json
import re
import shutil
from datetime import datetime, timezone
from importlib import resources
from pathlib import Path
from typing import TYPE_CHECKING, Any, Optional, Union

import typer
import tomli_w

from ..util import ensure_core_on_path, get_global_config_file

app = typer.Typer(help="Configuration inspection and validation")

profiles_app = typer.Typer(help="Named config profiles (file-based overrides)")
app.add_typer(profiles_app, name="profiles")

if TYPE_CHECKING:
    from kano_backlog_core.config import BacklogContext


def _validate_required_fields(cfg: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    product = cfg.get("product")
    if not isinstance(product, dict):
        errors.append("[product] must be a table with name and prefix")
    else:
        name = product.get("name")
        prefix = product.get("prefix")
        if not isinstance(name, str) or not name.strip():
            errors.append("[product].name is required and must be a non-empty string")
        if not isinstance(prefix, str) or not prefix.strip():
            errors.append("[product].prefix is required and must be a non-empty string")

    process = cfg.get("process")
    if isinstance(process, dict):
        if process.get("profile") and process.get("path"):
            errors.append("[process] cannot set both profile and path")

    return errors


def _stringify_paths(value: Any) -> Any:
    """Recursively convert Path objects to str for serialization."""
    if isinstance(value, Path):
        return str(value)
    if isinstance(value, dict):
        return {k: _stringify_paths(v) for k, v in value.items()}
    if isinstance(value, list):
        return [_stringify_paths(v) for v in value]
    return value


def _default_export_path(ctx, fmt: str, topic: Optional[str], workset_item_id: Optional[str]) -> Path:
    # DEPRECATED: Manual config export now requires explicit --out path to avoid accumulating files.
    # This function is kept for backward compatibility but should not be used.
    raise NotImplementedError("config export now requires explicit --out path")


def _default_auto_export_path(ctx, fmt: str, topic: Optional[str], workset_item_id: Optional[str]) -> Path:
    cache_dir = ctx.project_root / ".kano" / "cache"
    cache_dir.mkdir(parents=True, exist_ok=True)
    return cache_dir / f"effective_backlog_config.{fmt}"


def _write_effective_config_artifact(
    *,
    ctx,
    effective: dict[str, Any],
    fmt: str,
    out_path: Path,
    overwrite: bool,
) -> Path:
    context_dict = _stringify_paths(ctx.model_dump())
    # project_root is already canonical in Context.


    payload = {
        "context": context_dict,
        "config": effective,
    }
    cleaned_payload = _strip_nulls(payload)
    serializable = _stringify_paths(cleaned_payload)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    if out_path.exists() and not overwrite:
        raise FileExistsError(f"Refusing to overwrite existing file: {out_path}")

    if fmt == "json":
        text = json.dumps(serializable, indent=2, default=str)
    else:
        text = tomli_w.dumps(serializable)

    out_path.write_text(text, encoding="utf-8")
    return out_path


_SECRET_SUFFIXES = ("_token", "_password", "_key")


def _walk_for_secrets(prefix: str, value: Any, errors: list[str]) -> None:
    if isinstance(value, dict):
        for k, v in value.items():
            if isinstance(k, str) and k.lower().endswith(_SECRET_SUFFIXES):
                if isinstance(v, str) and v.startswith("env:"):
                    continue
                errors.append(f"Secret-like field must use env: reference: {prefix}{k}")
            _walk_for_secrets(f"{prefix}{k}.", v, errors)
    elif isinstance(value, list):
        for idx, item in enumerate(value):
            _walk_for_secrets(f"{prefix}[{idx}].", item, errors)


def _validate_secrets(cfg: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    _walk_for_secrets("", cfg, errors)
    return errors


def _strip_nulls(value: Any) -> Any:
    if isinstance(value, dict):
        cleaned: dict[str, Any] = {}
        for k, v in value.items():
            cleaned_v = _strip_nulls(v)
            if cleaned_v is not None:
                cleaned[k] = cleaned_v
        return cleaned
    if isinstance(value, list):
        cleaned_list = []
        for item in value:
            cleaned_item = _strip_nulls(item)
            if cleaned_item is not None:
                cleaned_list.append(cleaned_item)
        return cleaned_list
    return value


def _read_config_template() -> str:
    """Read the packaged product config template with a source-tree fallback."""
    try:
        return resources.read_text(
            "kano_backlog_cli.templates",
            "config.template.toml",
            encoding="utf-8",
        )
    except (FileNotFoundError, ModuleNotFoundError):
        pass

    source_template_path = Path(__file__).resolve().parents[4] / "templates" / "config.template.toml"
    if source_template_path.exists():
        return source_template_path.read_text(encoding="utf-8")

    raise FileNotFoundError(
        "Template not found in package data or source tree: "
        f"kano_backlog_cli.templates/config.template.toml; {source_template_path}"
    )


def _next_backup_path(json_path: Path) -> Path:
    base = json_path.with_suffix(json_path.suffix + ".bak")
    candidate = base
    counter = 1
    while candidate.exists():
        candidate = base.with_suffix(base.suffix + f".{counter}")
        counter += 1
    return candidate


@profiles_app.command("list")
def profiles_list(
    path: Path = typer.Option(Path("."), "--path", help="Resource path to resolve config from"),
    product: Optional[str] = typer.Option(None, "--product", help="Product name (optional)"),
):
    """List available profile names."""
    ensure_core_on_path()
    from kano_backlog_core.config import ConfigLoader

    ctx = ConfigLoader.from_path(path, product=product)
    root = ConfigLoader.get_profiles_root(ctx.project_root)
    if not root.exists():
        typer.echo("No profiles directory found")
        typer.echo(f"Expected: {root}")
        raise typer.Exit(0)

    names: list[str] = []
    for p in sorted(root.rglob("*.toml")):
        if not p.is_file():
            continue
        rel = p.relative_to(root).with_suffix("")
        names.append(str(rel).replace("\\", "/"))
    if not names:
        typer.echo("No profiles found")
        typer.echo(f"Directory: {root}")
        raise typer.Exit(0)

    for name in names:
        typer.echo(name)


@profiles_app.command("show")
def profiles_show(
    name: str = typer.Argument(
        ...,
        help="Profile shorthand (e.g. embedding/local-noop) or path (e.g. .kano/backlog_config/embedding/local-noop.toml)",
    ),
    path: Path = typer.Option(Path("."), "--path", help="Resource path to resolve config from"),
    product: Optional[str] = typer.Option(None, "--product", help="Product name (optional)"),
):
    """Show the parsed profile config as JSON."""
    ensure_core_on_path()
    from kano_backlog_core.config import ConfigLoader

    ctx = ConfigLoader.from_path(path, product=product)
    overrides = ConfigLoader.load_profile_overrides(ctx.project_root, profile=name)
    typer.echo(json.dumps({"name": name, "overrides": overrides}, indent=2, default=str))



@app.command("pipeline")
def config_pipeline(
    path: Path = typer.Option(Path("."), "--path", help="Resource path to resolve config from"),
    product: Optional[str] = typer.Option(None, "--product", help="Product name (optional)"),
    sandbox: Optional[str] = typer.Option(None, "--sandbox", help="Sandbox name (optional)"),
    agent: Optional[str] = typer.Option(None, "--agent", help="Agent name for topic lookup"),
    topic: Optional[str] = typer.Option(None, "--topic", help="Explicit topic name"),
    profile: Optional[str] = typer.Option(
        None,
        "--profile",
        help="Profile (path or shorthand; shorthand prefers .kano/backlog_config)",
    ),
):
    """Inspect effective embedding pipeline configuration."""
    ensure_core_on_path()
    from kano_backlog_core.config import ConfigLoader
    
    try:
        ctx, effective = ConfigLoader.load_effective_config(
            path,
            product=product,
            sandbox=sandbox,
            agent=agent,
            topic=topic,
            profile=profile,
        )
        
        typer.echo(f"Context: Product={ctx.product_name} Topic={topic or 'None'}")
        
        try:
            pc = ConfigLoader.validate_pipeline_config(effective)
            typer.echo("✓ Pipeline config is valid")
            # typer.echo(pc) 
        except Exception as e:
            typer.echo(f"✗ Pipeline config invalid: {e}")
            typer.echo(json.dumps(effective, indent=2, default=str))

    except Exception as e:
        typer.echo(f"Error: {e}")
        raise typer.Exit(1)


@app.command("show")
def config_show(
    path: Path = typer.Option(Path("."), "--path", help="Resource path to resolve config from"),
    product: Optional[str] = typer.Option(None, "--product", help="Product name (optional)"),
    sandbox: Optional[str] = typer.Option(None, "--sandbox", help="Sandbox name (optional)"),
    agent: Optional[str] = typer.Option(None, "--agent", help="Agent name for topic lookup"),
    topic: Optional[str] = typer.Option(None, "--topic", help="Explicit topic name"),
    profile: Optional[str] = typer.Option(
        None,
        "--profile",
        help="Profile (path or shorthand; shorthand prefers .kano/backlog_config)",
    ),
    workset_item_id: Optional[str] = typer.Option(None, "--workset", help="Workset item id"),
):
    """Print effective merged config as JSON (includes compiled backend URIs)."""
    ensure_core_on_path()
    from kano_backlog_core.config import ConfigLoader

    # Get custom config file from global state if set
    custom_config_file = get_global_config_file()

    ctx, effective = ConfigLoader.load_effective_config(
        path,
        product=product,
        sandbox=sandbox,
        agent=agent,
        topic=topic,
        profile=profile,
        workset_item_id=workset_item_id,
        custom_config_file=custom_config_file,
    )

    typer.echo(
        json.dumps(
            {"context": ctx.model_dump(), "config": effective},
            indent=2,
            default=str,
        )
    )


@app.command("export")
def config_export(
    path: Path = typer.Option(Path("."), "--path", help="Resource path to resolve config from"),
    product: Optional[str] = typer.Option(None, "--product", help="Product name (optional)"),
    sandbox: Optional[str] = typer.Option(None, "--sandbox", help="Sandbox name (optional)"),
    agent: Optional[str] = typer.Option(None, "--agent", help="Agent name for topic lookup"),
    topic: Optional[str] = typer.Option(None, "--topic", help="Explicit topic name"),
    profile: Optional[str] = typer.Option(
        None,
        "--profile",
        help="Profile (path or shorthand; shorthand prefers .kano/backlog_config)",
    ),
    workset_item_id: Optional[str] = typer.Option(None, "--workset", help="Workset item id"),
    format: str = typer.Option("toml", "--format", case_sensitive=False, help="Output format: toml|json"),
    out: Optional[Path] = typer.Option(None, "--out", help="Output file path (REQUIRED: no default to avoid file accumulation)"),
    overwrite: bool = typer.Option(False, "--overwrite", help="Overwrite if output already exists"),
):
    """Write effective merged config (context + config) to disk."""
    ensure_core_on_path()
    from kano_backlog_core.config import ConfigLoader
    from kano_backlog_core.errors import ConfigError

    fmt = format.lower()
    if fmt not in {"toml", "json"}:
        typer.echo("format must be toml or json")
        raise typer.Exit(1)

    try:
        ctx, effective = ConfigLoader.load_effective_config(
            path,
            product=product,
            sandbox=sandbox,
            agent=agent,
            topic=topic,
            profile=profile,
            workset_item_id=workset_item_id,
        )
    except ConfigError as e:
        typer.echo(f"ConfigError: {e}")
        raise typer.Exit(1)

    if not out:
        typer.echo("Error: --out is required. Specify output file path to avoid accumulating timestamped files.")
        typer.echo("Hint: Use auto-export via 'view refresh' for a stable .kano/cache/effective_backlog_config.toml.")
        raise typer.Exit(1)

    out_path = out
    try:
        written = _write_effective_config_artifact(
            ctx=ctx,
            effective=effective,
            fmt=fmt,
            out_path=out_path,
            overwrite=overwrite,
        )
    except FileExistsError as e:
        typer.echo(str(e))
        raise typer.Exit(1)

    typer.echo(f"Wrote effective config to {written}")


@app.command("validate")
def config_validate(
    path: Path = typer.Option(Path("."), "--path", help="Resource path to resolve config from"),
    product: Optional[str] = typer.Option(None, "--product", help="Product name (optional)"),
    sandbox: Optional[str] = typer.Option(None, "--sandbox", help="Sandbox name (optional)"),
    agent: Optional[str] = typer.Option(None, "--agent", help="Agent name for topic lookup"),
    topic: Optional[str] = typer.Option(None, "--topic", help="Explicit topic name"),
    profile: Optional[str] = typer.Option(
        None,
        "--profile",
        help="Profile (path or shorthand; shorthand prefers .kano/backlog_config)",
    ),
    workset_item_id: Optional[str] = typer.Option(None, "--workset", help="Workset item id"),
):
    """Validate layered config; exit 0 if ok, 1 otherwise."""
    ensure_core_on_path()
    from kano_backlog_core.config import ConfigLoader
    from kano_backlog_core.errors import ConfigError

    try:
        _, effective = ConfigLoader.load_effective_config(
            path,
            product=product,
            sandbox=sandbox,
            agent=agent,
            topic=topic,
            profile=profile,
            workset_item_id=workset_item_id,
        )
    except ConfigError as e:
        typer.echo(f"ConfigError: {e}")
        raise typer.Exit(1)

    errors: list[str] = []
    errors.extend(_validate_required_fields(effective))
    errors.extend(_validate_secrets(effective))

    if errors:
        typer.echo("Validation failed:")
        for err in errors:
            typer.echo(f"- {err}")
        raise typer.Exit(1)

    typer.echo("Config is valid")


@app.command("init")
def config_init(
    path: Path = typer.Option(Path("."), "--path", help="Resource path to resolve config from"),
    product: Optional[str] = typer.Option(None, "--product", help="Product name (optional)"),
    sandbox: Optional[str] = typer.Option(None, "--sandbox", help="Sandbox name (optional)"),
    agent: Optional[str] = typer.Option(None, "--agent", help="Agent name for topic lookup"),
    topic: Optional[str] = typer.Option(None, "--topic", help="Explicit topic name"),
    profile: Optional[str] = typer.Option(
        None,
        "--profile",
        help="Profile (path or shorthand; shorthand prefers .kano/backlog_config)",
    ),
    workset_item_id: Optional[str] = typer.Option(None, "--workset", help="Workset item id"),
    prefix: Optional[str] = typer.Option(None, "--prefix", help="Override product prefix (default: derived)"),
    force: bool = typer.Option(False, "--force", help="Overwrite existing config.toml if present"),
):
    """Instantiate a product config from the annotated template."""
    ensure_core_on_path()
    from kano_backlog_core.config import ConfigLoader
    from kano_backlog_core.errors import ConfigError
    from kano_backlog_ops import item_utils

    try:
        ctx, _ = ConfigLoader.load_effective_config(
            path,
            product=product,
            sandbox=sandbox,
            agent=agent,
            topic=topic,
            profile=profile,
            workset_item_id=workset_item_id,
        )
    except ConfigError as e:
        typer.echo(f"ConfigError: {e}")
        raise typer.Exit(1)

    product_root = ctx.product_root
    config_path = product_root / "_config" / "config.toml"
    if config_path.exists() and not force:
        typer.echo(f"Config already exists: {config_path}. Use --force to overwrite.")
        raise typer.Exit(1)

    product_name = ctx.product_name
    derived_prefix = item_utils.derive_prefix(product_name)
    final_prefix = (prefix or derived_prefix).upper()

    try:
        template = _read_config_template()
    except FileNotFoundError as e:
        typer.echo(str(e))
        raise typer.Exit(1)

    rendered = template.replace("{{PRODUCT_NAME}}", product_name).replace("{{PRODUCT_PREFIX}}", final_prefix)

    config_path.parent.mkdir(parents=True, exist_ok=True)
    config_path.write_text(rendered, encoding="utf-8")
    typer.echo(f"Wrote product config from template: {config_path}")

    # Best-effort: write stable effective config artifact to _index/.
    try:
        ctx2, effective2 = ConfigLoader.load_effective_config(
            path,
            product=product,
            sandbox=sandbox,
            agent=agent,
            topic=topic,
            profile=profile,
            workset_item_id=workset_item_id,
        )
        out_path = _default_auto_export_path(ctx2, "toml", topic, workset_item_id)
        _write_effective_config_artifact(
            ctx=ctx2,
            effective=effective2,
            fmt="toml",
            out_path=out_path,
            overwrite=True,
        )
        typer.echo(f"Wrote effective config artifact: {out_path}")
    except Exception as e:
        typer.echo(f"Warning: could not write effective config artifact: {e}")


@app.command("migrate-json")
def config_migrate_json(
    path: Path = typer.Option(Path("."), "--path", help="Resource path to resolve config from"),
    product: Optional[str] = typer.Option(None, "--product", help="Product name (optional)"),
    sandbox: Optional[str] = typer.Option(None, "--sandbox", help="Sandbox name (optional)"),
    agent: Optional[str] = typer.Option(None, "--agent", help="Agent name for topic lookup"),
    topic: Optional[str] = typer.Option(None, "--topic", help="Explicit topic name"),
    workset_item_id: Optional[str] = typer.Option(None, "--workset", help="Workset item id"),
    write: bool = typer.Option(False, "--write", help="Apply migration (default: dry-run)"),
):
    """Convert JSON config files to TOML with backups (dry-run by default)."""
    ensure_core_on_path()
    from kano_backlog_core.config import ConfigLoader
    from kano_backlog_core.errors import ConfigError

    def _legacy_ctx_from_path() -> "BacklogContext":
        """Best-effort context resolver for legacy (pre-project-config) layouts.

        This command exists to help migrate legacy JSON configs. Requiring a
        project-level .kano/backlog_config.toml would make migration impossible
        for new adopters, so we allow a legacy fallback.
        """
        from kano_backlog_core.config import BacklogContext

        resource_path = path.resolve()
        base = resource_path if resource_path.is_dir() else resource_path.parent

        backlog_root: Optional[Path] = None
        for candidate in [base, *base.parents]:
            if candidate.name == "backlog" and candidate.parent.name == "_kano":
                backlog_root = candidate
                break
        if backlog_root is None:
            raise ConfigError(
                "Could not infer legacy backlog root from --path. "
                "Expected path under _kano/backlog/"
            )

        # Infer product.
        resolved_product = (product or "").strip() or None
        products_root = backlog_root / "products"
        product_root: Optional[Path] = None

        if resolved_product:
            product_root = products_root / resolved_product
        else:
            # If --path points inside backlog_root/products/<product>/..., infer.
            try:
                rel = base.relative_to(products_root)
                if rel.parts:
                    resolved_product = rel.parts[0]
                    product_root = products_root / resolved_product
            except ValueError:
                pass

        if product_root is None:
            if products_root.exists():
                product_dirs = [p for p in products_root.iterdir() if p.is_dir()]
                if len(product_dirs) == 1:
                    product_root = product_dirs[0]
                    resolved_product = product_root.name

        if product_root is None or resolved_product is None:
            raise ConfigError(
                "Could not infer product from --path; pass --product explicitly."
            )

        sandbox_root = None
        is_sandbox = False
        if sandbox:
            sandbox_root = backlog_root.parent / "backlog_sandbox" / sandbox
            is_sandbox = True

        project_root = backlog_root.parent.parent
        return BacklogContext(
            project_root=project_root,
            backlog_root=backlog_root,
            product_root=product_root,
            sandbox_root=sandbox_root,
            product_name=resolved_product,
            is_sandbox=is_sandbox,
        )

    try:
        ctx = ConfigLoader.from_path(
            path,
            product=product,
            sandbox=sandbox,
            agent=agent,
            topic=topic,
        )
    except ConfigError as e:
        try:
            ctx = _legacy_ctx_from_path()
        except ConfigError:
            typer.echo(f"ConfigError: {e}")
            raise typer.Exit(1)

    topic_name = (topic or "").strip() or (ConfigLoader.get_active_topic(ctx.backlog_root, agent or "") or "")

    targets: list[tuple[str, Path, Path]] = [
        ("defaults", ctx.backlog_root / "_shared" / "defaults.json", ctx.backlog_root / "_shared" / "defaults.toml"),
        ("product", ctx.product_root / "_config" / "config.json", ctx.product_root / "_config" / "config.toml"),
    ]
    if topic_name:
        targets.append(
            (
                f"topic:{topic_name}",
                ConfigLoader.get_topic_path(ctx.backlog_root, topic_name) / "config.json",
                ConfigLoader.get_topic_path(ctx.backlog_root, topic_name) / "config.toml",
            )
        )
    if workset_item_id:
        targets.append(
            (
                f"workset:{workset_item_id}",
                ConfigLoader.get_workset_path(ctx.backlog_root, workset_item_id) / "config.json",
                ConfigLoader.get_workset_path(ctx.backlog_root, workset_item_id) / "config.toml",
            )
        )

    plans: list[dict[str, Any]] = []
    had_error = False

    for label, json_path, toml_path in targets:
        if not json_path.exists():
            continue
        if toml_path.exists():
            plans.append({
                "label": label,
                "json": str(json_path),
                "toml": str(toml_path),
                "status": "skipped-toml-exists",
            })
            continue

        try:
            data = ConfigLoader._read_json_optional(json_path)
        except ConfigError as e:
            had_error = True
            plans.append({
                "label": label,
                "json": str(json_path),
                "toml": str(toml_path),
                "status": "error",
                "error": str(e),
            })
            continue

        cleaned = _strip_nulls(data)
        if isinstance(cleaned.get("project"), dict):
            project_cfg = cleaned.pop("project")
            if "product" not in cleaned:
                cleaned["product"] = project_cfg
        plan: dict[str, Any] = {
            "label": label,
            "json": str(json_path),
            "toml": str(toml_path),
            "status": "dry-run" if not write else "pending",
        }

        if write:
            backup_path = _next_backup_path(json_path)
            shutil.copy2(json_path, backup_path)
            toml_path.parent.mkdir(parents=True, exist_ok=True)
            toml_text = tomli_w.dumps(cleaned)
            toml_path.write_text(toml_text, encoding="utf-8")
            plan["status"] = "written"
            plan["backup"] = str(backup_path)

        plans.append(plan)

    if not plans:
        typer.echo("No JSON config files found to migrate.")
        return

    response: dict[str, Any] = {
        "applied": write,
        "plans": plans,
        "rollback": "Restore from the backup paths if needed.",
    }

    # Best-effort: if we applied a migration, write a stable effective config artifact.
    # IMPORTANT: keep output as valid JSON for scripting.
    if write and not had_error:
        try:
            ctx2, effective2 = ConfigLoader.load_effective_config(
                path,
                product=product,
                sandbox=sandbox,
                agent=agent,
                topic=topic,
                workset_item_id=workset_item_id,
            )
            out_path = _default_auto_export_path(ctx2, "toml", topic, workset_item_id)
            _write_effective_config_artifact(
                ctx=ctx2,
                effective=effective2,
                fmt="toml",
                out_path=out_path,
                overwrite=True,
            )
            response["effective_config_artifact"] = str(out_path)
        except Exception as e:
            response["effective_config_error"] = str(e)

    typer.echo(json.dumps(response, indent=2))

    if had_error:
        raise typer.Exit(1)
