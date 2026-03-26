import warnings

from pathlib import Path
from typing import Dict, Optional, Tuple

try:
    from hypothesis import settings

    # Silence expected deprecation warnings emitted when intentionally using legacy JSON configs in tests.
    warnings.filterwarnings(
        "ignore",
        message=r"JSON config is deprecated; migrate to TOML",
        category=DeprecationWarning,
    )

    # Prevent Hypothesis from writing a local example database (e.g. `.hypothesis/`) during tests.
    settings.register_profile("kano-tests", database=None)
    settings.load_profile("kano-tests")
except ImportError:
    # hypothesis is optional; tests can still run without it
    pass


def write_project_backlog_config(
    project_root: Path,
    *,
    products: Optional[Dict[str, Tuple[str, str]]] = None,
) -> Path:
    """Write a minimal project-level .kano/backlog_config.toml for tests.

    The current config system requires a project config and explicit product
    registration. Many tests build an isolated backlog under tmp_path, so
    this helper makes those tests compatible with the project-level config.

    Args:
        project_root: Temporary workspace root (tmp_path).
        products: Mapping of product -> (name, prefix).

    Returns:
        Path to the written config file.
    """
    if products is None:
        products = {"test-product": ("test-product", "TEST")}

    kano_dir = project_root / ".kano"
    kano_dir.mkdir(parents=True, exist_ok=True)
    path = kano_dir / "backlog_config.toml"

    lines = [
        "[defaults]",
        "auto_refresh = false",
        "",
        "[shared.cache]",
        'root = ".kano/cache/backlog"',
        "",
    ]

    for product, (name, prefix) in products.items():
        lines.extend(
            [
                f"[products.{product}]",
                f'name = "{name}"',
                f'prefix = "{prefix}"',
                f'backlog_root = "_kano/backlog/products/{product}"',
                "vector_enabled = true",
                "",
            ]
        )

    path.write_text("\n".join(lines).strip() + "\n", encoding="utf-8")
    return path
