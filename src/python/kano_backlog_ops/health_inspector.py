"""
health_inspector.py - Health Review Inspector for evidence credibility.

This module implements the Health Review Inspector per ADR-0037, consuming
the evidence query surface and producing a Trust Gap Report.

Checks implemented:
- Single source dependency (all evidence from one file/author)
- Jargon credentialism (claims backed by terminology, not data)
- Missing counter-examples (survivor bias check)
- Unverifiable claims (missing verification steps)
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional, Set

from kano_backlog_core.errors import BacklogError


# =============================================================================
# Error Types
# =============================================================================


class HealthInspectorError(BacklogError):
    """Base error for health inspector operations."""

    def __init__(self, message: str, suggestion: Optional[str] = None):
        self.message = message
        self.suggestion = suggestion
        super().__init__(message)


# =============================================================================
# Finding Model
# =============================================================================


@dataclass
class TrustGapFinding:
    """A single finding in the trust gap report."""

    finding_id: str
    check_name: str
    severity: str  # "critical", "warning", "info"
    item_id: str
    claim: str
    evidence_ref: Optional[str]
    recommendation: str


@dataclass
class HealthReport:
    """Full health review report."""

    generated_at: str
    inspector_version: str
    total_items_scanned: int
    items_with_issues: int
    findings: List[TrustGapFinding]
    by_severity: Dict[str, int]
    by_check: Dict[str, int]


# =============================================================================
# Credentialism Terms (high-confidence jargon signals)
# =============================================================================

_CREDENTIALISM_TERMS: Set[str] = {
    "best-in-class",
    "best-of-breed",
    "cutting-edge",
    "enterprise-grade",
    "industry-leading",
    "next-generation",
    "optimized",
    "robust",
    "scalable",
    "seamless",
    "state-of-the-art",
    "turnkey",
    "mission-critical",
    "world-class",
    "proven",
    "battle-tested",
    "production-ready",
    "enterprise-ready",
    "military-grade",
    "ai-powered",
    "ml-powered",
    "intelligent",
    "smart",
}

_CREDENTIALISM_PATTERN = re.compile(
    r"\b(" + "|".join(re.escape(t) for t in _CREDENTIALISM_TERMS) + r")\b",
    re.IGNORECASE,
)


# =============================================================================
# Core Credibility Checks
# =============================================================================


def _check_single_source_dependency(
    item_id: str,
    evidence_records: List[Any],
) -> List[TrustGapFinding]:
    """Check for single source dependency across evidence records."""
    findings: List[TrustGapFinding] = []

    # Group by source file
    by_source: Dict[str, List[Any]] = {}
    for rec in evidence_records:
        source = getattr(rec, "source", "unknown") or "unknown"
        if source not in by_source:
            by_source[source] = []
        by_source[source].append(rec)

    # If all evidence from one source, flag it
    if len(by_source) == 1 and len(evidence_records) > 1:
        single_source = list(by_source.keys())[0]
        findings.append(
            TrustGapFinding(
                finding_id=f"SSD-{item_id}-001",
                check_name="Single Source Dependency",
                severity="warning",
                item_id=item_id,
                claim=f"All {len(evidence_records)} evidence records cite a single source: {single_source}",
                evidence_ref=single_source,
                recommendation="Add evidence from independent sources to corroborate this claim.",
            )
        )

    return findings


def _check_jargon_credentialism(
    item_id: str,
    claim_text: str,
) -> List[TrustGapFinding]:
    """Check for jargon credentialism (vague praise terms without data)."""
    findings: List[TrustGapFinding] = []

    matches = _CREDENTIALISM_PATTERN.findall(claim_text)
    if matches:
        findings.append(
            TrustGapFinding(
                finding_id=f"JGC-{item_id}-001",
                check_name="Jargon Credentialism",
                severity="info",
                item_id=item_id,
                claim=f"Claim contains unsubstantiated praise terms: {', '.join(matches)}",
                evidence_ref=None,
                recommendation="Replace vague marketing terms with specific, verifiable metrics or data.",
            )
        )

    return findings


def _check_missing_counter_examples(
    item_id: str,
    claim_text: str,
    evidence_records: List[Any],
) -> List[TrustGapFinding]:
    """Check for survivorship bias / missing counter-examples."""
    findings: List[TrustGapFinding] = []

    # High confidence signal: strong claim with zero evidence
    STRONG_CLAIM_INDICATORS = [
        "always",
        "never",
        "all",
        "every",
        "must",
        "guaranteed",
        "100%",
        "100 percent",
        "impossible",
        "proven",
    ]

    has_strong_claim = any(
        indicator in claim_text.lower() for indicator in STRONG_CLAIM_INDICATORS
    )

    if has_strong_claim and len(evidence_records) == 0:
        findings.append(
            TrustGapFinding(
                finding_id=f"MCE-{item_id}-001",
                check_name="Missing Counter-examples",
                severity="warning",
                item_id=item_id,
                claim=f"Strong claim ('always'/'never'/'all') made without any supporting evidence",
                evidence_ref=None,
                recommendation="Provide counter-examples or qualifying conditions. Absolute claims without evidence indicate survivorship bias.",
            )
        )

    return findings


def _check_unverifiable_claims(
    item_id: str,
    claim_text: str,
    verification_steps: Optional[List[str]],
) -> List[TrustGapFinding]:
    """Check for unverifiable claims (missing verification steps)."""
    findings: List[TrustGapFinding] = []

    VERIFIABILITY_SIGNALS = [
        "tested",
        "verified",
        "measured",
        "benchmark",
        "experiment",
        "data",
        "metric",
        "result",
        "performance",
        "latency",
        "throughput",
    ]

    has_verifiable_signal = any(
        signal in claim_text.lower() for signal in VERIFIABILITY_SIGNALS
    )

    if has_verifiable_signal and not verification_steps:
        findings.append(
            TrustGapFinding(
                finding_id=f"UVC-{item_id}-001",
                check_name="Unverifiable Claims",
                severity="warning",
                item_id=item_id,
                claim=f"Claim references verifiable metrics but no verification steps provided",
                evidence_ref=None,
                recommendation="Add specific verification steps or link to evidence of the metrics.",
            )
        )

    return findings


# =============================================================================
# Report Formatting
# =============================================================================


def format_health_markdown(report: HealthReport) -> str:
    """Format health report as markdown."""
    lines = [
        "# Health Review Inspector Report",
        "",
        f"Generated: {report.generated_at}",
        f"Inspector Version: {report.inspector_version}",
        "",
        "## Summary",
        "",
        f"- Items scanned: {report.total_items_scanned}",
        f"- Items with issues: {report.items_with_issues}",
        f"- Total findings: {len(report.findings)}",
        "",
    ]

    if report.by_severity.get("critical", 0) > 0:
        lines.append(f"- **Critical**: {report.by_severity['critical']}")
    if report.by_severity.get("warning", 0) > 0:
        lines.append(f"- **Warning**: {report.by_severity['warning']}")
    if report.by_severity.get("info", 0) > 0:
        lines.append(f"- **Info**: {report.by_severity['info']}")

    lines.append("")
    lines.append("## Findings by Check")
    for check_name, count in report.by_check.items():
        if count > 0:
            lines.append(f"- **{check_name}**: {count}")

    if report.findings:
        lines.append("")
        lines.append("## Detailed Findings")
        for f in report.findings:
            lines.append("")
            lines.append(f"### [{f.severity.upper()}] {f.check_name} — {f.item_id}")
            lines.append(f"- **Finding ID**: {f.finding_id}")
            lines.append(f"- **Claim**: {f.claim}")
            if f.evidence_ref:
                lines.append(f"- **Evidence Ref**: {f.evidence_ref}")
            lines.append(f"- **Recommendation**: {f.recommendation}")

    return "\n".join(lines)