from __future__ import annotations

import os
import sys
from pathlib import Path
from typing import Optional, Protocol, cast


class _ReconfigurableStream(Protocol):
    def reconfigure(self, *, errors: str) -> None: ...

# Global variable to store custom config file path
_global_config_file: Optional[Path] = None


def set_global_config_file(config_file: Path) -> None:
    """Set the global config file path for use by utility functions."""
    global _global_config_file
    _global_config_file = config_file.resolve()


def get_global_config_file() -> Optional[Path]:
    """Get the global config file path if set."""
    return _global_config_file


def configure_stdio() -> None:
    """Make CLI output robust across Windows console encodings.

    Some Windows terminals use a non-UTF8 encoding (e.g., cp1252). If we print
    Unicode glyphs (✓/❌/⚠️), Python can raise UnicodeEncodeError and abort the
    command. To prevent first-run failures for agents, configure stdout/stderr
    to replace unencodable characters instead of crashing.
    """

    if os.name != "nt":
        return

    for stream in (sys.stdout, sys.stderr):
        try:
            # Keep the current encoding, but make encoding errors non-fatal.
            cast(_ReconfigurableStream, stream).reconfigure(errors="replace")
        except Exception:
            continue


def load_env_file(path: Path, *, required: bool = False) -> None:
    """Load environment variables from a simple KEY=VALUE file.

    Args:
        path: Path to env file.
        required: If True, raise when missing.
    """
    env_path = Path(path).expanduser().resolve()
    if not env_path.exists():
        if required:
            raise SystemExit(f"Env file not found: {env_path}")
        return

    try:
        lines = env_path.read_text(encoding="utf-8").splitlines()
    except Exception as e:
        raise SystemExit(f"Failed to read env file: {env_path}. Error: {e}")

    for line in lines:
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if stripped.startswith("export "):
            stripped = stripped[7:].strip()
        if "=" not in stripped:
            continue

        key, value = stripped.split("=", 1)
        key = key.strip()
        value = value.strip()
        if not key:
            continue

        if len(value) >= 2 and ((value[0] == value[-1]) and value[0] in {"'", '"'}):
            value = value[1:-1]

        if key not in os.environ:
            os.environ[key] = value


def ensure_core_on_path() -> None:
    try:
        import kano_backlog_core  # noqa: F401
        return
    except Exception:
        pass
    # Try local path fallback: kano_backlog_core is in the same src/ directory as kano_backlog_cli
    skill_src = Path(__file__).resolve().parent.parent
    if (skill_src / "kano_backlog_core").exists():
        sys.path.insert(0, str(skill_src))
        try:
            import kano_backlog_core  # noqa: F401
            return
        except Exception:
            pass
    # Also try upward search for backward compatibility
    search_roots = [Path.cwd().resolve(), Path(__file__).resolve().parent.parent.parent.parent.parent]
    for root in search_roots:
        candidate = root / "kano-backlog-core" / "src"
        if candidate.exists() and (candidate / "kano_backlog_core").exists():
            sys.path.insert(0, str(candidate))
            try:
                import kano_backlog_core  # noqa: F401
                return
            except Exception:
                pass
    raise SystemExit("kano_backlog_core not found; install the package or check that kano_backlog_core is in src/ directory.")


def resolve_model(model: Optional[str]) -> tuple[str, bool]:
    """
    Resolve the model name deterministically.

    Order:
    1) explicit CLI flag
    2) env vars KANO_AGENT_MODEL, KANO_MODEL
    3) "unknown"

    Returns:
        (model_value, is_default_unknown)
    """
    if model and model.strip():
        return model.strip(), False
    env_model = (os.environ.get("KANO_AGENT_MODEL") or os.environ.get("KANO_MODEL") or "").strip()
    if env_model:
        return env_model, False
    return "unknown", True


def find_project_root(start: Optional[Path] = None) -> Path:
    """Find repo project root containing _kano/backlog."""
    cur = (start or Path.cwd()).resolve()
    while True:
        if (cur / "_kano" / "backlog").exists():
            return cur
        if cur.parent == cur:
            raise SystemExit("Could not locate _kano/backlog from current path.")
        cur = cur.parent


def resolve_backlog_root(
    start: Optional[Path] = None,
    backlog_root_override: Optional[Path] = None,
) -> Path:
    """Resolve backlog root, honoring an explicit override when provided."""
    if backlog_root_override:
        candidate = Path(backlog_root_override).expanduser().resolve()
        if (candidate / "_kano" / "backlog").exists():
            backlog_root = candidate / "_kano" / "backlog"
        else:
            backlog_root = candidate
        if not backlog_root.exists():
            raise SystemExit(f"Backlog root override not found: {backlog_root}")
        if not ((backlog_root / "products").exists() or (backlog_root / "items").exists()):
            raise SystemExit(f"Backlog root override is invalid: {backlog_root}")
        return backlog_root

    project_root = find_project_root(start)
    return project_root / "_kano" / "backlog"


def resolve_product_root(
    product: Optional[str] = None,
    start: Optional[Path] = None,
    backlog_root_override: Optional[Path] = None,
) -> Path:
    """Resolve product root using the project-level config system.
    
    BREAKING CHANGE: Traditional product structure no longer supported.
    Only project-level configs (.kano/backlog_config.toml) are used.
    """
    try:
        if backlog_root_override:
            override_root = resolve_backlog_root(
                start=start,
                backlog_root_override=backlog_root_override,
            )

            if (override_root / "items").exists():
                return override_root

            if product:
                override_product_root = override_root / "products" / product
                if override_product_root.exists() and (override_product_root / "items").exists():
                    return override_product_root

                raise SystemExit(
                    f"Product '{product}' not found under backlog root override: {override_root}"
                )

            product_roots = [
                path
                for path in (override_root / "products").iterdir()
                if path.is_dir() and (path / "items").exists()
            ] if (override_root / "products").exists() else []

            if len(product_roots) == 1:
                return product_roots[0]
            if len(product_roots) > 1:
                names = ", ".join(sorted(path.name for path in product_roots))
                raise SystemExit(
                    f"Multiple products found under backlog root override; specify --product: {names}"
                )

            raise SystemExit(
                f"No product roots found under backlog root override: {override_root}"
            )

        ensure_core_on_path()
        from kano_backlog_core.config import ConfigLoader
        from kano_backlog_core.project_config import ProjectConfigLoader
        
        start_path = start or Path.cwd()
        
        # Check if a custom config file was specified via CLI
        custom_config_file = get_global_config_file()
        
        if custom_config_file:
            # Load project config from custom file
            project_config = ProjectConfigLoader.load_project_config(custom_config_file)
            
            # If product is specified, try to resolve from project config
            if product:
                backlog_root = project_config.resolve_backlog_root(product, custom_config_file)
                if backlog_root:
                    # For project config products, the backlog_root IS the product root
                    return backlog_root
                else:
                    raise SystemExit(f"Product '{product}' not found in config file: {custom_config_file}")
            else:
                # Auto-detect product from project config
                products = project_config.list_products()
                if len(products) == 1:
                    backlog_root = project_config.resolve_backlog_root(products[0], custom_config_file)
                    if backlog_root:
                        return backlog_root
                elif len(products) > 1:
                    raise SystemExit(f"Multiple products found in {custom_config_file}; specify --product: {', '.join(products)}")
                else:
                    raise SystemExit(f"No products defined in config file: {custom_config_file}")
        
        # Use the new config system (project config required)
        ctx = ConfigLoader.from_path(
            start_path,
            product=product,
            custom_config_file=custom_config_file,
        )
        
        return ctx.product_root
        
    except Exception as e:
        # No fallback - project config is required
        raise SystemExit(f"Project config required but not found. Create .kano/backlog_config.toml in project root. Error: {e}")


def find_item_path_by_id(items_root: Path, display_id: str) -> Path:
    # Quick filename match first (exclude .index.md files)
    for path in items_root.rglob(f"{display_id}_*.md"):
        if not path.name.endswith(".index.md"):
            return path
    # Fallback: scan all and check frontmatter ids
    try:
        import frontmatter
    except Exception as e:
        raise SystemExit(f"frontmatter is required for scanning items: {e}")
    for path in items_root.rglob("*.md"):
        if path.name.endswith(".index.md"):
            continue  # Skip index files
        try:
            post = frontmatter.load(str(path))
            if post.get("id") == display_id:
                return path
        except Exception:
            continue
    raise SystemExit(f"Item not found: {display_id}")
