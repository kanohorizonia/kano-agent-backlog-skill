# Documentation Deployment Scripts

This directory contains scripts for building and staging the official documentation website with Quartz, MkDocs, and GitHub Pages.

## Architecture

### Workspace Structure

```
_ws/
├── src/           # Source repositories
│   ├── demo/      # Demo repo clone
│   ├── quartz/    # Quartz v4.4.0 engine
│   └── skill/     # Skill repo working tree snapshot
├── build/         # Generated artifacts
│   ├── content/   # Prepared content for Quartz and MkDocs helpers
│   ├── staged/    # Final static site artifact for GitHub Pages
│   └── public/    # Reserved for local Quartz output if needed
└── deploy/        # Optional local deploy target
    └── gh-pages/  # Local gh-pages working tree
```

### Pipeline Flow

```
Source Repos → Content Preparation → Quartz Build → MkDocs Integration → Optional Local Deploy Prep
     ↓               ↓                    ↓                 ↓                        ↓
  _ws/src/    →  _ws/build/content  →  _ws/build/staged → _ws/build/staged  →  _ws/deploy/gh-pages
```

## File Structure

### Main Scripts
| Script | Purpose | Dependencies |
|--------|---------|-------------|
| `01-setup-workspace.sh` | Clone the demo repo, clone Quartz, and snapshot the local skill repo | Git, tar |
| `02-prepare-content.sh` | Prepare native CLI/API overview content | 01 |
| `03-build-site.sh` | Build static site with Quartz | 02, Node.js 22 |
| `04-deploy-mkdocs.sh` | Write native API placeholder into the staged site | 03 |
| `05-deploy-quartz.sh` | Copy the staged site into a local gh-pages working tree | 04 |
| `06-push-remote.sh` | Commit and push the gh-pages working tree, explicit use only | 05 |
| `build-and-deploy.sh` | Main entrypoint, builds and stages output by default | 01 to 04 |

### Configuration Files
| File | Purpose |
|------|---------|
| `config/build.json` | Build parameters, repository URLs, deployment metadata |
| `config/quartz.config.ts` | Quartz theme and plugin configuration |
| `config/mkdocs.yml` | Retired MkDocs placeholder retained for legacy script arguments |
| `config/publish.config.yml` | Historical content mapping reference |

### Helper Tools
| File | Purpose |
|------|---------|
| `help/config-paths.sh` | Shared config path resolution |

## Content Publishing System

### YAML Based Configuration

Content publishing is controlled by `config/publish.config.yml`.

It defines:

- which files are copied from the demo and skill repos
- which sections appear in navigation
- which file becomes the site landing page
- how CLI and API sections are stitched into the final site

### Published Sections

The current config publishes these top level sections:

- Demo
- Skill
- Guides
- Maintainer Automation
- Architecture Decisions
- Examples
- References
- CLI
- API
- Releases

The landing page comes from `skill/docs/index.md`, not from an auto generated placeholder.

## Usage

### Quick Start

```bash
./src/shell/docs/build-and-deploy.sh
```

This builds the site and leaves the GitHub Pages artifact in `_ws/build/staged`.

### CI Mode

```bash
./src/shell/docs/build-and-deploy.sh --ci
```

This uses the same build flow and still stops at the staged artifact.

### Optional Local Deploy Prep

```bash
./src/shell/docs/build-and-deploy.sh --prep-deploy
```

This additionally populates `_ws/deploy/gh-pages` without pushing.

### Explicit Remote Publish

```bash
./src/shell/docs/build-and-deploy.sh --prep-deploy --push
```

Only run this if you intentionally want the legacy git based publish step.

### Step by Step Execution

```bash
./src/shell/docs/01-setup-workspace.sh
./src/shell/docs/02-prepare-content.sh
./src/shell/docs/03-build-site.sh
./src/shell/docs/04-deploy-mkdocs.sh
./src/shell/docs/05-deploy-quartz.sh
./src/shell/docs/06-push-remote.sh
```

## Prerequisites

- **Git** for repository cloning
- **Node.js 22** for Quartz build
- **Native kano-backlog binary** built with `pixi run build-dev`
- **GitHub push rights** only if you explicitly run the remote publish step

## Configuration

### Build Configuration

`config/build.json` tracks:

- Quartz version and source
- demo repo URL
- skill repo URL
- deployed site URL
- branch metadata for the optional git based publish flow

Current canonical site URL:

- `https://agentskill-backlog.kanohorizonia.com/`

### Content Mapping

`config/publish.config.yml` controls:

- content copied from `demo/` and `skill/`
- generated section indexes
- home page source selection
  - native API docs site URL metadata

## GitHub Pages Integration

The repository workflow uses the official Pages artifact model:

1. Build the site into `_ws/build/staged`
2. Upload it with `actions/upload-pages-artifact`
3. Deploy it with `actions/deploy-pages`

Custom domain handling should still be configured in repository Pages settings, but the current branch-based `gh-pages` publish flow also restores a `CNAME` file from `config/build.json` so the deployed branch keeps the expected hostname.

## Public CI Report Slots

The deploy step always reserves stable public report entrypoints under:

- `reports/latest/test-report/index.html`
- `reports/latest/coverage-report/index.html`
- `reports/latest/public-report-slots.json`

When Jenkins stages public-safe report HTML under `KANO_SITE_STAGING_ROOT/reports/latest`, that directory is copied into the `gh-pages` working tree. When no staged reports exist, the deploy step writes placeholders so public links do not 404.

Coverage source visibility follows `KANO_PUBLIC_COVERAGE_SOURCE_POLICY`:

- `source-included` is allowed for this open-source project.
- `source-free` is the fail-closed default for private or unknown-source projects.

## Troubleshooting

### Workspace not found

Run:

```bash
./src/shell/docs/01-setup-workspace.sh
```

### YAML config not found

Make sure `src/shell/docs/config/publish.config.yml` exists in the skill repo checkout.

### Native docs pipeline

The docs pipeline no longer requires Python, MkDocs, mkdocstrings, or YAML helper scripts. It builds the Quartz site and writes a native API placeholder for the supported executable contract.

## Local Testing

1. Run `build-and-deploy.sh`
2. Serve `_ws/build/staged`
3. Open the local server in your browser

Example:

Use any static file server for `_ws/build/staged`.

## Security Notes

- The official GitHub Pages workflow uses the repository provided Pages token flow
- Local builds do not push by default
- The optional git based publish step is still available for maintainers who need it
