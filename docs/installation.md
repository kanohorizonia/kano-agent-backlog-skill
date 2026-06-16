# Installation

Installable public artifacts are published on GitHub Releases. Repo-local
developer execution uses the native C++ CLI built from a clone.

## Release downloads

Use the [latest GitHub Release](https://github.com/kanohorizonia/kano-agent-backlog-skill/releases/latest)
for normal installation. For the 0.0.4 line, the expected release tag is
[`v0.0.4`](https://github.com/kanohorizonia/kano-agent-backlog-skill/releases/tag/v0.0.4).

The release page must contain platform artifacts and checksum/index metadata
from the matching Jenkins `Build_CI` source version. If the release page or
assets are missing, that version has not completed the release gate.

Expected artifact naming:

| Platform | Artifact selector |
| --- | --- |
| Windows x64 | `KanoHorizonia.KanoBacklog-windows-x64-*-Release-cli.tar.gz` |
| Windows arm64 | `KanoHorizonia.KanoBacklog-windows-arm64-*-Release-cli.tar.gz` |
| Linux x64 | `KanoHorizonia.KanoBacklog-linux-x64-*-Release-cli.tar.gz` |
| Linux arm64 | `KanoHorizonia.KanoBacklog-linux-arm64-*-Release-cli.tar.gz` |
| macOS x64 | `KanoHorizonia.KanoBacklog-macos-x64-*-Release-cli.tar.gz` |
| macOS arm64 | `KanoHorizonia.KanoBacklog-macos-arm64-*-Release-cli.tar.gz` |

Download the archive matching your platform, verify it with the published
checksum/index metadata, then extract it into your skills directory.

## Manual install from an archive

Linux and macOS:

```bash
mkdir -p "$HOME/.agents/skills/kano-agent-backlog-skill"
tar -xzf KanoHorizonia.KanoBacklog-<platform>-main-<version>-Release-cli.tar.gz \
  -C "$HOME/.agents/skills/kano-agent-backlog-skill" --strip-components 1
export PATH="$HOME/.agents/skills/kano-agent-backlog-skill/scripts:$PATH"
kob --version
kob doctor
```

Windows PowerShell:

```powershell
$SkillRoot = "$HOME\.agents\skills\kano-agent-backlog-skill"
New-Item -ItemType Directory -Force -Path $SkillRoot | Out-Null
tar -xzf .\KanoHorizonia.KanoBacklog-windows-x64-main-<version>-Release-cli.tar.gz -C $SkillRoot --strip-components 1
$env:PATH = "$SkillRoot\scripts;$env:PATH"
kob --version
kob doctor
```

When a release publishes a Windows MSI, prefer the MSI because it installs the
skill under `~/.agents/skills/kano-agent-backlog-skill` and configures the
`scripts` directory on `PATH`. The portable archive remains the baseline
install artifact for release validation.

## Package managers

Package-manager channels are release metadata channels, not substitutes for the
GitHub Release asset gate.

| Channel | Status for 0.0.4 | Intended install command |
| --- | --- | --- |
| winget | Prepared only until winget metadata is published | `winget install --id KanoHorizonia.KanoBacklog -e` |
| Homebrew | Owned Kano tap path only; no `homebrew-core` PR | `brew tap kanohorizonia/kano && brew install kano-backlog` |
| apt | Planned owned apt repository; no public apt repository is live yet | `sudo apt install kano-backlog` after repository setup |

Jenkins `Release_Publish` may generate winget, Homebrew, and apt review
payloads. Public package-manager publication is accepted only after the
corresponding release flag publishes metadata to the configured owned target.

## Quick build (developer checkout)

```bash
# Build release binary (default)
bash scripts/kob self build

# Explicit modes
bash scripts/kob self build release   # same as above
bash scripts/kob self build debug     # debug only when explicitly requested
```

`bash scripts/kob self build` is equivalent to `bash scripts/kob self build release`. Debug requires explicit `debug` argument.

### pixi build (also available)

```bash
pixi run build-dev
```

## Verify the installation

```bash
bash scripts/kob --version
bash scripts/kob doctor
bash scripts/kob self status    # shows binary path, config, mode
bash scripts/kob self doctor    # full health check
```

## Rebuild from scratch

```bash
bash scripts/kob self rebuild         # release (default)
bash scripts/kob self rebuild release # same as above
bash scripts/kob self rebuild debug   # debug only when explicitly requested
```

`self rebuild` clears the canonical preset's obj/bin directories and runs `self build`. Platform presets are aligned:

| Platform | Canonical preset |
| --- | --- |
| Windows | `windows-ninja-msvc` (fallback: `windows-msbuild`) |
| Linux | `linux-ninja-clang` |
| macOS | `macos-ninja-clang` |

## Notes

- `self build` and `self rebuild` only work in a **developer checkout** (requires `VERSION`, `src/cpp/CMakeLists.txt`, `pixi.toml`).
- Packaged installs must use the release/update flow, not `self build`.
- No Python runtime. No `~/.kano` build config. Build contract lives in `src/shell/support/self-build.sh`.
- Binary resolution is always release-first. Override with `KANO_BACKLOG_BINARY=/path/to/binary`.

If you are working from a clone of the skill repository, start with the repo level guidance in these pages:

- [Agent quick start](agent-quick-start.md)
- [Main repository README](https://github.com/kanohorizonia/kano-agent-backlog-skill#quick-start)

For repo-local execution, use `bash scripts/kob`. It requires a built native binary and does not fall back to Python.

For docs maintenance, the local docs pipeline lives under `src/shell/docs/` and builds into `_ws/build/staged` by default.

## Contributor setup

```bash
pixi run build-dev
pixi run quick-test
```

This is the supported path when working on the native executable. `quick-test` is a bounded native smoke lane with explicit CTest timeout diagnostics; use the shared infra `test`/`full-test` lanes, report tasks, and coverage tasks for broader integration evidence. Python package metadata has been retired for this native milestone.

## Platform notes

- Windows users should prefer Git Bash or WSL for the bundled shell scripts.
- macOS and Linux can use the scripts directly.
- The repo also includes `pixi.toml` for native build and cross-platform helper tasks.

## Troubleshooting

- If `kob` cannot find a binary, run `bash scripts/kob self build` (or `pixi run build-dev`).
- Binary resolution is release-first. Set `KANO_BACKLOG_BINARY` to override.
- If docs dependencies are missing, use the native docs scripts under `src/shell/docs/`.
- If tests fail before launching the CLI, use `pixi run quick-test` to exercise the native smoke lane.
