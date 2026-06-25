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
- [Release channels](release-channels.md)
- [Release Record schema](design/release-record-schema.md)
- [Releases directory contract](design/releases-directory-contract.md)
- [Native CLI direction](design/native-cli-direction.md)
- [Backboard information architecture](design/backboard-information-architecture.md)
- [Backboard Review Inbox model](design/backboard-review-inbox-model.md)
- [Backboard partial UI boundary](design/backboard-partial-ui-boundary.md)
- [KOB Webview technology boundary](design/webview-technology-boundary.md)
- [Actor alias and assignment policy](design/actor-alias-and-assignment-policy.md)
- [Canonical backlog taxonomy](design/canonical-backlog-taxonomy.md)
- [Project model decision](design/project-model-decision.md)
- [Hierarchy validation matrix](design/hierarchy-validation-matrix.md)
- [Product Map projection schema](design/product-map-projection-schema.md)
- [ADR lifecycle metadata](design/adr-lifecycle-metadata.md)
- [Feature evolution event model](design/feature-evolution-event-model.md)
- [Design-history graph edge semantics](design/design-history-graph-edge-semantics.md)
- [Version Goal Ledger schema](design/version-goal-ledger-schema.md)
- [Evidence quality classification model](design/evidence-quality-classification-model.md)
- [Context recovery summary contract](design/context-recovery-summary-contract.md)

## Release notes

- [0.0.4 release notes](releases/0.0.4.md)
- [0.0.3 release notes](releases/0.0.3.md)
- [CHANGELOG](../CHANGELOG.md)

## References

- [Repository reference](../REFERENCE.md)
- [Workflow reference](../references/workflow.md)
- [Schema reference](../references/schema.md)
- [Product Map schema fixture](../references/product-map-projection.schema.json)
- [ADR lifecycle schema fixture](../references/adr-lifecycle-metadata.schema.json)
- [Feature evolution schema fixture](../references/feature-evolution-event.schema.json)
- [Design-history graph schema fixture](../references/design-history-graph.schema.json)
- [Version Goal Ledger schema fixture](../references/version-goal-ledger.schema.json)
- [Release Record schema fixture](../references/release-record.schema.json)
- [Release Record v0.2.0 fixture](../references/release-record-v0.2.0.fixture.json)
- [Release notes evidence bundle schema fixture](../references/release-notes-evidence-bundle.schema.json)
- [Release notes evidence bundle fixture](../references/release-notes-evidence-bundle.fixture.json)
- [Releases directory schema fixture](../references/releases-directory-contract.schema.json)
- [Releases directory v0.2.0 fixture](../references/releases-directory-contract-v0.2.0.fixture.json)
- [Evidence quality schema fixture](../references/evidence-quality-classification.schema.json)
- [Context recovery schema fixture](../references/context-recovery-summary.schema.json)

## Repo-local launcher behavior

For a cloned repository, use `bash scripts/kob ...`.

- It requires a native `kano-backlog` binary built locally under `src/cpp/out`.
- If no native binary is available, build one with `pixi run build-dev`.

The former Python runtime and pytest oracle have been removed. Verification is native C++ only.
