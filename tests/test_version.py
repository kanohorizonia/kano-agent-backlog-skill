"""Tests for version management system.

This module tests the version management implementation for task 1.4
of the current release preparation spec.
"""

from kano_backlog_core import __version__, __version_info__


def test_version_string_format():
    """Version string should follow semantic versioning format."""
    assert isinstance(__version__, str)
    parts = __version__.split(".")
    assert len(parts) == 3, f"Version should have 3 parts, got {len(parts)}"
    for part in parts:
        assert part.isdigit(), f"Version part '{part}' should be numeric"


def test_version_info_tuple():
    """Version info should be a tuple of three integers."""
    assert isinstance(__version_info__, tuple)
    assert len(__version_info__) == 3
    assert all(isinstance(x, int) for x in __version_info__)


def test_version_consistency():
    """Version string and version info should be consistent."""
    major, minor, patch = __version_info__
    expected = f"{major}.{minor}.{patch}"
    assert __version__ == expected, f"Version mismatch: {__version__} != {expected}"


def test_version_is_0_0_3():
    """Version should be 0.0.3 for this release."""
    assert __version__ == "0.0.3"
    assert __version_info__ == (0, 0, 3)


def test_version_accessible_from_package():
    """Version should be accessible from the main package."""
    from kano_backlog_core import __version__ as v
    assert v == "0.0.3"


def test_version_accessible_from_version_module():
    """Version should be accessible from __version__ module."""
    from kano_backlog_core.__version__ import __version__ as v
    assert v == "0.0.3"
