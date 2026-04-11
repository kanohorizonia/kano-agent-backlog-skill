"""Pydantic models for backlog items."""

from enum import Enum
from typing import Dict, Any, List, Optional
from pathlib import Path
from pydantic import BaseModel, Field, ConfigDict


class ItemType(str, Enum):
    """Backlog item type."""

    EPIC = "Epic"
    FEATURE = "Feature"
    USER_STORY = "UserStory"
    TASK = "Task"
    BUG = "Bug"


class ItemState(str, Enum):
    """Backlog item state."""

    NEW = "New"
    PROPOSED = "Proposed"
    PLANNED = "Planned"
    READY = "Ready"
    IN_PROGRESS = "InProgress"
    REVIEW = "Review"
    DONE = "Done"
    BLOCKED = "Blocked"
    DROPPED = "Dropped"


class StateAction(str, Enum):
    """Actions that trigger state transitions."""

    PROPOSE = "propose"      # New → Proposed
    READY = "ready"          # Proposed → Ready
    START = "start"          # Ready/New → InProgress
    REVIEW = "review"        # InProgress → Review
    DONE = "done"            # InProgress/Review → Done
    BLOCK = "block"          # Any → Blocked
    DROP = "drop"            # Any → Dropped


class BacklogItem(BaseModel):
    """Parsed backlog item with frontmatter and body."""

    # Frontmatter fields
    id: str = Field(..., description="Display ID (e.g., KABSD-TSK-0115)")
    uid: str = Field(..., description="UUIDv7 (immutable primary key)")
    type: ItemType
    title: str
    state: ItemState
    priority: Optional[str] = Field(None, description="P0, P1, P2, P3")
    parent: Optional[str] = Field(None, description="Parent display ID")
    owner: Optional[str] = Field(None, description="Agent or user name")
    tags: List[str] = Field(default_factory=list)
    created: str = Field(..., description="ISO date (YYYY-MM-DD)")
    updated: str = Field(..., description="ISO date (YYYY-MM-DD)")
    area: Optional[str] = None
    iteration: Optional[str] = None
    external: Dict[str, Any] = Field(default_factory=dict)
    links: Dict[str, List[str]] = Field(
        default_factory=lambda: {"relates": [], "blocks": [], "blocked_by": []}
    )
    decisions: List[str] = Field(default_factory=list, description="ADR references")

    # Body sections (parsed from markdown)
    context: Optional[str] = None
    goal: Optional[str] = None
    non_goals: Optional[str] = None
    approach: Optional[str] = None
    alternatives: Optional[str] = None
    acceptance_criteria: Optional[str] = None
    risks: Optional[str] = None
    worklog: List[str] = Field(default_factory=list, description="Parsed worklog entries")

    # Metadata
    file_path: Optional[Path] = Field(None, description="Absolute path to .md file")

    model_config = ConfigDict(
        arbitrary_types_allowed=True,  # Allow Path type
        use_enum_values=False,  # Keep enum objects, not string values
    )


class WorklogEntry(BaseModel):
    """Single worklog entry."""

    timestamp: str = Field(..., description="YYYY-MM-DD HH:MM format")
    agent: str
    model: Optional[str] = Field(None, description="Model used by agent (e.g., claude-sonnet-4.5, gpt-5.1)")
    message: str

    @classmethod
    def parse(cls, line: str) -> Optional["WorklogEntry"]:
        """
        Parse worklog line:
        - "2026-01-07 19:59 [agent=copilot] Message"
        - "2026-01-07 19:59 [agent=copilot] [model=claude-sonnet-4.5] Message"

        Returns:
            WorklogEntry or None if parse fails
        """
        import re

        # Pattern with optional [model=...] tag
        pattern = r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}) \[agent=([^\]]+)\](?:\s+\[model=([^\]]+)\])? (.+)$"
        match = re.match(pattern, line.strip())
        if not match:
            return None
        timestamp, agent, model, message = match.groups()
        return cls(timestamp=timestamp, agent=agent, model=model, message=message)

    def format(self) -> str:
        """Format as: 2026-01-07 19:59 [agent=copilot] [model=claude-sonnet-4.5] Message"""
        if self.model:
            return f"{self.timestamp} [agent={self.agent}] [model={self.model}] {self.message}"
        return f"{self.timestamp} [agent={self.agent}] {self.message}"


class EvidenceAxis(str, Enum):
    """The 5 axes for evaluating evidence quality."""

    RELEVANCE = "relevance"
    RELIABILITY = "reliability"
    SUFFICIENCY = "sufficiency"
    VERIFIABILITY = "verifiability"
    INDEPENDENCE = "independence"


class EvidenceRecord(BaseModel):
    """Evidence record with 5-axis credibility evaluation.

    Each axis is scored 0.0-1.0:
    - Relevance: How relevant is this evidence to the claim?
    - Reliability: How reliable is the source?
    - Sufficiency: Is there enough evidence?
    - Verifiability: Can the evidence be verified independently?
    - Independence: Is the evidence independent of other claims?
    """

    id: str = Field(..., description="Unique evidence ID")
    claim_id: str = Field(..., description="ID of the claim this evidence supports")
    source: str = Field(..., description="Source of the evidence (item ID, URL, etc.)")
    content: str = Field(..., description="The evidence content/text")
    relevance: float = Field(
        default=0.5,
        ge=0.0,
        le=1.0,
        description="Relevance score (0.0-1.0)",
    )
    reliability: float = Field(
        default=0.5,
        ge=0.0,
        le=1.0,
        description="Reliability score (0.0-1.0)",
    )
    sufficiency: float = Field(
        default=0.5,
        ge=0.0,
        le=1.0,
        description="Sufficiency score (0.0-1.0)",
    )
    verifiability: float = Field(
        default=0.5,
        ge=0.0,
        le=1.0,
        description="Verifiability score (0.0-1.0)",
    )
    independence: float = Field(
        default=0.5,
        ge=0.0,
        le=1.0,
        description="Independence score (0.0-1.0)",
    )
    notes: Optional[str] = Field(None, description="Optional notes about this evidence")

    model_config = ConfigDict(
        arbitrary_types_allowed=True,
        use_enum_values=False,
    )

    def to_dict(self) -> Dict[str, Any]:
        """Serialize to dict."""
        return self.model_dump()

    def to_json(self) -> str:
        """Serialize to JSON string."""
        return self.model_dump_json()

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "EvidenceRecord":
        """Deserialize from dict."""
        return cls(**data)

    @classmethod
    def from_json(cls, data: str) -> "EvidenceRecord":
        """Deserialize from JSON string."""
        return cls.model_validate_json(data)

    def overall_score(self) -> float:
        """Compute unweighted average of all 5 axes."""
        return (
            self.relevance
            + self.reliability
            + self.sufficiency
            + self.verifiability
            + self.independence
        ) / 5.0


class AssumptionStatus(str, Enum):
    """Assumption validation status."""

    STATED = "stated"
    VALIDATED = "validated"
    INVALIDATED = "invalidated"
    UNKNOWN = "unknown"


class Assumption(BaseModel):
    """An assumption or prior that can be tracked and validated."""

    id: str = Field(..., description="Unique assumption ID")
    statement: str = Field(..., description="The assumption text")
    status: AssumptionStatus = Field(
        default=AssumptionStatus.STATED,
        description="Current validation status",
    )
    source: str = Field(..., description="Source of the assumption (item ID, ADR, etc.)")
    validated_at: Optional[str] = Field(
        None,
        description="ISO timestamp when validated/invalidated",
    )
    notes: Optional[str] = Field(None, description="Optional notes")

    model_config = ConfigDict(
        arbitrary_types_allowed=True,
        use_enum_values=False,
    )

    def to_dict(self) -> Dict[str, Any]:
        """Serialize to dict."""
        return self.model_dump()

    def to_json(self) -> str:
        """Serialize to JSON string."""
        return self.model_dump_json()

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "Assumption":
        """Deserialize from dict."""
        return cls(**data)

    @classmethod
    def from_json(cls, data: str) -> "Assumption":
        """Deserialize from JSON string."""
        return cls.model_validate_json(data)
