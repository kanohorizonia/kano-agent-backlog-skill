"""Test version management system for Requirements 5.1-5.5."""

import re
import subprocess
import sys
from pathlib import Path

import pytest


def test_version_exists_in_version_file():
    """Verify __version__ exists in __version__.py (Requirement 5.2)."""
    from kano_backlog_core.__version__ import __version__
    
    assert __version__ is not None
    assert isinstance(__version__, str)
    assert __version__ == "0.0.3"


def test_version_info_tuple_exists():
    """Verify __version_info__ tuple exists in __version__.py (Requirement 5.2)."""
    from kano_backlog_core.__version__ import __version_info__
    
    assert __version_info__ is not None
    assert isinstance(__version_info__, tuple)
    assert len(__version_info__) == 3
    assert __version_info__ == (0, 0, 3)


def test_version_follows_semver():
    """Verify version follows Semantic Versioning (Requirement 5.1)."""
    from kano_backlog_core import __version__
    
    # Semantic versioning pattern: MAJOR.MINOR.PATCH
    semver_pattern = r'^\d+\.\d+\.\d+$'
    assert re.match(semver_pattern, __version__), \
        f"Version '{__version__}' does not follow semantic versioning"


def test_version_accessible_from_core_package():
    """Verify __version__ is accessible from kano_backlog_core package (Requirement 5.5)."""
    from kano_backlog_core import __version__, __version_info__
    
    assert __version__ == "0.0.3"
    assert __version_info__ == (0, 0, 3)


def test_cli_version_flag():
    """Verify CLI --version flag displays version (Requirement 5.4)."""
    # Test using the installed console script
    try:
        result = subprocess.run(
            ["kano-backlog", "--version"],
            capture_output=True,
            text=True,
        )
    except FileNotFoundError:
        result = subprocess.CompletedProcess(
            args=["kano-backlog", "--version"],
            returncode=1,
            stdout="",
            stderr="",
        )
    
    # If console script not available, test via module
    if result.returncode != 0:
        result = subprocess.run(
             [sys.executable, "-c", 
             "import sys; sys.path.insert(0, 'src/python'); "
             "from kano_backlog_cli.cli import app; "
             "app(['--version'], standalone_mode=False)"],
            capture_output=True,
            text=True,
            cwd=Path(__file__).parent.parent
        )
    
    assert result.returncode == 0
    # Version output might be in stdout or stderr
    output = result.stdout + result.stderr
    assert "0.0.3" in output, f"Expected '0.0.3' in output, got: {output}"
    assert "kano-backlog version" in output or "version" in output.lower()


def test_pyproject_dynamic_version_configured():
    """Verify pyproject.toml has dynamic version configured (Requirement 5.3)."""
    try:
        import tomllib  # Python 3.11+
    except ImportError:
        import tomli as tomllib  # Python 3.8-3.10
    
    pyproject_path = Path(__file__).parent.parent / "pyproject.toml"
    with open(pyproject_path, "rb") as f:
        config = tomllib.load(f)
    
    # Check that version is declared as dynamic
    assert "dynamic" in config["project"]
    assert "version" in config["project"]["dynamic"]
    
    # Check that setuptools.dynamic points to correct attribute
    assert "tool" in config
    assert "setuptools" in config["tool"]
    assert "dynamic" in config["tool"]["setuptools"]
    assert "version" in config["tool"]["setuptools"]["dynamic"]
    
    version_attr = config["tool"]["setuptools"]["dynamic"]["version"]
    assert "attr" in version_attr
    assert version_attr["attr"] == "kano_backlog_core.__version__.__version__"


def test_version_consistency():
    """Verify version is consistent across all locations (Requirement 5.3)."""
    from kano_backlog_core import __version__
    from kano_backlog_core.__version__ import __version__ as direct_version
    
    # Version from package import should match direct import
    assert __version__ == direct_version
    
    # Version should be 0.0.3 for this release line
    assert __version__ == "0.0.3"


def test_version_info_matches_version_string():
    """Verify __version_info__ tuple matches __version__ string."""
    from kano_backlog_core import __version__, __version_info__
    
    # Convert tuple to string and compare
    version_from_tuple = f"{__version_info__[0]}.{__version_info__[1]}.{__version_info__[2]}"
    assert version_from_tuple == __version__


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
