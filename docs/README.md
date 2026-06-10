# Documentation index for repo-local browsing

This directory serves two audiences:

- **GitHub / repo-local readers** browsing markdown files directly in the repository
- **GitHub Pages** generation, where `docs/index.md` is the published site home page source

If you are reading the repository directly, start here instead of `docs/index.md`.

## Core guides

- [Quick start](quick-start.md)
- [Agent quick start](agent-quick-start.md)
- [Installation](installation.md)
- [Configuration](configuration.md)
- [Maintainer automation](maintainer-automation.md)
- [Codex for OSS](codex-for-oss.md)
- [Demo maintenance follow-up](demo-maintenance.md)
- [Version policy](version-policy.md)
- [Native CLI direction](design/native-cli-direction.md)

## Release notes

- [0.0.3 release notes](releases/0.0.3.md)
- [CHANGELOG](../CHANGELOG.md)

## References

- [Repository reference](../REFERENCE.md)
- [Workflow reference](../references/workflow.md)
- [Schema reference](../references/schema.md)

## Repo-local launcher behavior

For a cloned repository, use `bash scripts/kob ...`.

- It requires a native `kano-backlog` binary built locally under `src/cpp/out`.
- If no native binary is available, build one with `pixi run build-dev`.

The former Python runtime and pytest oracle have been removed. Verification is native C++ only.
