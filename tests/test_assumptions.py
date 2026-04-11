"""Tests for Assumption schema."""

import pytest
from kano_backlog_core.models import Assumption, AssumptionStatus


class TestAssumptionCreation:
    """Test Assumption creation with various configurations."""

    def test_create_minimal(self):
        """Create with required fields only."""
        a = Assumption(
            id="asm-1",
            statement="the system will be deployed on Linux",
            source="ADR-001",
        )
        assert a.id == "asm-1"
        assert a.statement == "the system will be deployed on Linux"
        assert a.source == "ADR-001"
        assert a.status == AssumptionStatus.STATED
        assert a.validated_at is None
        assert a.notes is None

    def test_create_with_all_fields(self):
        """Create with all optional fields set."""
        a = Assumption(
            id="asm-2",
            statement="users prefer dark mode",
            source="USER_STORY-0042",
            status=AssumptionStatus.VALIDATED,
            validated_at="2026-01-15T10:30:00Z",
            notes="confirmed via user research",
        )
        assert a.id == "asm-2"
        assert a.status == AssumptionStatus.VALIDATED
        assert a.validated_at == "2026-01-15T10:30:00Z"
        assert a.notes == "confirmed via user research"

    def test_status_enum_values(self):
        """Status enum has correct values."""
        assert AssumptionStatus.STATED.value == "stated"
        assert AssumptionStatus.VALIDATED.value == "validated"
        assert AssumptionStatus.INVALIDATED.value == "invalidated"
        assert AssumptionStatus.UNKNOWN.value == "unknown"


class TestAssumptionSerialization:
    """Test Assumption serialization/deserialization."""

    def test_to_dict(self):
        """Serialize to dict and verify all fields."""
        a = Assumption(
            id="asm-s",
            statement="API responses under 100ms",
            source="ADR-010",
            status=AssumptionStatus.VALIDATED,
            validated_at="2026-02-01T09:00:00Z",
            notes="performance test passed",
        )
        d = a.to_dict()
        assert d["id"] == "asm-s"
        assert d["statement"] == "API responses under 100ms"
        assert d["source"] == "ADR-010"
        assert d["status"] == AssumptionStatus.VALIDATED
        assert d["validated_at"] == "2026-02-01T09:00:00Z"
        assert d["notes"] == "performance test passed"

    def test_to_json(self):
        """Serialize to JSON string."""
        a = Assumption(
            id="asm-json",
            statement="TLS 1.3 will be supported",
            source="ADR-012",
        )
        j = a.to_json()
        assert '"asm-json"' in j
        assert '"TLS 1.3 will be supported"' in j

    def test_from_dict_roundtrip(self):
        """Deserialize from dict and verify all fields match."""
        original = Assumption(
            id="asm-rt",
            statement="roundtrip test",
            source="test.md",
            status=AssumptionStatus.INVALIDATED,
            validated_at="2026-03-01T12:00:00Z",
            notes="falsified by test",
        )
        restored = Assumption.from_dict(original.to_dict())
        assert restored.id == original.id
        assert restored.statement == original.statement
        assert restored.source == original.source
        assert restored.status == original.status
        assert restored.validated_at == original.validated_at
        assert restored.notes == original.notes

    def test_from_json_roundtrip(self):
        """Deserialize from JSON string and verify matches original."""
        original = Assumption(
            id="asm-json-rt",
            statement="json roundtrip",
            source="test.md",
        )
        restored = Assumption.from_json(original.to_json())
        assert restored.id == original.id
        assert restored.statement == original.statement
        assert restored.source == original.source


class TestAssumptionStatusTransitions:
    """Test status can be set and represents valid lifecycle."""

    def test_default_status_is_stated(self):
        """New assumptions default to STATED status."""
        a = Assumption(
            id="asm-new",
            statement="some assumption",
            source="test.md",
        )
        assert a.status == AssumptionStatus.STATED

    def test_status_can_be_validated(self):
        """Assumption can transition to VALIDATED."""
        a = Assumption(
            id="asm-validate",
            statement="the database will be PostgreSQL",
            source="ADR-005",
            status=AssumptionStatus.VALIDATED,
            validated_at="2026-01-20T14:00:00Z",
        )
        assert a.status == AssumptionStatus.VALIDATED

    def test_status_can_be_invalidated(self):
        """Assumption can transition to INVALIDATED."""
        a = Assumption(
            id="asm-invalidate",
            statement="no need for caching",
            source="ADR-007",
            status=AssumptionStatus.INVALIDATED,
            validated_at="2026-01-25T16:00:00Z",
            notes="cache needed after load test",
        )
        assert a.status == AssumptionStatus.INVALIDATED
