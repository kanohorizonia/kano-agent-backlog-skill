from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Optional
import typer

from kano_backlog_core import __version__

from .util import (
    configure_stdio,
    ensure_core_on_path,
    load_env_file,
    resolve_product_root,
    set_global_config_file,
)

app = typer.Typer(help="kano-backlog: Backlog management CLI (MVP)")


def version_callback(value: bool):
    """Display version information."""
    if value:
        typer.echo(f"kano-backlog version {__version__}")
        raise typer.Exit()


@app.callback()
def _init(
    config_file: Optional[Path] = typer.Option(
        None,
        "--config-file",
        help="Path to project config file (.kano/backlog_config.toml)",
        exists=True,
        file_okay=True,
        dir_okay=False,
        readable=True,
    ),
    env_file: Optional[Path] = typer.Option(
        None,
        "--env-file",
        help="Env file to load (default: env/local.secrets.env)",
        envvar="KANO_ENV_FILE",
    ),
    profile: Optional[str] = typer.Option(
        None,
        "--profile",
        help="Config profile to overlay (loads .kano/backlog_config/<profile>.toml relative to project root)",
    ),
    version: Optional[bool] = typer.Option(
        None,
        "--version",
        help="Show version and exit",
        callback=version_callback,
        is_eager=True,
    ),
):
    configure_stdio()
    ensure_core_on_path()

    if env_file is not None:
        load_env_file(env_file, required=True)
    else:
        load_env_file(Path("env") / "local.secrets.env", required=False)
    
    # Store the config file path globally for use by utility functions
    if config_file:
        set_global_config_file(config_file)

    # Expose profile selection to the core layer without creating a dependency
    # on CLI state (core reads this env var when computing effective config).
    if profile and profile.strip():
        os.environ["KANO_BACKLOG_PROFILE"] = profile.strip()


from .commands import admin as admin_cmd  # noqa: E402
from .commands import workitem as workitem_cmd  # noqa: E402
from .commands import state as state_cmd  # noqa: E402
from .commands import worklog as worklog_cmd  # noqa: E402
from .commands import view as view_cmd  # noqa: E402
from .commands import index as index_cmd  # noqa: E402
from .commands import demo as demo_cmd  # noqa: E402
from .commands import persona as persona_cmd  # noqa: E402
from .commands import sandbox as sandbox_cmd  # noqa: E402
from .commands import validate as validate_cmd  # noqa: E402
from .commands import links as links_cmd  # noqa: E402
from .commands import items as items_cmd  # noqa: E402
from .commands import adr as adr_cmd  # noqa: E402
from .commands import schema as schema_cmd  # noqa: E402
from .commands import meta as meta_cmd  # noqa: E402
from .commands import workset as workset_cmd  # noqa: E402
from .commands import evidence as evidence_cmd  # noqa: E402
from .commands import assumptions as assumptions_cmd  # noqa: E402
from .commands import inspect as inspect_cmd  # noqa: E402
from .commands import topic as topic_cmd  # noqa: E402
from .commands import config_cmd as config_cmd  # noqa: E402
from .commands import snapshot as snapshot_cmd  # noqa: E402
from .commands import changelog as changelog_cmd  # noqa: E402
from .commands import benchmark as benchmark_cmd  # noqa: E402
from .commands import release as release_cmd  # noqa: E402
from .commands import embedding as embedding_cmd  # noqa: E402
from .commands import chunks as chunks_cmd  # noqa: E402
from .commands import search as search_cmd  # noqa: E402
from .commands import tokenizer_cmd as tokenizer_cmd  # noqa: E402
from .commands import orphan as orphan_cmd  # noqa: E402
from .commands.doctor import doctor as doctor_fn  # noqa: E402

app.add_typer(admin_cmd.app, name="admin", help="Administrative and setup commands")
app.add_typer(workitem_cmd.app, name="workitem", help="Work item operations")
app.add_typer(workitem_cmd.app, name="item", help="Work item operations (alias)")
app.add_typer(state_cmd.app, name="state", help="State transitions")
app.add_typer(worklog_cmd.app, name="worklog", help="Worklog operations")
app.add_typer(view_cmd.app, name="view", help="View and dashboard operations")
app.add_typer(snapshot_cmd.app, name="snapshot", help="Snapshot and evidence operations")
app.add_typer(workset_cmd.app, name="workset", help="Workset cache operations")
app.add_typer(evidence_cmd.app, name="evidence", help="Evidence record operations")
app.add_typer(assumptions_cmd.app, name="assumptions", help="Assumptions registry operations")
app.add_typer(inspect_cmd.app, name="inspect", help="Inspector operations")
app.add_typer(topic_cmd.app, name="topic", help="Topic context operations")
app.add_typer(config_cmd.app, name="config", help="Config inspection and validation")
app.add_typer(changelog_cmd.app, name="changelog", help="Changelog generation from backlog")
app.add_typer(benchmark_cmd.app, name="benchmark", help="Deterministic benchmark harness")
app.add_typer(embedding_cmd.app, name="embedding", help="Embedeline operations")
app.add_typer(chunks_cmd.app, name="chunks", help="Canonical chunks DB (FTS5)")
app.add_typer(search_cmd.app, name="search", help="Vector similarity search")
app.add_typer(tokenizer_cmd.app, name="tokenizer", help="Tokenizer adapter configuration, testing, and diagnostics")
app.add_typer(orphan_cmd.app, name="orphan", help="Check for commits without backlog item IDs")
# Nest index, demo, persona, and sandbox under admin group
admin_cmd.app.add_typer(index_cmd.app, name="index", help="Index operations")
admin_cmd.app.add_typer(demo_cmd.app, name="demo", help="Demo data operations")
admin_cmd.app.add_typer(persona_cmd.app, name="persona", help="Persona activity operations")
admin_cmd.app.add_typer(sandbox_cmd.app, name="sandbox", help="Sandbox environment operations")
admin_cmd.app.add_typer(validate_cmd.app, name="validate", help="Backlog validation helpers")
admin_cmd.app.add_typer(links_cmd.app, name="links", help="Link maintenance helpers")
admin_cmd.app.add_typer(items_cmd.app, name="items", help="Item maintenance helpers")
admin_cmd.app.add_typer(adr_cmd.app, name="adr", help="ADR operations")
admin_cmd.app.add_typer(schema_cmd.app, name="schema", help="Schema validation and fixing")
admin_cmd.app.add_typer(meta_cmd.app, name="meta", help="Meta file helpers")
admin_cmd.app.add_typer(release_cmd.app, name="release", help="Release verification workflows")
app.command(name="doctor")(doctor_fn)


def main():
    app()
