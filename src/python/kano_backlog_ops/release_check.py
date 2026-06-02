from __future__ import annotations

import os
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple


@dataclass
class CheckResult:
    name: str
    passed: bool
    message: str
    details: Optional[str] = None


@dataclass
class PhaseReport:
    phase: str  # "phase1" | "phase2"
    version: str
    generated_at: str
    checks: List[CheckResult]
    artifacts: List[Path]

    @property
    def all_passed(self) -> bool:
        return all(c.passed for c in self.checks)


def _now_iso() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _find_repo_root(start: Optional[Path] = None) -> Path:
    cur = (start or Path.cwd()).resolve()
    while True:
        if (cur / ".git").exists() or (cur / "_kano" / "backlog").exists():
            return cur
        if cur.parent == cur:
            raise RuntimeError("Could not locate repo root")
        cur = cur.parent


def _read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def _extract_readme_declared_version(readme: str) -> Optional[str]:
    # Very lightweight heuristic: look for 'VERSION X.Y.Z' line.
    for raw in readme.splitlines():
        line = raw.strip()
        if "VERSION" in line and "0." in line:
            # Example: "⚠️ **VERSION 0.0.2 - ...** ⚠️"
            for token in line.replace("*", " ").replace("-", " ").split():
                if token.count(".") == 2 and token[0].isdigit():
                    return token.strip()
    return None


def _check_exact_file_version(path: Path, expected: str) -> CheckResult:
    if not path.exists():
        return CheckResult(
            name=f"version:file:{path.as_posix()}",
            passed=False,
            message="missing",
        )
    actual = _read_text(path).strip()
    if actual != expected:
        return CheckResult(
            name=f"version:file:{path.as_posix()}",
            passed=False,
            message=f"mismatch (expected={expected}, actual={actual})",
        )
    return CheckResult(
        name=f"version:file:{path.as_posix()}",
        passed=True,
        message=f"ok ({actual})",
    )


def _check_pyproject_version(pyproject_path: Path, expected: str) -> CheckResult:
    if not pyproject_path.exists():
        return CheckResult(
            name=f"version:pyproject:{pyproject_path.as_posix()}",
            passed=False,
            message="missing",
        )
    text = _read_text(pyproject_path)
    needle = f'version = "{expected}"'
    if needle not in text:
        return CheckResult(
            name=f"version:pyproject:{pyproject_path.as_posix()}",
            passed=False,
            message=f"missing expected project.version {expected}",
        )
    return CheckResult(
        name=f"version:pyproject:{pyproject_path.as_posix()}",
        passed=True,
        message="ok",
    )


def _check_changelog_has_release(changelog_path: Path, expected: str) -> CheckResult:
    if not changelog_path.exists():
        return CheckResult(
            name=f"changelog:{changelog_path.as_posix()}",
            passed=False,
            message="missing",
        )
    text = _read_text(changelog_path)
    if f"## [{expected}]" not in text:
        return CheckResult(
            name=f"changelog:{changelog_path.as_posix()}",
            passed=False,
            message=f"release section [{expected}] not found",
        )
    return CheckResult(
        name=f"changelog:{changelog_path.as_posix()}",
        passed=True,
        message="ok",
    )


def _check_changelog_bullets_mapped(skill_root: Path) -> List[CheckResult]:
    # Deterministic mapping for 0.0.2 bullets.
    # We validate existence of key files and minimal CLI wiring strings.
    checks: List[CheckResult] = []

    def exists(rel: str) -> bool:
        return (skill_root / rel).exists()

    def contains(rel: str, needle: str) -> bool:
        p = skill_root / rel
        if not p.exists():
            return False
        return needle in _read_text(p)

    # Topic templates
    checks.append(
        CheckResult(
            name="0.0.2:topic-templates",
            passed=(
                exists("src/kano_backlog_core/template.py")
                and exists("src/kano_backlog_ops/template.py")
                and contains("src/kano_backlog_cli/commands/topic.py", "--template")
                and contains("src/kano_backlog_cli/commands/topic.py", "--var")
            ),
            message="template engine + ops + CLI flags",
            details=(
                "core=src/kano_backlog_core/template.py; "
                "ops=src/kano_backlog_ops/template.py; "
                "cli=src/kano_backlog_cli/commands/topic.py"
            ),
        )
    )

    # Topic cross-references
    checks.append(
        CheckResult(
            name="0.0.2:topic-cross-references",
            passed=(
                contains("src/kano_backlog_ops/topic.py", "related_topics")
                and contains("src/kano_backlog_ops/topic.py", "TopicAddReferenceResult")
            ),
            message="manifest.related_topics + add/remove results",
            details="ops=src/kano_backlog_ops/topic.py",
        )
    )

    # Topic snapshots
    checks.append(
        CheckResult(
            name="0.0.2:topic-snapshots",
            passed=(
                contains("src/kano_backlog_ops/topic.py", "class TopicSnapshot")
                and contains("src/kano_backlog_ops/topic.py", "class TopicRestoreResult")
            ),
            message="snapshot models + restore result",
            details="ops=src/kano_backlog_ops/topic.py",
        )
    )

    # Topic merge/split
    checks.append(
        CheckResult(
            name="0.0.2:topic-merge-split",
            passed=(
                contains("src/kano_backlog_ops/topic.py", "class TopicMergeResult")
                and contains("src/kano_backlog_ops/topic.py", "class TopicSplitResult")
                and contains("src/kano_backlog_ops/topic.py", "class TopicMergePlan")
                and contains("src/kano_backlog_ops/topic.py", "class TopicSplitPlan")
            ),
            message="merge/split + dry-run plan types",
            details="ops=src/kano_backlog_ops/topic.py",
        )
    )

    # Distill listing improvements
    checks.append(
        CheckResult(
            name="0.0.2:topic-distill-seed-item-rendering",
            passed=(
                contains("src/kano_backlog_ops/topic.py", "def distill_topic")
                and contains("src/kano_backlog_ops/topic.py", "<!-- uid:")
                and contains("src/kano_backlog_ops/topic.py", "item_id")
                and contains("src/kano_backlog_ops/topic.py", "item_type")
                and contains("src/kano_backlog_ops/topic.py", "state")
            ),
            message="human-readable seed listing + uid mapping comment",
            details="ops=src/kano_backlog_ops/topic.py",
        )
    )

    # Artifact attachment product layout resolution
    checks.append(
        CheckResult(
            name="0.0.2:attach-artifact-product-layout",
            passed=(
                contains("src/kano_backlog_cli/commands/workitem.py", "--backlog-root-override")
                and contains("src/kano_backlog_cli/commands/workitem.py", "--product")
                and contains("src/kano_backlog_ops/artifacts.py", "candidate = backlog_root / \"products\" / product")
            ),
            message="CLI flags + ops resolution candidate path",
            details=(
                "cli=src/kano_backlog_cli/commands/workitem.py; "
                "ops=src/kano_backlog_ops/artifacts.py"
            ),
        )
    )

    return checks


def run_phase1(*, version: str, repo_root: Optional[Path] = None) -> PhaseReport:
    repo_root = repo_root or _find_repo_root()
    skill_root = repo_root / "skills" / "kano-agent-backlog-skill"
    checks: List[CheckResult] = []

    # Version consistency
    readme_path = repo_root / "README.md"
    if readme_path.exists():
        declared = _extract_readme_declared_version(_read_text(readme_path))
        checks.append(
            CheckResult(
                name="version:README",
                passed=(declared == version),
                message=(
                    "ok" if declared == version else f"mismatch (expected={version}, actual={declared})"
                ),
                details=str(readme_path),
            )
        )
    else:
        checks.append(CheckResult(name="version:README", passed=False, message="missing"))

    checks.append(_check_exact_file_version(skill_root / "VERSION", version))
    checks.append(_check_pyproject_version(skill_root / "pyproject.toml", version))
    checks.append(_check_changelog_has_release(skill_root / "CHANGELOG.md", version))

    # Changelog bullet mapping
    checks.extend(_check_changelog_bullets_mapped(skill_root))

    return PhaseReport(
        phase="phase1",
        version=version,
        generated_at=_now_iso(),
        checks=checks,
        artifacts=[],
    )


def _run_cmd(
    cmd: List[str],
    *,
    cwd: Optional[Path] = None,
    timeout_s: int = 180,
    env: Optional[Dict[str, str]] = None,
) -> Tuple[int, str]:
    base_env = os.environ.copy()
    if env:
        base_env.update(env)
    proc = subprocess.run(
        cmd,
        cwd=str(cwd) if cwd else None,
        env=base_env,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout_s,
        check=False,
    )
    out = (proc.stdout or "") + ("\n" + proc.stderr if proc.stderr else "")
    return proc.returncode, out


def run_phase2(
    *,
    version: str,
    repo_root: Optional[Path] = None,
    sandbox_name: str,
    product: str,
    agent: str,
    artifact_dir: Optional[Path] = None,
) -> PhaseReport:
    repo_root = repo_root or _find_repo_root()
    checks: List[CheckResult] = []
    artifacts: List[Path] = []

    py = sys.executable
    kano = ["bash", "scripts/kob"]

    def write_artifact(name: str, content: str) -> Optional[Path]:
        if artifact_dir is None:
            return None
        artifact_dir.mkdir(parents=True, exist_ok=True)
        path = artifact_dir / name
        path.write_text(content, encoding="utf-8")
        artifacts.append(path)
        return path

    # doctor
    rc, out = _run_cmd(kano + ["doctor", "--format", "plain"], cwd=repo_root, timeout_s=120)
    write_artifact("phase2_doctor.txt", out)
    checks.append(
        CheckResult(
            name="phase2:doctor",
            passed=(rc == 0),
            message=f"exit={rc}",
            details=None,
        )
    )

    # pytest
    pytest_tmp_root = repo_root / "_kano" / "backlog" / "_tmp_tests" / "pytest"
    pytest_tmp_root.mkdir(parents=True, exist_ok=True)
    rc, out = _run_cmd(
        [
            py,
            "-m",
            "pytest",
            "skills/kano-agent-backlog-skill/tests",
            "-q",
            "-p",
            "no:cacheprovider",
            "-p",
            "no:pytest_cov",
            "--basetemp",
            str(pytest_tmp_root / "basetemp"),
        ],
        cwd=repo_root,
        timeout_s=240,
        env={
            "COVERAGE_FILE": str(pytest_tmp_root / ".coverage"),
        },
    )
    write_artifact("phase2_pytest.txt", out)
    checks.append(
        CheckResult(
            name="phase2:pytest",
            passed=(rc == 0),
            message=f"exit={rc}",
        )
    )

    # sandbox init (for isolation of product-level operations)
    rc, out = _run_cmd(
        kano
        + [
            "admin",
            "sandbox",
            "init",
            sandbox_name,
            "--product",
            product,
            "--agent",
            agent,
        ],
        cwd=repo_root,
        timeout_s=120,
    )
    write_artifact("phase2_sandbox_init.txt", out)
    out_lower = out.lower()
    checks.append(
        CheckResult(
            name="phase2:sandbox-init",
            passed=(rc == 0 or "sandbox already exists" in out_lower),
            message=f"exit={rc}",
            details=(
                "Note: sandbox creates an isolated layout under _kano/backlog_sandbox/<name>/; "
                "Phase2 topic smoke is executed against that sandbox root via --backlog-root-override."
            ),
        )
    )

    sandbox_backlog_root = repo_root / "_kano" / "backlog_sandbox" / sandbox_name

    # Cleanup smoke topics from previous runs to ensure deterministic creation
    import shutil
    smoke_topics_dir = sandbox_backlog_root / "topics"
    if smoke_topics_dir.exists():
        smoke_topic_a_path = smoke_topics_dir / f"release-{version.replace('.', '-')}-smoke-a"
        smoke_topic_b_path = smoke_topics_dir / f"release-{version.replace('.', '-')}-smoke-b"
        if smoke_topic_a_path.exists():
            try:
                shutil.rmtree(smoke_topic_a_path)
            except Exception:
                pass
        if smoke_topic_b_path.exists():
            try:
                shutil.rmtree(smoke_topic_b_path)
            except Exception:
                pass

    # Topic smoke (0.0.2 features)
    def smoke(
        name: str,
        args: List[str],
        *,
        timeout_s: int = 120,
        allow_contains: Optional[List[str]] = None,
    ) -> None:
        rc, out = _run_cmd(kano + args, cwd=repo_root, timeout_s=timeout_s)
        write_artifact(f"phase2_smoke_{name}.txt", out)
        passed = rc == 0
        if not passed and allow_contains:
            out_lower = out.lower()
            if any(token.lower() in out_lower for token in allow_contains):
                passed = True
        checks.append(
            CheckResult(
                name=f"phase2:smoke:{name}",
                passed=passed,
                message=f"exit={rc}",
            )
        )

    smoke_topic_a = f"release-{version.replace('.', '-')}-smoke-a"
    smoke_topic_b = f"release-{version.replace('.', '-')}-smoke-b"
    snap_name = "smoke"

    topic_base = [
        "topic",
        "--backlog-root-override",
        str(sandbox_backlog_root),
    ]

    smoke(
        "topic-create-a-template",
        [
            *topic_base,
            "create",
            smoke_topic_a,
            "--agent",
            agent,
            "--template",
            "research",
            "--var",
            "research_question=Does 0.0.2 release check pass?",
        ],
        allow_contains=["already exists"],
    )
    smoke(
        "topic-create-b",
        [
            *topic_base,
            "create",
            smoke_topic_b,
            "--agent",
            agent,
        ],
        allow_contains=["already exists"],
    )
    smoke(
        "topic-add-reference",
        [
            *topic_base,
            "add-reference",
            smoke_topic_a,
            "--to",
            smoke_topic_b,
        ],
        allow_contains=["already exists"],
    )
    smoke(
        "topic-snapshot-create",
        [
            *topic_base,
            "snapshot",
            "create",
            smoke_topic_a,
            snap_name,
            "--agent",
            agent,
            "--description",
            "release check smoke snapshot",
        ],
        allow_contains=["already exists"],
    )
    smoke(
        "topic-snapshot-list",
        [
            *topic_base,
            "snapshot",
            "list",
            smoke_topic_a,
            "--format",
            "plain",
        ],
    )
    smoke(
        "topic-snapshot-restore",
        [
            *topic_base,
            "snapshot",
            "restore",
            smoke_topic_a,
            snap_name,
            "--agent",
            agent,
            "--no-backup",
        ],
    )
    smoke(
        "topic-snapshot-cleanup-dry",
        [
            *topic_base,
            "snapshot",
            "cleanup",
            smoke_topic_a,
            "--ttl-days",
            "0",
            "--keep-latest",
            "1",
        ],
    )
    smoke(
        "topic-merge-dry",
        [
            *topic_base,
            "merge",
            smoke_topic_a,
            smoke_topic_b,
            "--agent",
            agent,
            "--dry-run",
            "--no-snapshots",
        ],
    )
    smoke(
        "topic-split-dry",
        [
            *topic_base,
            "split",
            smoke_topic_a,
            "--agent",
            agent,
            "--dry-run",
            "--no-snapshots",
            "--new-topic",
            f"{smoke_topic_a}-child:",
        ],
    )

    return PhaseReport(
        phase="phase2",
        version=version,
        generated_at=_now_iso(),
        checks=checks,
        artifacts=artifacts,
    )


def format_report_md(report: PhaseReport) -> str:
    lines: List[str] = []
    lines.append(f"# Release Check ({report.phase})")
    lines.append("")
    lines.append(f"- version: {report.version}")
    lines.append(f"- generated_at: {report.generated_at}")
    lines.append(f"- result: {'PASS' if report.all_passed else 'FAIL'}")
    lines.append("")
    lines.append("## Checks")
    lines.append("")
    for c in report.checks:
        status = "PASS" if c.passed else "FAIL"
        lines.append(f"- [{status}] {c.name}: {c.message}")
        if c.details:
            lines.append(f"  details: {c.details}")
    if report.artifacts:
        lines.append("")
        lines.append("## Artifacts")
        lines.append("")
        for a in report.artifacts:
            lines.append(f"- {a.as_posix()}")
    lines.append("")
    return "\n".join(lines)
