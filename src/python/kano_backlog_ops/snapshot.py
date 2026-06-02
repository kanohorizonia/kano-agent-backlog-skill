"""
snapshot.py - Repo Snapshot and Evidence Collection operations.

This module provides the core logic for generating deterministic "evidence packs"
that characterize the state of the repository, enabling evidence-driven reporting.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import List, Optional, Dict, Any, Union

from kano_backlog_core.canonical import CanonicalStore
from kano_backlog_ops.vcs import VcsMeta, get_vcs_meta


@dataclass
class SnapshotMeta:
    """Metadata for the snapshot evidence pack."""
    scope: str              # "repo" or "product:<name>"
    vcs: VcsMeta            # VCS metadata (agnostic)
    generator_version: str = "0.0.3"


@dataclass
class CliCommand:
    """Representation of a CLI command node."""
    name: str
    help: str
    subcommands: List["CliCommand"] = field(default_factory=list)


@dataclass
class StubEntry:
    """Evidence of a stub or unfinished implementation."""
    type: str           # "NotImplementedError" | "TODO" | "FIXME"
    file: str           # relative path
    line: int
    message: str
    ticket_ref: Optional[str] = None


@dataclass
class CapabilityEvidence:
    """Evidence of a functional capability."""
    area: str           # e.g. "workitem", "view", "index"
    feature: str        # e.g. "create", "refresh"
    status: str         # "done" | "partial" | "missing"
    evidence_refs: List[str]  # paths or command outputs
    item_id: Optional[str] = None


@dataclass
class HealthCheck:
    """Result of a health check."""
    name: str
    passed: bool
    message: str
    details: Optional[str] = None


@dataclass
class EvidencePack:
    """Container for all snapshot evidence."""
    meta: SnapshotMeta
    cli_tree: List[CliCommand] = field(default_factory=list)
    stub_inventory: List[StubEntry] = field(default_factory=list)
    capabilities: List[CapabilityEvidence] = field(default_factory=list)
    health: List[HealthCheck] = field(default_factory=list)

    def to_json(self) -> str:
        """Serialize to JSON."""
        return json.dumps(asdict(self), indent=2, sort_keys=True)

    @classmethod
    def from_json(cls, json_str: str) -> "EvidencePack":
        """Deserialize from JSON."""
        data = json.loads(json_str)
        # Reconstruct nested objects
        meta_data = data.get('meta', {}) or {}
        vcs_data = meta_data.get('vcs', {}) if isinstance(meta_data, dict) else {}

        # Backward compatibility:
        # - v1: provider/revision/ref/label/dirty
        # - v2: provider/branch/revno/hash/dirty
        branch = vcs_data.get('branch') or vcs_data.get('ref') or 'unknown'
        revno = vcs_data.get('revno') or 'unknown'
        vcs_hash = vcs_data.get('hash') or vcs_data.get('revision') or 'unknown'

        data['meta'] = SnapshotMeta(
            scope=meta_data.get('scope', 'repo'),
            vcs=VcsMeta(
                provider=vcs_data.get('provider', 'unknown'),
                branch=branch,
                revno=revno,
                hash=vcs_hash,
                dirty=vcs_data.get('dirty', 'unknown'),
            ),
            generator_version=meta_data.get('generator_version', '0.0.3'),
        )
        data['cli_tree'] = [cls._reconstruct_cli(c) for c in data.get('cli_tree', [])]
        data['stub_inventory'] = [StubEntry(**s) for s in data.get('stub_inventory', [])]
        data['capabilities'] = [CapabilityEvidence(**c) for c in data.get('capabilities', [])]
        data['health'] = [HealthCheck(**h) for h in data.get('health', [])]
        return cls(**data)

    @classmethod
    def _reconstruct_cli(cls, data: Dict[str, Any]) -> CliCommand:
        """Recursively reconstruct CliCommand."""
        data['subcommands'] = [cls._reconstruct_cli(c) for c in data.get('subcommands', [])]
        return CliCommand(**data)


def collect_stubs(src_root: Path, extensions: List[str] = [".py", ".md"]) -> List[StubEntry]:
    """
    Scan source code for stubs (NotImplementedError) and markers (TODO, FIXME).
    """
    stubs: List[StubEntry] = []
    
    # Patterns to look for
    patterns = [
        (r"raise NotImplementedError\((.*)\)", "NotImplementedError"),
        (r"#\s*TODO:?\s*(.*)", "TODO"),
        (r"#\s*FIXME:?\s*(.*)", "FIXME"),
    ]
    
    if not src_root.exists():
        return stubs

    # Walk directory
    for path in src_root.rglob("*"):
        if path.is_file() and path.suffix in extensions:
            if any(p in path.parts for p in ["node_modules", ".git", "__pycache__", ".venv", ".obsidian", "htmlcov", ".pytest_cache"]):
                continue
                
            try:
                rel_path = path.relative_to(src_root).as_posix()
                lines = path.read_text(encoding="utf-8").splitlines()
                
                for i, line in enumerate(lines, 1):
                    for pattern, stub_type in patterns:
                        match = re.search(pattern, line)
                        if match:
                            msg = match.group(1).strip().strip('"').strip("'")
                            
                            # Try to extract ticket ref from message (e.g. "KABSD-123")
                            ticket_ref = None
                            ticket_match = re.search(r"([A-Z]+-[A-Z]+-\d+)", msg)
                            if ticket_match:
                                ticket_ref = ticket_match.group(1)
                                
                            stubs.append(StubEntry(
                                type=stub_type,
                                file=rel_path,
                                line=i,
                                message=msg,
                                ticket_ref=ticket_ref
                            ))
                            break # Only one match per line relevant
            except Exception:
                # Ignore read errors
                continue
                
    return stubs


def collect_capabilities(product_root: Path, stubs: List[StubEntry] = []) -> List[CapabilityEvidence]:
    """
    Collect functional capabilities from Backlog Features.
    """
    caps: List[CapabilityEvidence] = []
    
    # We need to find the backlog root. 
    # Assumption: product_root is either .../products/<name> or the repository root.
    # If it's repo root, we might not be able to load a specific product store easily without knowing the product name.
    # However, generate_pack passes (root_path, product_name).
    # If product_name is provided, we can load that product's features.
    
    # This function signature in previous step was (product_root: Path). 
    # Allowing it to be smarter: if "products" dir exists inside, it's a repo root.
    
    store_path = None
    if (product_root / "items").exists():
        store_path = product_root
    elif (product_root / "products").exists():
        # Repo root, but which product? 
        # For now, if passed a repo root, we skip detailed capability mapping 
        # unless we want to map ALL products. 
        # Let's keep it simple: Real capability mapping only works if we know the product context 
        # or if we are pointing at a product root.
        pass
        
    if not store_path:
        # Fallback check - if we are in a product dir
        if (product_root / "../../items").resolve().exists(): 
             # Maybe we are deeper? Unlikely given usage.
             pass
        return []

    try:
        store = CanonicalStore(store_path)
        items = store.list_items()
        
        for item_path in items:
            try:
                item = store.read(item_path)
                # Check normalized type name
                if str(item.type.value).lower() != "feature":
                    continue
                    
                # We found a feature.
                # Status determination
                status = "missing"
                # Use string values to be safe if enum mismatch
                state_val = str(item.state.value) if hasattr(item.state, 'value') else str(item.state)
                
                if state_val in ["Done", "InProgress"]:
                    status = "done" if state_val == "Done" else "partial"
                
                # Check for stubs linked to this ticket?
                # If we have stubs that ref this ticket ID, downgrade Done to Partial?
                # This is aggressive but "Evidence based".
                related_stubs = [s for s in stubs if s.ticket_ref == item.id]
                if related_stubs and status == "done":
                    status = "partial" # Downgrade due to known stubs
                
                # Evidence collection
                # 1. Look for files mentioned in Worklog (heuristic)
                # 2. Look for files that share the feature name (heuristic)
                evidence = []
                
                # Check worklog for "Created file" or similar patterns?
                # Or just grab the worklog snippet as evidence text?
                if item.worklog:
                    # Just cite the latest worklog entry as evidence of activity
                    evidence.append(f"Worklog: {item.worklog[-1]}")
                
                caps.append(CapabilityEvidence(
                    area="backlog", # Generic area for now, could parse from title/tags
                    feature=item.title,
                    status=status,
                    evidence_refs=evidence,
                    item_id=item.id
                ))
            except Exception:
                continue
                
    except Exception as e:
        # If store loading fails, return empty
        return []
    
    # Sort caps by item_id
    caps.sort(key=lambda x: x.item_id if x.item_id else "")
    return caps


def collect_health(product: Optional[str], backlog_root: Optional[Path]) -> List[HealthCheck]:
    """
    Collect health status.
    CURRENTLY A STUB: Returns placeholder data.
    """
    # TODO: Integrate with doctor commands
    return [
        HealthCheck(
            name="prerequisites",
            passed=True,
            message="Python prerequisites check skipped (stub)"
        )
    ]


def generate_pack(
    *,
    scope: str,
    root_path: Path,
    product: Optional[str] = None
) -> EvidencePack:
    """
    Generate the full evidence pack.
    """
    meta = SnapshotMeta(
        scope=scope,
        vcs=get_vcs_meta(root_path),
    )

    stubs_scan_root = root_path
    if product:
        # Product-scoped snapshots should not scan the whole monorepo.
        skill_root = root_path / "skills" / product
        backlog_product_root = root_path / "_kano" / "backlog" / "products" / product

        if skill_root.exists():
            stubs_scan_root = skill_root
        elif backlog_product_root.exists():
            stubs_scan_root = backlog_product_root
    else:
        # Repo-scoped snapshots scan the repository source root if present.
        repo_src = root_path / "src"
        if repo_src.exists():
            stubs_scan_root = repo_src

    stubs = collect_stubs(stubs_scan_root)
    
    # Resolve product root for capability collection
    caps = []
    if product:
        # Try to resolve product path relative to root
        # root_path is likely the repo root (cwd)
        possible_product_root = root_path / "_kano" / "backlog" / "products" / product
        if possible_product_root.exists():
            caps = collect_capabilities(possible_product_root, stubs)
    elif scope == "repo":
        # Maybe collect all? For now skip to keep fast.
        pass

    health = collect_health(product, root_path)
    
    return EvidencePack(
        meta=meta,
        stub_inventory=stubs,
        capabilities=caps,
        health=health
    )
