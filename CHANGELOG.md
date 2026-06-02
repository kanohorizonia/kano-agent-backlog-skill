# Changelog

All notable changes to `kano-agent-backlog-skill` will be documented in this file.

This project uses Git tags as releases: `vX.Y.Z`.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Current OSS release target: `0.0.3`.
Latest released OSS version: `0.0.2`.

`0.1.0` references below are future planning only unless called out elsewhere as internal or experimental.

## [0.1.0] - Future / internal planning

### Overview

**Future alpha milestone** - Planned transition from pre-alpha development tool to a broader distributable package milestone. These notes are retained as forward-looking planning, not as a released or currently tagged OSS version.

**Status**: Future / internal planning. API and packaging details may change before a `0.1.0` release is actually prepared.

### Added

#### Package Distribution
- **PyPI Distribution**: Package now available via `pip install kano-agent-backlog-skill`
- **Console Script**: `kano-backlog` command automatically installed and available in PATH
- **Python 3.8+ Support**: Compatible with Python 3.8, 3.9, 3.10, 3.11, and 3.12
- **Modern Packaging**: Uses `pyproject.toml` (PEP 621) with proper metadata and classifiers
- **Optional Dependencies**: Organized extras groups ([dev], [vector]) for flexible installation

#### Documentation
- **Quick Start Guide**: 5-10 minute walkthrough covering installation and first backlog creation
- **Installation Guide**: System requirements, virtual environment setup, and troubleshooting
- **Configuration Guide**: Profiles, layered config, environment variables, and multi-product setup
- **Usage Examples**: Copy-paste commands for common workflows
- **CONTRIBUTING.md**: Development setup, code style guidelines, testing approach, and release process
- **README.md**: Project overview with key features, installation command, and minimal usage example

#### Core Features (Validated for Beta)
- **Structured Work Items**: Hierarchical backlog (Epic → Feature → User Story → Task/Bug) with frontmatter metadata
- **Ready Gate Validation**: Enforce required fields (Context, Goal, Approach, Acceptance Criteria, Risks/Dependencies) before Task/Bug can move to Ready state
- **Append-Only Worklog**: Auditable decision trail with timestamps and agent identity
- **Architecture Decision Records (ADRs)**: Capture significant technical decisions with trade-offs and consequences
- **State Management**: Validated state transitions with automatic Worklog updates
- **Multi-Product Support**: Isolated product data with independent ID sequences
- **ID Assignment**: Unique IDs following pattern `{PRODUCT}-{TYPE}-{SEQUENCE}` with collision-free incrementing
- **Doctor Command**: Environment validation with actionable recommendations for Python version, SQLite, permissions, and optional dependencies

#### Developer Tools
- **Version Management**: Single source of truth in `VERSION` file, accessible programmatically
- **Build System**: Modern Python packaging with `python -m build` producing .tar.gz and .whl
- **Release Checklist**: Documented process covering pre-release validation, build, test installation, PyPI upload, and post-release verification

### Changed
- **Package Structure**: Reorganized to follow standard Python packaging conventions
- **Entry Point**: CLI now installed as proper console script via setuptools entry_points
- **Dependency Management**: Runtime dependencies use version ranges (not pinned) for flexibility
- **Documentation Structure**: Consolidated user-facing docs in `docs/` directory

### Known Limitations

#### Alpha Status
- **API Stability**: Command-line interface and configuration format will change as we refine the design
- **Breaking Changes**: Pre-1.0 releases (0.x.y) will introduce breaking changes without major version bump
- **Testing**: Core functionality tested but edge cases may not be fully covered

#### Feature Maturity
- **Vector Search**: Experimental - SQLite indexing and vector embeddings are functional but may have performance issues with large backlogs
- **Windows Support**: Core functionality works, but some path handling edge cases may exist
- **Multi-User Workflows**: Designed for single-user or small team use; concurrent writes may cause conflicts

#### Documentation Gaps
- **Advanced Workflows**: Some advanced use cases (complex state transitions, custom templates) are not yet fully documented
- **Migration Guides**: No automated migration from pre-0.1.0 backlog structures
- **Troubleshooting**: Limited troubleshooting documentation for edge cases

#### Testing Coverage
- **Property-Based Tests**: Core properties validated, but some edge cases may not be covered
- **Integration Tests**: Basic workflows tested, but complex multi-product scenarios need more coverage
- **Platform Testing**: Primarily tested on Linux and macOS; Windows testing is limited

#### Performance
- **Large Backlogs**: Performance with 1000+ items not extensively tested
- **View Generation**: Dashboard generation may be slow for large backlogs without SQLite indexing
- **Search**: Vector search performance depends on embedding model and hardware

### Security
- **Audit Logging**: Tool invocations logged with agent identity and timestamp
- **Secret Redaction**: Automatic redaction of sensitive data in audit logs
- **Log Rotation**: Audit logs automatically rotated to prevent unbounded growth

### Upgrade Notes
- **First Release**: No upgrade path needed - this is the initial beta release
- **Future Upgrades**: Breaking changes in 0.x.y releases will be documented in CHANGELOG

### Feedback Welcome
This is an alpha release. Please report issues, suggest improvements, or share your use cases:
- **GitHub Issues**: [Report bugs or request features](https://github.com/kanohorizonia/kano-agent-backlog-skill/issues)
- **Discussions**: Use the repository issue tracker until a public discussions flow is published.

---

## [0.0.3] - Unreleased

### Added
- Effective config artifacts (stable vs runtime) written to deterministic paths under `.kano/cache/`.
- Gemini embedding provider support (google-genai) with a profile for `gemini-embedding-001`.
- CLI env auto-load for local development: `env/local.secrets.env` by default, override via `--env-file` / `KANO_ENV_FILE`.

### Changed
- Profile resolution precedence:
  - Explicit path inputs are honored.
  - Shorthand prefers `.kano/backlog_config/<ref>.toml`, with fallback to `<repo_root>/<ref>.toml`.
- `cache.root` handling: relative paths are resolved relative to repo root (not CWD).
- Release check Phase2 is stabilized by aligning tests with the project-level config model.

### Fixed
- SQLite vector query path alignment: query now resolves the same DB path + `embedding_space_id` as the index builder.
- Repo corpus indexing/search no longer requires a fully initialized backlog/project config to function.

### Documentation
- Release notes: `docs/releases/0.0.3.md`.
- Public OSS-readiness cleanup across README, package metadata, docs links, and GitHub Pages configuration.

## [0.0.2] - 2026-01-19

### Added
- Topic templates/archetypes with variable substitution and CLI integration.
- Topic cross-references (`related_topics`) with bidirectional linking.
- Topic snapshots (create/list/restore/cleanup) for checkpointing.
- Topic merge/split operations with dry-run support and history preservation.

### Changed
- Topic distillation renders human-readable seed item listings (ID/title/type/state) while keeping UID mapping in HTML comments.
- Artifact attachment resolves items in product layout (`_kano/backlog/products/<product>/items/...`) when `--backlog-root-override` is used with `--product`.

### Documentation
- Release notes for GitHub Releases: `docs/releases/0.0.2.md`.

## [0.0.1] - 2026-01-15

### Added
- Optional SQLite index layer (rebuildable) to accelerate reads and view generation.
- DBIndex vs NoDBIndex demo dashboards under `_kano/backlog/views/_demo/`.
- Demo tool for recent/iteration focus views (`_kano/backlog/tools/generate_focus_view.py`).
- First-run bootstrap (`scripts/backlog/bootstrap_init_project.py`) + templates to enable the backlog system in a repo.
- `views.auto_refresh` config flag (default: true) to keep dashboards up to date automatically.

### Documentation
- Release notes for GitHub Releases: `docs/releases/0.0.1.md`.

### Changed
- Unified generated dashboards to prefer SQLite when enabled/available and fall back to file scan.
- Kept `scripts/backlog/view_generate_demo.py` self-contained; demo repo tool is a thin wrapper.
- Mutating scripts auto-refresh dashboards by default; `scripts/fs/*` now also require `--agent` for auditability.

### Fixed
- `query_sqlite_index.py --sql` validation (SELECT/WITH detection).


### Added
- Local-first backlog structure under `_kano/backlog/` (items, decisions/ADRs, views).
- Work item scripts: create items, validate Ready gate, update state with append-only Worklog.
- Audit logging for tool invocations with redaction and rotation.
- Plain Markdown dashboards + Obsidian Dataview/Bases demo views.
- Config system under `_kano/backlog/_config/config.json`.

### Changed
- Enforced explicit `--agent` for Worklog-writing scripts and auditability.

### Security
- Secret redaction and log rotation defaults for audit logs.
