"""
doctor.py - Environment health check command.

Checks prerequisites and backlog initialization status.
"""

from __future__ import annotations

import json
import sys
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Optional, List, Dict, Any

import typer
from rich.console import Console
from rich.table import Table

# Conditional TOML import: stdlib tomllib (3.11+) or fallback tomli (<3.11)
try:
    import tomllib  # Python 3.11+
except ImportError:
    try:
        import tomli as tomllib  # type: ignore
    except ImportError:
        tomllib = None  # type: ignore

app = typer.Typer()
console = Console()


@dataclass
class CheckResult:
    """Result of a single check."""
    name: str
    passed: bool
    message: str
    details: Optional[str] = None


@dataclass
class DoctorResult:
    """Overall doctor check result."""
    all_passed: bool
    checks: List[CheckResult]


def check_python_version() -> CheckResult:
    """Check that Python version meets minimum requirements."""
    min_version = (3, 8)
    current_version = sys.version_info[:2]
    
    version_str = f"{current_version[0]}.{current_version[1]}"
    min_version_str = f"{min_version[0]}.{min_version[1]}"
    
    if current_version < min_version:
        return CheckResult(
            name="Python Version",
            passed=False,
            message=f"Python {version_str} is below minimum required version",
            details=f"Current: Python {version_str}, Required: Python {min_version_str}+",
        )
    
    return CheckResult(
        name="Python Version",
        passed=True,
        message=f"Python {version_str} meets requirements (>= {min_version_str})",
    )


def check_python_prereqs() -> CheckResult:
    """Check that required Python packages are installed."""
    missing = []
    packages = [
        ("pydantic", "pydantic"),
        ("frontmatter", "python-frontmatter"),
        ("typer", "typer"),
        ("rich", "rich"),
    ]
    
    for import_name, pip_name in packages:
        try:
            __import__(import_name)
        except ImportError:
            missing.append(pip_name)
    
    if missing:
        return CheckResult(
            name="Python Prerequisites",
            passed=False,
            message=f"Missing packages: {', '.join(missing)}",
            details=f"Install with: pip install {' '.join(missing)}",
        )
    
    return CheckResult(
        name="Python Prerequisites",
        passed=True,
        message="All required packages installed",
    )


def check_sqlite_availability() -> CheckResult:
    """Check that SQLite is available and meets version requirements."""
    try:
        import sqlite3
    except ImportError:
        return CheckResult(
            name="SQLite Availability",
            passed=False,
            message="SQLite module not available",
            details=(
                "SQLite3 is required for ID sequence management. "
                "It should be included with Python by default. "
                "If missing, reinstall Python with SQLite support."
            ),
        )
    
    # Check SQLite version
    try:
        sqlite_version = sqlite3.sqlite_version
        # Parse version string (e.g., "3.35.5")
        version_parts = sqlite_version.split(".")
        major = int(version_parts[0])
        minor = int(version_parts[1]) if len(version_parts) > 1 else 0
        
        # Minimum required version: 3.8.0 (for basic features)
        min_major = 3
        min_minor = 8
        
        if major < min_major or (major == min_major and minor < min_minor):
            return CheckResult(
                name="SQLite Availability",
                passed=False,
                message=f"SQLite {sqlite_version} is below minimum required version",
                details=f"Current: SQLite {sqlite_version}, Required: SQLite {min_major}.{min_minor}.0+",
            )
        
        return CheckResult(
            name="SQLite Availability",
            passed=True,
            message=f"SQLite {sqlite_version} available (>= {min_major}.{min_minor}.0)",
        )
    except Exception as e:
        return CheckResult(
            name="SQLite Availability",
            passed=False,
            message="Failed to check SQLite version",
            details=str(e),
        )


def check_backlog_structure(
    backlog_root: Optional[Path] = None,
) -> CheckResult:
    """Check that backlog has the required directory structure."""
    # Find backlog root
    if backlog_root is None:
        # Try to find _kano/backlog in current directory or parents
        cwd = Path.cwd()
        for parent in [cwd] + list(cwd.parents):
            candidate = parent / "_kano" / "backlog"
            if candidate.exists():
                backlog_root = candidate
                break
    
    if backlog_root is None or not backlog_root.exists():
        return CheckResult(
            name="Backlog Structure",
            passed=False,
            message="Backlog root not found",
            details=(
                "Run 'kano-backlog admin init --product <name> --agent <id>' to initialize a backlog.\n"
                "This will create the required directory structure at _kano/backlog/"
            ),
        )
    
    # Check for required top-level directories
    required_dirs = {
        "products": backlog_root / "products",
    }
    
    missing_dirs = []
    for dir_name, dir_path in required_dirs.items():
        if not dir_path.exists():
            missing_dirs.append(dir_name)
    
    if missing_dirs:
        return CheckResult(
            name="Backlog Structure",
            passed=False,
            message=f"Missing required directories: {', '.join(missing_dirs)}",
            details=(
                f"Backlog root: {backlog_root}\n"
                f"Run 'kano-backlog admin init --product <name> --agent <id>' to create the required structure."
            ),
        )
    
    # Check for at least one product with proper structure
    products_root = backlog_root / "products"
    products = [p for p in products_root.iterdir() if p.is_dir() and not p.name.startswith("_")]
    
    if not products:
        return CheckResult(
            name="Backlog Structure",
            passed=False,
            message="No products found in backlog",
            details=(
                f"Products directory exists at {products_root} but is empty.\n"
                f"Run 'kano-backlog admin init --product <name> --agent <id>' to create a product."
            ),
        )
    
    # Check structure of first product as a sample
    product = products[0]
    product_required_dirs = {
        "items": product / "items",
        "decisions": product / "decisions",
        "_meta": product / "_meta",
    }
    
    product_missing_dirs = []
    for dir_name, dir_path in product_required_dirs.items():
        if not dir_path.exists():
            product_missing_dirs.append(dir_name)
    
    if product_missing_dirs:
        return CheckResult(
            name="Backlog Structure",
            passed=False,
            message=f"Product '{product.name}' missing directories: {', '.join(product_missing_dirs)}",
            details=(
                f"Product path: {product}\n"
                f"Run 'kano-backlog admin init --product {product.name} --agent <id> --force' to fix the structure."
            ),
        )
    
    return CheckResult(
        name="Backlog Structure",
        passed=True,
        message=f"Backlog structure valid with {len(products)} product(s): {', '.join(p.name for p in products)}",
    )


def check_backlog_initialized(
    product: Optional[str] = None,
    backlog_root: Optional[Path] = None,
) -> CheckResult:
    """Check that backlog is initialized for the product."""
    # Find project root
    if backlog_root is None:
        # Try to find _kano/backlog in current directory or parents
        cwd = Path.cwd()
        for parent in [cwd] + list(cwd.parents):
            candidate = parent / "_kano" / "backlog"
            if candidate.exists():
                backlog_root = candidate
                break
    
    if backlog_root is None or not backlog_root.exists():
        return CheckResult(
            name="Backlog Initialized",
            passed=False,
            message="Backlog root not found",
            details=(
                "Initialize the backlog with 'bash scripts/kob admin "
                "init --product <name> --agent <id>' or follow SKILL.md for manual scaffolding."
            ),
        )
    
    # Check for products directory
    products_root = backlog_root / "products"
    if not products_root.exists():
        return CheckResult(
            name="Backlog Initialized",
            passed=False,
            message="No products directory found",
            details=f"Expected: {products_root}",
        )
    
    # If product specified, check that specific product
    if product:
        product_root = products_root / product
        config_path = product_root / "_config" / "config.json"
        if not config_path.exists():
            return CheckResult(
                name="Backlog Initialized",
                passed=False,
                message=f"Product '{product}' not initialized",
                details=f"Missing: {config_path}",
            )
        return CheckResult(
            name="Backlog Initialized",
            passed=True,
            message=f"Product '{product}' initialized at {product_root}",
        )
    
    # List available products
    products = [p.name for p in products_root.iterdir() if p.is_dir() and not p.name.startswith("_")]
    if not products:
        return CheckResult(
            name="Backlog Initialized",
            passed=False,
            message="No products found",
            details=(
                "Create one with 'bash scripts/kob admin init --product <name> "
                "--agent <id>' or follow SKILL.md for manual scaffolding."
            ),
        )
    
    return CheckResult(
        name="Backlog Initialized",
        passed=True,
        message=f"Found {len(products)} product(s): {', '.join(products)}",
    )


def check_skill_layout() -> CheckResult:
    """Detect common repo-layout regressions (developer workflow guardrail)."""
    cwd = Path.cwd().resolve()
    skill_root: Optional[Path] = None
    for parent in [cwd, *cwd.parents]:
        candidate = parent / "skills" / "kano-agent-backlog-skill"
        if candidate.exists() and candidate.is_dir():
            skill_root = candidate
            break

    if skill_root is None:
        return CheckResult(
            name="Skill Layout",
            passed=True,
            message="Skill root not found from cwd (skipping layout checks)",
        )

    legacy_cli_root = skill_root / "src" / "kano_cli"
    legacy_py_files = list(legacy_cli_root.rglob("*.py")) if legacy_cli_root.exists() else []
    if legacy_py_files:
        sample = legacy_py_files[0].as_posix()
        return CheckResult(
            name="Skill Layout",
            passed=False,
            message="Legacy CLI package reintroduced under src/kano_cli",
            details=(
                "Move CLI code under src/kano_backlog_cli instead. "
                f"Example offending file: {sample}"
            ),
        )

    return CheckResult(
        name="Skill Layout",
        passed=True,
        message="OK (no legacy src/kano_cli python files)",
    )


def check_configuration_validity(
    backlog_root: Optional[Path] = None,
) -> CheckResult:
    """Check that configuration files are valid and contain required fields."""
    # Find backlog root
    if backlog_root is None:
        # Try to find _kano/backlog in current directory or parents
        cwd = Path.cwd()
        for parent in [cwd] + list(cwd.parents):
            candidate = parent / "_kano" / "backlog"
            if candidate.exists():
                backlog_root = candidate
                break
    
    if backlog_root is None or not backlog_root.exists():
        return CheckResult(
            name="Configuration Validity",
            passed=True,
            message="Backlog root not found (skipping config validation)",
        )
    
    errors = []
    warnings = []
    
    # Check 1: Shared defaults config (_kano/backlog/_shared/defaults.toml or defaults.json)
    shared_dir = backlog_root / "_shared"
    if shared_dir.exists():
        defaults_toml = shared_dir / "defaults.toml"
        defaults_json = shared_dir / "defaults.json"
        
        if defaults_toml.exists():
            try:
                # Try to load TOML
                if tomllib is None:
                    warnings.append(f"Cannot validate {defaults_toml}: tomllib/tomli not available")
                else:
                    with open(defaults_toml, "rb") as f:
                        data = tomllib.load(f)
                    if not isinstance(data, dict):
                        errors.append(f"{defaults_toml}: Config must be a TOML table (got {type(data).__name__})")
            except Exception as e:
                error_msg = str(e)
                # Try to extract line number from error message
                if hasattr(e, 'lineno'):
                    error_msg = f"line {e.lineno}: {error_msg}"
                errors.append(f"{defaults_toml}: Invalid TOML syntax - {error_msg}")
        
        if defaults_json.exists():
            try:
                # Try to load JSON
                with open(defaults_json, "r", encoding="utf-8") as f:
                    data = json.load(f)
                if not isinstance(data, dict):
                    errors.append(f"{defaults_json}: Config must be a JSON object (got {type(data).__name__})")
                warnings.append(f"{defaults_json}: JSON config is deprecated, migrate to TOML")
            except json.JSONDecodeError as e:
                errors.append(f"{defaults_json}: Invalid JSON syntax - line {e.lineno}, column {e.colno}: {e.msg}")
            except Exception as e:
                errors.append(f"{defaults_json}: Failed to load - {e}")
    
    # Check 2: Project config (.kano/backlog_config.toml)
    project_root = backlog_root.parent.parent
    project_config_toml = project_root / ".kano" / "backlog_config.toml"
    
    if project_config_toml.exists():
        try:
            if tomllib is None:
                warnings.append(f"Cannot validate {project_config_toml}: tomllib/tomli not available")
            else:
                with open(project_config_toml, "rb") as f:
                    data = tomllib.load(f)
                if not isinstance(data, dict):
                    errors.append(f"{project_config_toml}: Config must be a TOML table (got {type(data).__name__})")
                else:
                    # Check for required fields in project config
                    if "products" not in data or not isinstance(data.get("products"), dict):
                        errors.append(f"{project_config_toml}: Missing required 'products' table")
                    else:
                        products = data["products"]
                        if not products:
                            warnings.append(f"{project_config_toml}: 'products' table is empty")
                        else:
                            # Validate each product has required fields
                            for product_name, product_config in products.items():
                                if not isinstance(product_config, dict):
                                    errors.append(
                                        f"{project_config_toml}: Product '{product_name}' must be a table"
                                    )
                                    continue
                                
                                # Check required fields: name, prefix, backlog_root
                                if "name" not in product_config:
                                    errors.append(
                                        f"{project_config_toml}: Product '{product_name}' missing required field 'name'"
                                    )
                                if "prefix" not in product_config:
                                    errors.append(
                                        f"{project_config_toml}: Product '{product_name}' missing required field 'prefix'"
                                    )
                                if "backlog_root" not in product_config:
                                    errors.append(
                                        f"{project_config_toml}: Product '{product_name}' missing required field 'backlog_root'"
                                    )
        except Exception as e:
            error_msg = str(e)
            # Try to extract line number from error message
            if hasattr(e, 'lineno'):
                error_msg = f"line {e.lineno}: {error_msg}"
            errors.append(f"{project_config_toml}: Invalid TOML syntax - {error_msg}")
    else:
        # Project config is required in the new architecture
        errors.append(
            f"{project_config_toml}: Project config file not found. "
            "Create .kano/backlog_config.toml in project root."
        )
    
    # Check 3: Product-specific configs (deprecated but check if they exist)
    products_dir = backlog_root / "products"
    if products_dir.exists():
        for product_dir in products_dir.iterdir():
            if not product_dir.is_dir() or product_dir.name.startswith("_"):
                continue
            
            config_dir = product_dir / "_config"
            if config_dir.exists():
                config_json = config_dir / "config.json"
                if config_json.exists():
                    try:
                        with open(config_json, "r", encoding="utf-8") as f:
                            data = json.load(f)
                        if not isinstance(data, dict):
                            errors.append(
                                f"{config_json}: Config must be a JSON object (got {type(data).__name__})"
                            )
                        else:
                            # Check for required fields in product config
                            if "project" not in data or not isinstance(data.get("project"), dict):
                                errors.append(f"{config_json}: Missing required 'project' object")
                            else:
                                project = data["project"]
                                if "name" not in project:
                                    errors.append(f"{config_json}: Missing required field 'project.name'")
                                if "prefix" not in project:
                                    errors.append(f"{config_json}: Missing required field 'project.prefix'")
                        
                        warnings.append(
                            f"{config_json}: Product-specific config is deprecated. "
                            "Migrate to project-level .kano/backlog_config.toml"
                        )
                    except json.JSONDecodeError as e:
                        errors.append(
                            f"{config_json}: Invalid JSON syntax - line {e.lineno}, column {e.colno}: {e.msg}"
                        )
                    except Exception as e:
                        errors.append(f"{config_json}: Failed to load - {e}")
    
    # Determine result
    if errors:
        details_lines = []
        if errors:
            details_lines.append("Errors:")
            for error in errors:
                details_lines.append(f"  ✗ {error}")
        if warnings:
            details_lines.append("\nWarnings:")
            for warning in warnings:
                details_lines.append(f"  ⚠ {warning}")
        
        return CheckResult(
            name="Configuration Validity",
            passed=False,
            message=f"Found {len(errors)} configuration error(s)",
            details="\n".join(details_lines),
        )
    
    if warnings:
        details_lines = ["Warnings:"]
        for warning in warnings:
            details_lines.append(f"  ⚠ {warning}")
        
        return CheckResult(
            name="Configuration Validity",
            passed=True,
            message=f"Configuration valid with {len(warnings)} warning(s)",
            details="\n".join(details_lines),
        )
    
    return CheckResult(
        name="Configuration Validity",
        passed=True,
        message="All configuration files are valid",
    )


def check_kano_backlog_cli() -> CheckResult:
    """Check that kano-backlog CLI is available."""
    try:
        from kano_backlog_cli import cli
        return CheckResult(
            name="Kano CLI",
            passed=True,
            message="CLI module available",
        )
    except ImportError as e:
        return CheckResult(
            name="Kano CLI",
            passed=False,
            message="CLI module not found",
            details=str(e),
        )


def check_optional_dependencies() -> CheckResult:
    """Check which optional dependency groups are installed."""
    # Define optional dependency groups and their key packages
    optional_groups = {
        "dev": [
            ("pytest", "pytest"),
            ("black", "black"),
            ("mypy", "mypy"),
            ("isort", "isort"),
        ],
        "vector": [
            ("sentence_transformers", "sentence-transformers"),
            ("faiss", "faiss-cpu"),
        ],
    }
    
    installed_groups = []
    partially_installed = []
    not_installed = []
    
    for group_name, packages in optional_groups.items():
        installed_count = 0
        missing_packages = []
        
        for import_name, pip_name in packages:
            try:
                __import__(import_name)
                installed_count += 1
            except ImportError:
                missing_packages.append(pip_name)
        
        if installed_count == len(packages):
            installed_groups.append(group_name)
        elif installed_count > 0:
            partially_installed.append((group_name, missing_packages))
        else:
            not_installed.append(group_name)
    
    # Build message and details
    if installed_groups and not partially_installed and not not_installed:
        message = f"All optional groups installed: {', '.join(installed_groups)}"
        details = None
    elif not installed_groups and not partially_installed:
        message = "No optional dependency groups installed"
        details = (
            "Optional groups available:\n"
            f"  - [dev]: Development tools (pytest, black, mypy, isort)\n"
            f"  - [vector]: Vector search dependencies (sentence-transformers, faiss)\n"
            f"\nInstall with: pip install kano-agent-backlog-skill[dev] or [vector]"
        )
    else:
        parts = []
        if installed_groups:
            parts.append(f"Installed: {', '.join(installed_groups)}")
        if not_installed:
            parts.append(f"Not installed: {', '.join(not_installed)}")
        if partially_installed:
            parts.append(f"Partially installed: {', '.join(g for g, _ in partially_installed)}")
        
        message = "; ".join(parts)
        
        details_lines = []
        if partially_installed:
            details_lines.append("Partially installed groups:")
            for group_name, missing in partially_installed:
                details_lines.append(f"  [{group_name}] missing: {', '.join(missing)}")
        
        if not_installed:
            details_lines.append("\nNot installed groups:")
            for group_name in not_installed:
                details_lines.append(f"  [{group_name}]")
        
        if details_lines:
            details_lines.append("\nInstall with: pip install kano-agent-backlog-skill[dev] or [vector]")
            details = "\n".join(details_lines)
        else:
            details = None
    
    return CheckResult(
        name="Optional Dependencies",
        passed=True,  # This is informational, not a failure
        message=message,
        details=details,
    )


def check_permissions(
    backlog_root: Optional[Path] = None,
) -> CheckResult:
    """Check write permissions on backlog root and key directories."""
    # Find backlog root
    if backlog_root is None:
        # Try to find _kano/backlog in current directory or parents
        cwd = Path.cwd()
        for parent in [cwd] + list(cwd.parents):
            candidate = parent / "_kano" / "backlog"
            if candidate.exists():
                backlog_root = candidate
                break
    
    if backlog_root is None or not backlog_root.exists():
        return CheckResult(
            name="Permissions",
            passed=True,
            message="Backlog root not found (skipping permissions check)",
        )
    
    # Directories to check
    dirs_to_check = [
        ("Backlog root", backlog_root),
    ]
    
    # Check products directory
    products_dir = backlog_root / "products"
    if products_dir.exists():
        dirs_to_check.append(("Products directory", products_dir))
        
        # Check each product's key directories
        for product_dir in products_dir.iterdir():
            if not product_dir.is_dir() or product_dir.name.startswith("_"):
                continue
            
            dirs_to_check.append((f"Product '{product_dir.name}'", product_dir))
            
            # Check product subdirectories
            for subdir_name in ["items", "decisions", "_meta"]:
                subdir = product_dir / subdir_name
                if subdir.exists():
                    dirs_to_check.append((f"Product '{product_dir.name}' {subdir_name}", subdir))
    
    # Check permissions
    permission_issues = []
    for dir_name, dir_path in dirs_to_check:
        # Check if directory is writable
        # Try to create a temporary file to test write permissions
        try:
            test_file = dir_path / ".permission_test_tmp"
            test_file.touch()
            test_file.unlink()
        except (PermissionError, OSError) as e:
            permission_issues.append((dir_name, dir_path, str(e)))
    
    if permission_issues:
        details_lines = ["Directories lacking write permissions:"]
        for dir_name, dir_path, error in permission_issues:
            details_lines.append(f"  ✗ {dir_name}: {dir_path}")
            details_lines.append(f"    Error: {error}")
        
        details_lines.append("\nRecommendations:")
        details_lines.append("  - Check file system permissions")
        details_lines.append("  - Ensure you have write access to the backlog directory")
        details_lines.append("  - On Unix systems, use: chmod -R u+w <directory>")
        details_lines.append("  - On Windows, check folder properties and security settings")
        
        return CheckResult(
            name="Permissions",
            passed=False,
            message=f"Found {len(permission_issues)} director{'y' if len(permission_issues) == 1 else 'ies'} with permission issues",
            details="\n".join(details_lines),
        )
    
    return CheckResult(
        name="Permissions",
        passed=True,
        message=f"Write permissions OK for {len(dirs_to_check)} director{'y' if len(dirs_to_check) == 1 else 'ies'}",
    )
def check_optional_dependencies() -> CheckResult:
    """Check which optional dependency groups are installed."""
    # Define optional dependency groups and their key packages
    optional_groups = {
        "dev": [
            ("pytest", "pytest"),
            ("black", "black"),
            ("mypy", "mypy"),
            ("isort", "isort"),
        ],
        "vector": [
            ("sentence_transformers", "sentence-transformers"),
            ("faiss", "faiss-cpu"),
        ],
    }

    installed_groups = []
    partially_installed = []
    not_installed = []

    for group_name, packages in optional_groups.items():
        installed_count = 0
        missing_packages = []

        for import_name, pip_name in packages:
            try:
                __import__(import_name)
                installed_count += 1
            except ImportError:
                missing_packages.append(pip_name)

        if installed_count == len(packages):
            installed_groups.append(group_name)
        elif installed_count > 0:
            partially_installed.append((group_name, missing_packages))
        else:
            not_installed.append(group_name)

    # Build message and details
    if installed_groups and not partially_installed and not not_installed:
        message = f"All optional groups installed: {', '.join(installed_groups)}"
        details = None
    elif not installed_groups and not partially_installed:
        message = "No optional dependency groups installed"
        details = (
            "Optional groups available:\n"
            f"  - [dev]: Development tools (pytest, black, mypy, isort)\n"
            f"  - [vector]: Vector search dependencies (sentence-transformers, faiss)\n"
            f"\nInstall with: pip install kano-agent-backlog-skill[dev] or [vector]"
        )
    else:
        parts = []
        if installed_groups:
            parts.append(f"Installed: {', '.join(installed_groups)}")
        if not_installed:
            parts.append(f"Not installed: {', '.join(not_installed)}")
        if partially_installed:
            parts.append(f"Partially installed: {', '.join(g for g, _ in partially_installed)}")

        message = "; ".join(parts)

        details_lines = []
        if partially_installed:
            details_lines.append("Partially installed groups:")
            for group_name, missing in partially_installed:
                details_lines.append(f"  [{group_name}] missing: {', '.join(missing)}")

        if not_installed:
            details_lines.append("\nNot installed groups:")
            for group_name in not_installed:
                details_lines.append(f"  [{group_name}]")

        if details_lines:
            details_lines.append("\nInstall with: pip install kano-agent-backlog-skill[dev] or [vector]")
            details = "\n".join(details_lines)
        else:
            details = None

    return CheckResult(
        name="Optional Dependencies",
        passed=True,  # This is informational, not a failure
        message=message,
        details=details,
    )


def check_permissions(
    backlog_root: Optional[Path] = None,
) -> CheckResult:
    """Check write permissions on backlog root and key directories."""
    # Find backlog root
    if backlog_root is None:
        # Try to find _kano/backlog in current directory or parents
        cwd = Path.cwd()
        for parent in [cwd] + list(cwd.parents):
            candidate = parent / "_kano" / "backlog"
            if candidate.exists():
                backlog_root = candidate
                break
    
    if backlog_root is None or not backlog_root.exists():
        return CheckResult(
            name="Permissions",
            passed=True,
            message="Backlog root not found (skipping permissions check)",
        )
    
    # Directories to check
    dirs_to_check = [
        ("Backlog root", backlog_root),
    ]
    
    # Check products directory
    products_dir = backlog_root / "products"
    if products_dir.exists():
        dirs_to_check.append(("Products directory", products_dir))
        
        # Check each product's key directories
        for product_dir in products_dir.iterdir():
            if not product_dir.is_dir() or product_dir.name.startswith("_"):
                continue
            
            dirs_to_check.append((f"Product '{product_dir.name}'", product_dir))
            
            # Check product subdirectories
            for subdir_name in ["items", "decisions", "_meta"]:
                subdir = product_dir / subdir_name
                if subdir.exists():
                    dirs_to_check.append((f"Product '{product_dir.name}' {subdir_name}", subdir))
    
    # Check permissions
    permission_issues = []
    for dir_name, dir_path in dirs_to_check:
        # Check if directory is writable
        # Try to create a temporary file to test write permissions
        try:
            test_file = dir_path / ".permission_test_tmp"
            test_file.touch()
            test_file.unlink()
        except (PermissionError, OSError) as e:
            permission_issues.append((dir_name, dir_path, str(e)))
    
    if permission_issues:
        details_lines = ["Directories lacking write permissions:"]
        for dir_name, dir_path, error in permission_issues:
            details_lines.append(f"  ✗ {dir_name}: {dir_path}")
            details_lines.append(f"    Error: {error}")
        
        details_lines.append("\nRecommendations:")
        details_lines.append("  - Check file system permissions")
        details_lines.append("  - Ensure you have write access to the backlog directory")
        details_lines.append("  - On Unix systems, use: chmod -R u+w <directory>")
        details_lines.append("  - On Windows, check folder properties and security settings")
        
        return CheckResult(
            name="Permissions",
            passed=False,
            message=f"Found {len(permission_issues)} director{'y' if len(permission_issues) == 1 else 'ies'} with permission issues",
            details="\n".join(details_lines),
        )
    
    return CheckResult(
        name="Permissions",
        passed=True,
        message=f"Write permissions OK for {len(dirs_to_check)} director{'y' if len(dirs_to_check) == 1 else 'ies'}",
    )


def check_configuration_validity(
    backlog_root: Optional[Path] = None,
) -> CheckResult:
    """Check that configuration files are valid and contain required fields."""
    # Find backlog root
    if backlog_root is None:
        # Try to find _kano/backlog in current directory or parents
        cwd = Path.cwd()
        for parent in [cwd] + list(cwd.parents):
            candidate = parent / "_kano" / "backlog"
            if candidate.exists():
                backlog_root = candidate
                break

    if backlog_root is None or not backlog_root.exists():
        return CheckResult(
            name="Configuration Validity",
            passed=True,
            message="Backlog root not found (skipping config validation)",
        )

    errors = []
    warnings = []

    # Check 1: Shared defaults config (_kano/backlog/_shared/defaults.toml or defaults.json)
    shared_dir = backlog_root / "_shared"
    if shared_dir.exists():
        defaults_toml = shared_dir / "defaults.toml"
        defaults_json = shared_dir / "defaults.json"

        if defaults_toml.exists():
            try:
                # Try to load TOML
                if tomllib is None:
                    warnings.append(f"Cannot validate {defaults_toml}: tomllib/tomli not available")
                else:
                    with open(defaults_toml, "rb") as f:
                        data = tomllib.load(f)
                    if not isinstance(data, dict):
                        errors.append(f"{defaults_toml}: Config must be a TOML table (got {type(data).__name__})")
            except Exception as e:
                error_msg = str(e)
                # Try to extract line number from error message
                if hasattr(e, 'lineno'):
                    error_msg = f"line {e.lineno}: {error_msg}"
                errors.append(f"{defaults_toml}: Invalid TOML syntax - {error_msg}")

        if defaults_json.exists():
            try:
                # Try to load JSON
                with open(defaults_json, "r", encoding="utf-8") as f:
                    data = json.load(f)
                if not isinstance(data, dict):
                    errors.append(f"{defaults_json}: Config must be a JSON object (got {type(data).__name__})")
                warnings.append(f"{defaults_json}: JSON config is deprecated, migrate to TOML")
            except json.JSONDecodeError as e:
                errors.append(f"{defaults_json}: Invalid JSON syntax - line {e.lineno}, column {e.colno}: {e.msg}")
            except Exception as e:
                errors.append(f"{defaults_json}: Failed to load - {e}")

    # Check 2: Project config (.kano/backlog_config.toml)
    project_root = backlog_root.parent.parent
    project_config_toml = project_root / ".kano" / "backlog_config.toml"

    if project_config_toml.exists():
        try:
            if tomllib is None:
                warnings.append(f"Cannot validate {project_config_toml}: tomllib/tomli not available")
            else:
                with open(project_config_toml, "rb") as f:
                    data = tomllib.load(f)
                if not isinstance(data, dict):
                    errors.append(f"{project_config_toml}: Config must be a TOML table (got {type(data).__name__})")
                else:
                    # Check for required fields in project config
                    if "products" not in data or not isinstance(data.get("products"), dict):
                        errors.append(f"{project_config_toml}: Missing required 'products' table")
                    else:
                        products = data["products"]
                        if not products:
                            warnings.append(f"{project_config_toml}: 'products' table is empty")
                        else:
                            # Validate each product has required fields
                            for product_name, product_config in products.items():
                                if not isinstance(product_config, dict):
                                    errors.append(
                                        f"{project_config_toml}: Product '{product_name}' must be a table"
                                    )
                                    continue

                                # Check required fields: name, prefix, backlog_root
                                if "name" not in product_config:
                                    errors.append(
                                        f"{project_config_toml}: Product '{product_name}' missing required field 'name'"
                                    )
                                if "prefix" not in product_config:
                                    errors.append(
                                        f"{project_config_toml}: Product '{product_name}' missing required field 'prefix'"
                                    )
                                if "backlog_root" not in product_config:
                                    errors.append(
                                        f"{project_config_toml}: Product '{product_name}' missing required field 'backlog_root'"
                                    )
        except Exception as e:
            error_msg = str(e)
            # Try to extract line number from error message
            if hasattr(e, 'lineno'):
                error_msg = f"line {e.lineno}: {error_msg}"
            errors.append(f"{project_config_toml}: Invalid TOML syntax - {error_msg}")
    else:
        # Project config is required in the new architecture
        errors.append(
            f"{project_config_toml}: Project config file not found. "
            "Create .kano/backlog_config.toml in project root."
        )

    # Check 3: Product-specific configs (deprecated but check if they exist)
    products_dir = backlog_root / "products"
    if products_dir.exists():
        for product_dir in products_dir.iterdir():
            if not product_dir.is_dir() or product_dir.name.startswith("_"):
                continue

            config_dir = product_dir / "_config"
            if config_dir.exists():
                config_json = config_dir / "config.json"
                if config_json.exists():
                    try:
                        with open(config_json, "r", encoding="utf-8") as f:
                            data = json.load(f)
                        if not isinstance(data, dict):
                            errors.append(
                                f"{config_json}: Config must be a JSON object (got {type(data).__name__})"
                            )
                        else:
                            # Check for required fields in product config
                            if "project" not in data or not isinstance(data.get("project"), dict):
                                errors.append(f"{config_json}: Missing required 'project' object")
                            else:
                                project = data["project"]
                                if "name" not in project:
                                    errors.append(f"{config_json}: Missing required field 'project.name'")
                                if "prefix" not in project:
                                    errors.append(f"{config_json}: Missing required field 'project.prefix'")

                        warnings.append(
                            f"{config_json}: Product-specific config is deprecated. "
                            "Migrate to project-level .kano/backlog_config.toml"
                        )
                    except json.JSONDecodeError as e:
                        errors.append(
                            f"{config_json}: Invalid JSON syntax - line {e.lineno}, column {e.colno}: {e.msg}"
                        )
                    except Exception as e:
                        errors.append(f"{config_json}: Failed to load - {e}")

    # Determine result
    if errors:
        details_lines = []
        if errors:
            details_lines.append("Errors:")
            for error in errors:
                details_lines.append(f"  ✗ {error}")
        if warnings:
            details_lines.append("\nWarnings:")
            for warning in warnings:
                details_lines.append(f"  ⚠ {warning}")

        return CheckResult(
            name="Configuration Validity",
            passed=False,
            message=f"Found {len(errors)} configuration error(s)",
            details="\n".join(details_lines),
        )

    if warnings:
        details_lines = ["Warnings:"]
        for warning in warnings:
            details_lines.append(f"  ⚠ {warning}")

        return CheckResult(
            name="Configuration Validity",
            passed=True,
            message=f"Configuration valid with {len(warnings)} warning(s)",
            details="\n".join(details_lines),
        )

    return CheckResult(
        name="Configuration Validity",
        passed=True,
        message="All configuration files are valid",
    )


def check_kano_backlog_cli() -> CheckResult:
    """Check that kano-backlog CLI is available."""
    try:
        from kano_backlog_cli import cli
        return CheckResult(
            name="Kano CLI",
            passed=True,
            message="CLI module available",
        )
    except ImportError as e:
        return CheckResult(
            name="Kano CLI",
            passed=False,
            message="CLI module not found",
            details=str(e),
        )


def _format_sequence_details(status_map: Dict[str, Dict[str, Any]]) -> str:
    order = ["EPIC", "FTR", "USR", "TSK", "BUG"]
    lines: List[str] = []
    for type_code in order:
        status = status_map.get(type_code, {})
        state = status.get("status", "MISSING")
        db_next = status.get("db_next")
        file_max = status.get("file_max")
        if file_max is None:
            file_max = status.get("file_next")
        db_value = db_next if db_next is not None else "missing"
        lines.append(f"{type_code}: {state} (db={db_value}, files={file_max})")
    return "\n".join(lines)


def check_db_sequences(
    product: Optional[str] = None,
    fix: bool = False,
) -> CheckResult:
    """Check DB ID sequences against filesystem max IDs."""
    try:
        from kano_backlog_core.config import ConfigLoader
        from kano_backlog_core.project_config import ProjectConfigLoader
        from kano_backlog_ops import item_utils
    except Exception as exc:
        return CheckResult(
            name="DB Sequences",
            passed=False,
            message="Required modules not available for sequence check",
            details=str(exc),
        )

    product_name = product
    if not product_name:
        project_config = ProjectConfigLoader.load_project_config_optional(Path.cwd())
        if not project_config:
            return CheckResult(
                name="DB Sequences",
                passed=True,
                message="Sequence check skipped (project config not found)",
            )
        products = project_config.list_products()
        if len(products) == 1:
            product_name = products[0]
        else:
            return CheckResult(
                name="DB Sequences",
                passed=True,
                message="Sequence check skipped (multiple products; use --product)",
                details=f"Products: {', '.join(products)}",
            )

    try:
        ctx = ConfigLoader.from_path(Path.cwd(), product=product_name)
        product_root = ctx.product_root
    except Exception as exc:
        return CheckResult(
            name="DB Sequences",
            passed=False,
            message=f"Failed to resolve product '{product_name}'",
            details=str(exc),
        )

    db_path, status_map = item_utils.check_sequence_health(product_name, product_root)
    needs_fix = any(
        status.get("status") != "OK" for status in status_map.values()
    )
    fixed = False

    if fix and needs_fix:
        try:
            item_utils.sync_id_sequences(product=product_name, backlog_root=None, dry_run=False)
            db_path, status_map = item_utils.check_sequence_health(product_name, product_root)
            fixed = True
        except Exception as exc:
            return CheckResult(
                name="DB Sequences",
                passed=False,
                message=f"Sequence sync failed for '{product_name}'",
                details=str(exc),
            )

    all_ok = all(status.get("status") == "OK" for status in status_map.values())
    if all_ok:
        message = f"Sequence health OK for '{product_name}'"
    elif fixed:
        message = f"Sequences synced for '{product_name}'"
    else:
        message = f"Sequences stale or missing for '{product_name}'"

    details = _format_sequence_details(status_map)
    if db_path:
        details = f"DB: {db_path}\n{details}"

    return CheckResult(
        name="DB Sequences",
        passed=all_ok,
        message=message,
        details=details,
    )


def run_doctor(
    product: Optional[str] = None,
    backlog_root: Optional[Path] = None,
    fix: bool = False,
) -> DoctorResult:
    """Run all doctor checks."""
    checks = [
        check_python_version(),
        check_python_prereqs(),
        check_sqlite_availability(),
        check_optional_dependencies(),
        check_skill_layout(),
        check_backlog_structure(backlog_root=backlog_root),
        check_permissions(backlog_root=backlog_root),
        check_configuration_validity(backlog_root=backlog_root),
        check_backlog_initialized(product=product, backlog_root=backlog_root),
        check_db_sequences(product=product, fix=fix),
        check_kano_backlog_cli(),
    ]
    
    all_passed = all(c.passed for c in checks)
    return DoctorResult(all_passed=all_passed, checks=checks)


def format_result_plain(result: DoctorResult) -> None:
    """Print result in plain text format with rich formatting."""
    from rich.panel import Panel
    from rich.text import Text
    
    # Print header
    console.print()
    console.print(Panel.fit(
        "[bold cyan]Kano Agent Backlog Skill - Environment Health Check[/bold cyan]",
        border_style="cyan"
    ))
    console.print()

    # Categorize checks
    passed_checks = []
    warning_checks = []
    failed_checks = []
    
    for check in result.checks:
        if check.passed:
            # Check if it's a warning (passed but has details with warnings)
            if check.details and ("warning" in check.message.lower() or "⚠" in check.details):
                warning_checks.append(check)
            else:
                passed_checks.append(check)
        else:
            failed_checks.append(check)

    # Print failed checks first (most important)
    if failed_checks:
        console.print("[red bold]✗ Failed Checks[/red bold]")
        console.print()
        for check in failed_checks:
            console.print(f"[red]✗ {check.name}[/red]")
            console.print(f"   {check.message}")
            if check.details:
                console.print()
                console.print("[yellow]   Recommendations:[/yellow]")
                # Format details with proper indentation
                for line in check.details.split("\n"):
                    if line.strip():
                        console.print(f"   {line}")
            console.print()

    # Print warnings
    if warning_checks:
        console.print("[yellow bold]⚠ Warnings[/yellow bold]")
        console.print()
        for check in warning_checks:
            console.print(f"[yellow]⚠ {check.name}[/yellow]")
            console.print(f"   {check.message}")
            if check.details:
                console.print()
                for line in check.details.split("\n"):
                    if line.strip():
                        console.print(f"   [dim]{line}[/dim]")
            console.print()

    # Print passed checks (summary only)
    if passed_checks:
        console.print("[green bold]✓ Passed Checks[/green bold]")
        console.print()
        for check in passed_checks:
            console.print(f"[green]✓ {check.name}[/green]: {check.message}")
        console.print()

    # Print summary
    console.print("─" * 60)
    total = len(result.checks)
    passed = len(passed_checks)
    warnings = len(warning_checks)
    failed = len(failed_checks)
    
    summary_text = Text()
    summary_text.append("Summary: ", style="bold")
    summary_text.append(f"{passed} passed", style="green")
    if warnings > 0:
        summary_text.append(f", {warnings} warning{'s' if warnings != 1 else ''}", style="yellow")
    if failed > 0:
        summary_text.append(f", {failed} failed", style="red")
    summary_text.append(f" (total: {total})")
    
    console.print(summary_text)
    console.print()

    if result.all_passed:
        if warnings > 0:
            console.print(Panel(
                "[yellow]✓ All critical checks passed, but there are warnings to review.[/yellow]",
                border_style="yellow"
            ))
        else:
            console.print(Panel(
                "[green bold]✓ All checks passed! Your environment is ready.[/green bold]",
                border_style="green"
            ))
    else:
        console.print(Panel(
            "[red bold]✗ Some checks failed. Please address the issues above before proceeding.[/red bold]",
            border_style="red"
        ))
    console.print()

def format_result_json(result: DoctorResult) -> None:
    """Print result in JSON format."""
    output = {
        "all_passed": result.all_passed,
        "checks": [asdict(c) for c in result.checks],
    }
    print(json.dumps(output, indent=2))


@app.command()
def doctor(
    product: Optional[str] = typer.Option(
        None, "--product", "-p",
        help="Product name to check (optional)",
    ),
    fix: bool = typer.Option(
        False,
        "--fix",
        help="Auto-sync DB ID sequences when stale",
    ),
    format: str = typer.Option(
        "plain", "--format", "-f",
        help="Output format: plain, json",
    ),
) -> None:
    """
    Check environment health.
    
    Verifies:
    - Python version meets requirements (>= 3.8)
    - Python prerequisites are installed
    - SQLite is available and meets version requirements
    - Backlog directory structure is valid
    - Backlog is initialized with at least one product
    - DB ID sequences are healthy (optionally auto-synced)
    - Kano CLI is available
    """
    result = run_doctor(product=product, fix=fix)
    
    if format == "json":
        format_result_json(result)
    else:
        format_result_plain(result)
    
    raise typer.Exit(0 if result.all_passed else 1)


if __name__ == "__main__":
    app()
