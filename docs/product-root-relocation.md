# Guarded Product Root Relocation

`kob migration relocate-product` moves one registered product root into the
canonical `products/<slug>` location of the shared backlog. It is intended for
identity-preserving convergence of an external product root, not item remapping
or cross-product migration.

## Safety Contract

- `plan` is read-only and produces a deterministic SHA-256 over the config
  revision, canonical byte manifest, item IDs/UIDs, structured-ref diagnostics,
  registry collision scan, durable sequence state, and destination path digest.
- `apply` requires both the exact reviewed `--plan-hash` and `--confirm`.
- The destination must be the registered slug's canonical shared root and must
  be absent or empty. Symlinks, prefix/identity collisions, raw filesystem
  navigation refs, active SQLite WAL/SHM files, stale config/source bytes, and
  non-empty targets fail closed.
- Canonical item, Worklog, receipt, artifact, ADR, and evidence bytes are copied
  without rewriting IDs, UIDs, history, hierarchy, or refs.
- Registered generated views, disposable caches, and metadata rows are not
  copied as authority. Hand-authored views are preserved byte for byte. The target
  product index is rebuilt from canonical files. Durable ID sequences and
  reservations are exported separately and restored into the fresh index.
- The cross-volume-safe transaction copies into a destination-volume stage,
  verifies every canonical hash, publishes the target on that volume, commits
  the config atomically, verifies canonical bytes, rebuilds the product index
  and registered generated views, validates a clean canonical rebuild, and only then renames the
  source to a verified rollback root on the source volume.
- The source rollback root is retained. This command never deletes the source
  before config, target, identity, reference, and index postconditions pass.

Public JSON uses bounded refs such as
`product:<slug>:configured-root`,
`product:<slug>/items/...`,
`project-config:.kano/backlog_config.toml`, and
`product-cache:<slug>/index/backlog.db`. Raw absolute paths are confined to the
local recovery journal and are not emitted as navigation refs or receipts.

## Register an Existing External Root First

Relocation requires the external product to exist in the exact shared registry
at `<shared-backlog-root>/.kano/backlog_config.toml`. Register an unregistered
root before planning relocation:

```bash
kob migration register-product plan \
  --product <slug> \
  --product-name <display-name> \
  --prefix <PREFIX> \
  --external-root <absolute-existing-product-root> \
  --backlog-root <absolute-shared-backlog-root>

kob migration register-product apply \
  --product <slug> \
  --product-name <display-name> \
  --prefix <PREFIX> \
  --external-root <absolute-existing-product-root> \
  --backlog-root <absolute-shared-backlog-root> \
  --plan-hash <sha256> \
  --confirm \
  --agent <actor>
```

Registration is config-only. It appends one product block atomically while
leaving the external root and the absent canonical destination untouched. It
does not run `admin init`, create product scaffolding, relocate files, or expose
manual rollback or unregister commands. Registration evidence is published as
one immutable directory before the shared config is replaced. Cooperating
registration writers serialize that replacement through
`.kano/product-registration.config.lock`, use a unique same-directory temporary
file, and atomically rename the reviewed bytes into place.

Recovery is intentionally exact and bounded. Tests interrupt every publication
phase and inject each post-publication failure: an exact-hash retry reclaims only
validated same-plan staging, resumes from the recorded config bytes, returns an
already-applied terminal receipt when the reviewed bytes are present, restores
the exact prior bytes when a safe rollback is required, and fails closed on a
third config state, changed source, excessive inventory, or tampered evidence.
Apply and recovery actors remain distinct in durable attempt history. Recovery
is performed only by repeating the same confirmed `apply`; `status` and
`verify` remain read-only.

The registration lock is a cooperative protocol, not a claim of global
linearizability for every historical config writer. Any legacy or unrelated
tool that edits `.kano/backlog_config.toml` without taking this lock must be
quiesced; schedule registration inside an exclusive operator window whenever
such writers may still run.

The operator order is deliberate:

1. `register-product` establishes the shared config authority.
2. `relocate-product` moves the registered root to `products/<slug>` when
   convergence is desired.
3. Ark product binding happens only after registration and any planned
   relocation have verified successfully.

## Workflow

Build and review a no-write plan:

```bash
kob migration relocate-product plan \
  --product <slug> \
  --backlog-root <shared-backlog-root>
```

`--destination-root` is optional. When supplied it must resolve exactly to
`<shared-backlog-root>/products/<slug>`. Use `--expected-source-revision` to
bind an external approval or handoff to a previously observed canonical
snapshot.

Apply the exact plan:

```bash
kob migration relocate-product apply \
  --product <slug> \
  --backlog-root <shared-backlog-root> \
  --plan-hash <sha256> \
  --confirm
```

Inspect or verify one persisted transaction:

```bash
kob migration relocate-product status <sha256> \
  --backlog-root <shared-backlog-root>

kob migration relocate-product verify <sha256> \
  --backlog-root <shared-backlog-root>
```

Recover an interrupted or applied transaction:

```bash
kob migration relocate-product rollback <sha256> \
  --backlog-root <shared-backlog-root> \
  --confirm
```

Rollback refuses to remove a target with unexpected canonical files or changed
hashes. Resolve that drift explicitly instead of deleting or rebuilding the
backlog.

## Scope Boundaries

This command does not remap Display IDs or UIDs, rewrite historical Worklog
text, merge a non-empty destination, migrate multiple products, copy derived
views as authority, expose raw filesystem navigation refs, or mutate a
production product without an exact reviewed plan and confirmation.

## Generated Surface Ownership and Registration

The current native producer inventory is deliberately small:

| Surface | Producer and ownership | Relocation behavior |
| --- | --- | --- |
| `.cache/index/backlog.db` metadata rows | Existing product-scoped `build_index` | Rebuild; existing metadata doctor validates canonical freshness. Durable sequences/reservations retain the existing relocation contract. |
| `_views/canonical-items.json` | Registered `canonical-item-list.v1`, using the existing canonical `ViewOps::list_item_projections` producer | Rebuild only when explicitly enabled for this product; compare exact bytes with a clean manifest-only canonical rebuild. |
| `views/` Markdown, Dataview, Bases and adjacent `.index.md` | Hand-authored custom views | Preserve exact bytes; never render, rewrite, or execute them. |
| Other `.cache/` files | Disposable indexes, embeddings, chunks and worksets | Omit; do not rebuild providers or unrelated caches. |
| Persona reports, snapshot packs and LLM analysis | Invocation-dependent artifacts; timestamps, output locations or external inputs prevent deterministic relocation rebuilding | Preserve when canonical artifacts; no registered relocation writer. |
| Retired generated Markdown dashboards | No current native refresh producer | Do not revive them or infer ownership from a filename under `views/`. |

Enable the fixed product list surface with the canonical product file
`_config/generated-views.json`:

```json
{
  "schema": "kob.product_generated_views.v1",
  "rebuilders": ["canonical-item-list.v1"]
}
```

Omitting the declaration (or using an empty list) invokes no generated-view hook.
Only the documented schema and registered name are admitted; arbitrary shell
commands, output paths, provider calls and rebuild-all are rejected. The declaration
is bounded to 16 KiB, belongs to the canonical byte manifest, and is bound into
the reviewed plan hash. An unregistered `_views` file fails planning as ambiguous
ownership. Plan JSON reports the applicable rebuilder and bounded product output
ref, plus one disposable-cache classification, without raw roots or cache-file
enumeration. Inventory file limits count excluded files too.

The view stores canonical metadata in the native list order with relative,
product-scoped item refs. It has no generation timestamps, source-root paths, shared topic
membership, index input, or LLM input. Pre-move generated bytes are never copied.
After canonical/config verification, apply journals the rebuilding stage, invokes
only selected product hooks, and compares output with a fresh manifest-only root
containing no cached or generated data. The product metadata doctor runs before
source retirement. Read-only `verify` repeats canonical rendering and detects
generated-output drift without repairing it.

The journal retains `generated_view_validation` status, selected/active rebuilder,
clean-oracle evidence and bounded failure details. Rebuild or validation failures
use the existing exact rollback transaction, retaining the original source views
and canonical records. `status` and confirmed `rollback` remain deterministic
after a recoverable interruption before source retirement. A completed rollback
is terminal; repeating apply does not silently replay its failed transaction.
The registered writer journals ownership before creating its exact atomic
temporary output. Automatic or explicit rollback removes only that owned regular
file; a pre-existing temporary file or symlink fails closed and is not deleted.
Persisted plans from before this hook keep their original custom-view exclusions
during verification and rollback; they do not acquire retroactive generated-view
hooks. In every transaction, `_views/canonical-items.json` is excluded only when
`canonical-item-list.v1` is registered in its plan. A concurrent unregistered file
created after inventory is unexpected drift; verification and rollback fail closed
and preserve its bytes.

Disposable 29-item HRR-shaped fixtures cover successful regeneration, stale
pre-move output, exact custom-view preservation, unrelated-product output/cache
preservation, rebuilding and oracle failures, rollback recovery, interruption
after generated validation, read-only drift detection, no-generated-view products,
partial atomic-output failure/recovery and unowned temporary-output preservation,
and unknown ownership/rebuilder rejection. Production HRR relocation and actual
separate-volume hard-interruption evidence remain outside this task.
