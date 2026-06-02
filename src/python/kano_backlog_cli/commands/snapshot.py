"""
snapshot.py - Snapshot command for generating evidence packs.
"""

from __future__ import annotations

import subprocess
import sys
from dataclasses import asdict
from pathlib import Path
from typing import Optional, List

import typer
from rich.console import Console

from kano_backlog_ops import snapshot as snapshot_ops
from kano_backlog_ops.template_engine import TemplateEngine
from kano_backlog_cli.util import ensure_core_on_path, resolve_product_root

app = typer.Typer()
console = Console()


def _collect_cli_remotely() -> List[snapshot_ops.CliCommand]:
    """
    Collect CLI tree by running 'kano-backlog --help' and parsing output.
    This simulates an external audit of the surface.
    """
    # Find the kano-backlog script wrapper or module
    # We try to run the same command that invoked us, or default to standard locations
    cmd = ["bash", "scripts/kob"]
    if not Path("scripts/kob").exists():
        # Fallback to module execution if script not found
        cmd = [sys.executable, "-m", "kano_backlog_cli"]

    try:
        # Run help
        result = subprocess.run(
            cmd + ["--help"], 
            capture_output=True, 
            text=True, 
            check=False,
            encoding='utf-8' # Force utf-8
        )
        if result.returncode != 0:
            console.print(f"[yellow]Warning: Failed to run help for CLI tree: {result.stderr}[/yellow]")
            return []
            
        # Parse logic (simplified for MVP: just top level and known groups)
        # For a full tree we would need recursive parsing. 
        # Here we just capture the raw help text as a single node description for now,
        # or do a shallow parse.
        
        # PROVISIONAL: Just return top-level help as one node to prove connectivity
        return [snapshot_ops.CliCommand(
            name="kano-backlog",
            help="Full CLI Help Output (Recursive parsing TODO)",
            subcommands=[]
        )]
        
    except Exception as e:
        console.print(f"[yellow]Warning: CLI collection failed: {e}[/yellow]")
        return []


def _resolve_output_path(
    scope: str, 
    view: str, 
    format: str, 
    out: Optional[Path], 
    cwd: Path,
) -> Path:
    """Determine final output path."""
    if out:
        return out
        
    # Flattened structure: _kano/backlog/[products/<name>/]views/snapshots/snapshot.<view>.<format>
    stem = f"snapshot.{view}"
    filename = f"{stem}.{format}"
    
    if scope.startswith("product:"):
        product_name = scope.split(":", 1)[1]
        try:
            product_root = resolve_product_root(product_name, start=cwd)
            target_dir = product_root / "views" / "snapshots"
        except SystemExit:
            target_dir = cwd / "snapshots"
    else:
        # Repo scope: store under the default product to avoid legacy root views.
        try:
            product_root = resolve_product_root(None, start=cwd)
            target_dir = product_root / "views" / "snapshots"
        except SystemExit:
            target_dir = cwd / "snapshots"
        
    return target_dir / filename


def _format_meta_block(meta: snapshot_ops.VcsMeta, mode: str) -> str:
    """Render VCS metadata block according to KABSD-FTR-0039."""
    if mode == "none":
        return ""

    lines = [
        "<!-- kano:build",
        f"vcs.provider: {meta.provider}",
        f"vcs.branch: {meta.branch}",
        f"vcs.revno: {meta.revno}",
        f"vcs.hash: {meta.hash}",
    ]

    if mode in ("min", "full"):
        lines.append(f"vcs.dirty: {meta.dirty}")

    lines.append("-->")
    return "\n".join(lines) + "\n\n"


def _validate_meta_mode(meta_mode: str) -> str:
    allowed = {"none", "min", "full"}
    if meta_mode not in allowed:
        raise typer.BadParameter(f"meta-mode must be one of {', '.join(sorted(allowed))}")
    return meta_mode


@app.command(name="create", help="Generate a deterministic snapshot evidence pack.")
def create(
    view: str = typer.Argument(..., help="View to capture: all|stubs|cli|health|capabilities"),
    scope: str = typer.Option("repo", "--scope", help="Scope: repo|product:<name>"),
    format: str = typer.Option("md", "--format", "-f", help="Output format: json|md"),
    write: bool = typer.Option(False, "--write", "-w", help="Write output to file"),
    out: Optional[Path] = typer.Option(None, "--out", "-o", help="Custom output path"),
    meta_mode: str = typer.Option("min", "--meta-mode", help="Metadata block mode: none|min|full"),
):
    """
    Generate a deterministic snapshot evidence pack.
    """
    cwd = Path.cwd()
    ensure_core_on_path()
    meta_mode = _validate_meta_mode(meta_mode)
    
    # Parse scope
    product_name = None
    if scope.startswith("product:"):
        product_name = scope.split(":", 1)[1]
        
    # Generate pack
    console.print(f"[bold blue]Snapshotting {scope} (view={view})...[/bold blue]")
    
    pack = snapshot_ops.generate_pack(
        scope=scope,
        root_path=cwd,
        product=product_name
    )

    meta_block = _format_meta_block(pack.meta.vcs, meta_mode)
    
    # Fill in CLI tree if requested (expensive/external)
    if view in ["all", "cli"]:
        pack.cli_tree = _collect_cli_remotely()
        
    # Format output
    output_content = ""
    if format == "json":
        output_content = pack.to_json()
    else:
        # Markdown rendering
        output_content = meta_block
        output_content += f"# Snapshot Report: {scope}\n\n"
        output_content += f"**Scope:** {pack.meta.scope}\n"
        output_content += f"**VCS Branch:** {pack.meta.vcs.branch}\n"
        output_content += f"**VCS RevNo:** {pack.meta.vcs.revno}\n"
        output_content += f"**VCS Hash:** {pack.meta.vcs.hash}\n"
        output_content += f"**VCS Dirty:** {pack.meta.vcs.dirty}\n\n"
        
        if view in ["all", "capabilities"]:
            output_content += "## Capabilities\n\n"
            for cap in pack.capabilities:
                output_content += f"- **{cap.area}.{cap.feature}**: {cap.status}\n"
        
        if view in ["all", "stubs"]:
            output_content += "\n## Stubs & TODOs\n\n"
            output_content += f"Found {len(pack.stub_inventory)} items.\n"
            # Limit listing for brevity in console, full in file
            for stub in pack.stub_inventory[:20]:
                output_content += f"- [{stub.type}] {stub.file}:{stub.line} - {stub.message}\n"
            if len(pack.stub_inventory) > 20:
                output_content += f"... and {len(pack.stub_inventory)-20} more.\n"

        if view in ["all", "health"]:
             output_content += "\n## Health Checks\n\n"
             for h in pack.health:
                 icon = "✅" if h.passed else "❌"
                 output_content += f"- {icon} {h.name}: {h.message}\n"
                 
        if view in ["all", "cli"]:
            output_content += "\n## CLI Surface\n\n"
            if pack.cli_tree:
                output_content += f"Command: {pack.cli_tree[0].name}\n"
                # TODO recursive print
            else:
                output_content += "No CLI tree collected.\n"

    # Display or Write
    if write:
        # Determine path
        target_path = _resolve_output_path(scope, view, format, out, cwd)
        target_path.parent.mkdir(parents=True, exist_ok=True)
        target_path.write_text(output_content, encoding="utf-8")
        console.print(f"[green]Snapshot written to: {target_path}[/green]")
    else:
        console.print(output_content)


@app.command()
def report(
    persona: str = typer.Argument(..., help="Target persona: developer|pm|qa"),
    scope: str = typer.Option("repo", "--scope", help="Scope: repo|product:<name>"),
    write: bool = typer.Option(False, "--write", "-w", help="Write report to file"),
    out: Optional[Path] = typer.Option(None, "--out", "-o", help="Custom output path"),
    meta_mode: str = typer.Option("min", "--meta-mode", help="Metadata block mode: none|min|full"),
):
    """
    Generate a personified report from a fresh snapshot using a template.
    """
    cwd = Path.cwd()
    ensure_core_on_path()
    meta_mode = _validate_meta_mode(meta_mode)
    
    console.print(f"[bold blue]Generating {persona} report for {scope}...[/bold blue]")
    
    # 1. Generate snapshot (evidence)
    product_name = None
    if scope.startswith("product:"):
        product_name = scope.split(":", 1)[1]
    
    pack = snapshot_ops.generate_pack(scope=scope, root_path=cwd, product=product_name)
    
    # If using 'all' info in report
    pack.cli_tree = _collect_cli_remotely()
    
    # 2. Load template
    # Assumption: templates located in skills directory
    template_name = f"snapshot_report_{persona}.md"
    skill_root = Path("skills/kano-agent-backlog-skill")
    template_path = skill_root / "templates" / template_name
    
    if not template_path.exists():
        console.print(f"[red]Error: Template {template_name} not found in {skill_root}/templates[/red]")
        raise typer.Exit(1)
        
    template_content = template_path.read_text(encoding="utf-8")
    
    # 3. Render
    engine = TemplateEngine()
    context = asdict(pack) # Flatten evidence pack to dict
    # Templates expect {{scope}}; provide an alias to meta.scope for convenience.
    context.setdefault("scope", pack.meta.scope)
    rendered = engine.render(template_content, context)
    meta_block = _format_meta_block(pack.meta.vcs, meta_mode)
    rendered = f"{meta_block}{rendered}" if meta_block else rendered
    
    # 4. Write or Print
    if write:
        target_path = _resolve_output_path(scope, f"report_{persona}", "md", out, cwd)
        target_path.parent.mkdir(parents=True, exist_ok=True)
        target_path.write_text(rendered, encoding="utf-8")
        console.print(f"[green]Report written to: {target_path}[/green]")
    else:
        console.print(rendered)
