"""Tests for EvidenceRecord schema."""

import pytest
from kano_backlog_core.models import EvidenceRecord, EvidenceAxis


class TestEvidenceRecordCreation:
    """Test EvidenceRecord creation with various configurations."""

    def test_create_minimal_record(self):
        """Create with required fields only."""
        er = EvidenceRecord(
            id="ev-1",
            claim_id="cl-1",
            source="test.md",
            content="some evidence",
        )
        assert er.id == "ev-1"
        assert er.claim_id == "cl-1"
        assert er.source == "test.md"
        assert er.content == "some evidence"
        # All axes default to 0.5
        assert er.relevance == 0.5
        assert er.reliability == 0.5
        assert er.sufficiency == 0.5
        assert er.verifiability == 0.5
        assert er.independence == 0.5
        assert er.notes is None

    def test_create_with_all_axes(self):
        """Create with all axes explicitly set."""
        er = EvidenceRecord(
            id="ev-2",
            claim_id="cl-2",
            source="https://example.com",
            content="some content",
            relevance=0.9,
            reliability=0.8,
            sufficiency=0.7,
            verifiability=0.6,
            independence=0.5,
            notes="strong evidence",
        )
        assert er.relevance == 0.9
        assert er.reliability == 0.8
        assert er.sufficiency == 0.7
        assert er.verifiability == 0.6
        assert er.independence == 0.5
        assert er.notes == "strong evidence"

    def test_axis_bounds_reject_invalid(self):
        """Axes outside 0.0-1.0 range should be rejected."""
        with pytest.raises(ValueError):
            EvidenceRecord(
                id="ev-bad",
                claim_id="cl-1",
                source="test.md",
                content="x",
                relevance=1.5,  # > 1.0
            )
        with pytest.raises(ValueError):
            EvidenceRecord(
                id="ev-bad2",
                claim_id="cl-1",
                source="test.md",
                content="x",
                relevance=-0.1,  # < 0.0
            )


class TestEvidenceRecordSerialization:
    """Test EvidenceRecord serialization/deserialization."""

    def test_to_dict(self):
        """Serialize to dict and verify all fields present."""
        er = EvidenceRecord(
            id="ev-s",
            claim_id="cl-s",
            source="test.md",
            content="content",
            relevance=0.8,
            reliability=0.7,
            sufficiency=0.6,
            verifiability=0.5,
            independence=0.4,
            notes="note",
        )
        d = er.to_dict()
        assert d["id"] == "ev-s"
        assert d["claim_id"] == "cl-s"
        assert d["source"] == "test.md"
        assert d["content"] == "content"
        assert d["relevance"] == 0.8
        assert d["reliability"] == 0.7
        assert d["sufficiency"] == 0.6
        assert d["verifiability"] == 0.5
        assert d["independence"] == 0.4
        assert d["notes"] == "note"

    def test_to_json(self):
        """Serialize to JSON string."""
        er = EvidenceRecord(
            id="ev-json",
            claim_id="cl-json",
            source="test.md",
            content="content",
        )
        j = er.to_json()
        assert '"ev-json"' in j
        assert '"cl-json"' in j

    def test_from_dict_roundtrip(self):
        """Deserialize from dict and verify all fields match."""
        original = EvidenceRecord(
            id="ev-rt",
            claim_id="cl-rt",
            source="test.md",
            content="roundtrip content",
            relevance=0.9,
            reliability=0.85,
            sufficiency=0.8,
            verifiability=0.75,
            independence=0.7,
            notes="roundtrip note",
        )
        restored = EvidenceRecord.from_dict(original.to_dict())
        assert restored.id == original.id
        assert restored.claim_id == original.claim_id
        assert restored.content == original.content
        assert restored.relevance == original.relevance
        assert restored.reliability == original.reliability
        assert restored.sufficiency == original.sufficiency
        assert restored.verifiability == original.verifiability
        assert restored.independence == original.independence
        assert restored.notes == original.notes

    def test_from_json_roundtrip(self):
        """Deserialize from JSON string and verify matches original."""
        original = EvidenceRecord(
            id="ev-json-rt",
            claim_id="cl-json-rt",
            source="test.md",
            content="json roundtrip",
        )
        restored = EvidenceRecord.from_json(original.to_json())
        assert restored.id == original.id
        assert restored.claim_id == original.claim_id
        assert restored.content == original.content


class TestEvidenceRecordOverallScore:
    """Test overall_score() computation."""

    def test_all_max_scores(self):
        """Unweighted average of all 1.0 = 1.0."""
        er = EvidenceRecord(
            id="ev-max",
            claim_id="cl-1",
            source="test.md",
            content="x",
            relevance=1.0,
            reliability=1.0,
            sufficiency=1.0,
            verifiability=1.0,
            independence=1.0,
        )
        assert er.overall_score() == 1.0

    def test_all_min_scores(self):
        """Unweighted average of all 0.0 = 0.0."""
        er = EvidenceRecord(
            id="ev-min",
            claim_id="cl-1",
            source="test.md",
            content="x",
            relevance=0.0,
            reliability=0.0,
            sufficiency=0.0,
            verifiability=0.0,
            independence=0.0,
        )
        assert er.overall_score() == 0.0

    def test_mixed_scores(self):
        """Mixed scores produce correct unweighted average."""
        er = EvidenceRecord(
            id="ev-mixed",
            claim_id="cl-1",
            source="test.md",
            content="x",
            relevance=1.0,
            reliability=0.5,
            sufficiency=0.5,
            verifiability=0.5,
            independence=0.5,
        )
        # (1.0 + 0.5 + 0.5 + 0.5 + 0.5) / 5 = 0.6
        assert er.overall_score() == 0.6

    def test_default_scores(self):
        """Default axes (all 0.5) average to 0.5."""
        er = EvidenceRecord(
            id="ev-default",
            claim_id="cl-1",
            source="test.md",
            content="x",
        )
        assert er.overall_score() == 0.5


class TestEvidenceAxis:
    """Test EvidenceAxis enum."""

    def test_all_five_axes_present(self):
        """All 5 axes are defined."""
        assert len(EvidenceAxis) == 5
        assert EvidenceAxis.RELEVANCE.value == "relevance"
        assert EvidenceAxis.RELIABILITY.value == "reliability"
        assert EvidenceAxis.SUFFICIENCY.value == "sufficiency"
        assert EvidenceAxis.VERIFIABILITY.value == "verifiability"
        assert EvidenceAxis.INDEPENDENCE.value == "independence"
