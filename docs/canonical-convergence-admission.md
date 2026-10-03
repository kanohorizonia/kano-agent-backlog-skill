# Canonical backlog convergence admission

Use one canonical mutable backlog checkout. Independent clones have separate
Git common-directory ID reservations and can allocate the same display ID to
different immutable UIDs. Local reservation guarantees do not extend across
clones. Inspect identities before convergence or allocation can obscure the
incoming collision.

From the canonical checkout, select one incoming ref already available in its
Git object database, then run:

```bash
kob workitem inspect-incoming-identities --product <product> \
  --incoming-ref refs/remotes/origin/main --format json
```

The selected product resolves the canonical backlog root. The inspector covers
its single-product `items/` and all `products/<product>/items/` directories,
including current uncommitted and untracked items. Incoming items come only
from the selected committed tree; incoming uncommitted data is excluded.
Adjacent `.index.md` views and non-item Markdown are excluded.

The read-only check pins `incoming_tree` before enumerating immutable blob OIDs.
It does not fetch, change branches, create indexes, sync sequences, allocate,
repair, rewrite Worklogs, or merge. The operator must establish remote freshness
through the governed Git workflow and select the intended ref explicitly.
No distributed allocation or global freshness guarantee is implied.
Git reads disable optional locks, lazy fetching and replacement objects. Missing
objects in partial clones fail instead of causing an implicit fetch. Git must
support these global options; unsupported Git versions fail explicitly.

Exit status 0 admits the inspected identities, 3 rejects an identity collision,
and other nonzero statuses report an invalid/missing ref or incomplete scan.
For a rejected display ID, JSON and plain output include every observed side,
UID, product and repository-relative path. The same UID on both sides is
ordinary content/path drift and does not cause identity rejection. Identity
admission does not resolve content conflicts or authorize convergence.

Default bounds are 16384 items and 65536 entries per side, 1 MiB per item,
64 MiB total retained input and a 30-second aggregate inspection deadline.
Use `--max-items`, `--max-tree-entries`, `--max-item-bytes`,
`--max-total-bytes` and `--timeout-ms` for explicit bounded limits. Incomplete
or oversized scans fail closed; they never report admission.

When a collision rejects admission, stop convergence. A human must determine
which immutable UID owns each item and review a UID-selected repair preview:

```bash
kob workitem remap-id <UID> --to <new-display-ID> \
  --agent <agent> --product <product> --format json
```

Repair application remains an explicit separate human-controlled step.
Ambiguous bare-ID backlinks must be reviewed; never automatically rewrite them
to either identity. Preserve append-only Worklogs. After reviewed repair and
publication, rerun inspection against the newly selected incoming tree, then
perform the usual KOG convergence admission and exact-path review. Never use a
production backlog worktree as a repair or convergence sandbox.
