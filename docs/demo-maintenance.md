# Demo Maintenance Follow Up

The demo repository referenced by this project is not available in the local workspace used for this documentation pass. This page records concrete follow up work for maintainers. It does not claim that the demo repository was updated.

## Goal

Keep the public demo aligned with the OSS facing documentation and current pre-1.0 behavior of `kano-agent-backlog-skill`.

## Required follow up checklist

- confirm the demo repository still installs the current published package or local development build
- confirm the demo README describes the project as pre-1.0 or experimental
- confirm the demo uses local first markdown artifacts under `_kano/backlog/`
- confirm the demo shows a task, bug, or issue passing the Ready Gate with Context, Goal, Approach, Acceptance Criteria, and Risks / Dependencies
- confirm the demo includes at least one ADR and one worklog example
- confirm the demo shows when to use a workset for focused execution
- confirm the demo shows when to use a topic for multi item or cross session context
- confirm the demo does not treat optional search as a required baseline feature
- confirm derived cache and log paths are ignored in version control
- confirm public links in the demo README point at the current docs pages in this repository

## Suggested verification steps

1. open the demo repo and run the install and doctor flow
2. initialize or inspect the backlog scaffold
3. compare the demo commands and screenshots against:
   - [quick-start.md](quick-start.md)
   - [installation.md](installation.md)
   - [configuration.md](configuration.md)
   - [../references/schema.md](../references/schema.md)
   - [../references/workflow.md](../references/workflow.md)
4. record any drift as backlog items or doc fixes

## What to avoid

- don't publish the demo as if it proves stable APIs
- don't show experimental search as the default path for first time users
- don't claim the demo repo reflects current behavior until the checklist above is completed

## Outcome expected after follow up

Once maintainers complete this checklist, the demo should reinforce the same public thesis as the main repository: a local first, auditable, reviewable workflow memory layer for agent assisted development.
