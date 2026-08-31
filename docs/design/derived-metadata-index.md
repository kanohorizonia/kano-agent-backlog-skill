# Derived metadata index

KOB keeps canonical item Markdown and frontmatter authoritative. The local
SQLite metadata index is a disposable projection used by bounded list, search,
status, and exact-item reads. A cache row never overrides canonical content.

## Contract

Each metadata row is product-scoped and records:

- display ID and UID
- product, item type, state, priority, title, and slug
- parent and duplicate references
- updated date and estimated token count
- product-relative source reference
- source size, high-resolution modification time, and content hash

Raw workspace paths are not part of the read contract. `source_ref` is
product-relative and rejects absolute paths or parent traversal.

The disposable SQLite row schema and snapshot schema are both internal version
3 contracts (`schema_version=3` and `snapshot_schema_version=3`). They include
the indexed rows, inventory and content revisions, item count, generation,
canonical-write receipt fence, platform change-proof state, status, and optional
invalidation reason. Inventory revision is deterministic over sorted source
refs, sizes, and modification times. Content revision is deterministic over
sorted source refs and source hashes.

Internal schema numbers do not version the CLI response. The public build and
refresh response remains `kob.metadata-index.v2`, status remains
`kob.metadata-index-status.v2`, and the public snapshot token remains
`kob.metadata-index-snapshot.v2`. Query and doctor keep their independently
versioned v1 response envelopes while referring to the v2 public snapshot
contract.

Every ready snapshot has an opaque, product-scoped `product_revision` with a
stable epoch and monotonically increasing generation. A verified tracked
mutation or rebuild advances the generation. Callers may return that token as
`--requested-revision`; they must not derive canonical state from its contents.

Readiness records platform-specific proof state. On Windows, a ready snapshot
uses `proof_kind=windows-ntfs-usn-v1`, verified root and journal identities,
verified USN data, and verified canonical-file and directory watch rows. On
non-Windows, a ready snapshot persists `proof_kind=none` and
`proof_status=unsupported`, with zero proof identities and USN values and no
watch rows. Portable readiness permits validated ordinary reads, not an
unchanged claim without a supported witness.

## Read lifecycle

KOB validates a ready snapshot before using its rows.

- Exact ID or UID reads validate the matched source hash.
- List and metadata search reads validate the complete content revision.
- On non-Windows, ordinary `query` and `doctor` reads may use rows only after
  canonical write-receipt, inventory, and full-content validation. Raw content
  drift fails closed.
- An exact ref absent from the index receives a bounded canonical lookup.
- Missing, incomplete, stale, schema-mismatched, or corrupt metadata falls back
  to canonical frontmatter.
- Fallback never silently omits an item solely because the index is unavailable.
- A matching `--requested-revision` with supported proof uses the persisted
  receipt and change witness as a zero-canonical-scan comparison. A clean
  comparison returns `status=unchanged`, `fallback_scan=false`, and
  `scanned_count=0`. An equal requested revision on portable proof state returns
  `status=stale`, `stale_reason=change_proof_unsupported`, `unchanged=false`,
  `fallback_scan=false`, and `scanned_count=0` with zero proof work. It never
  claims unchanged without a supported witness.
- A mismatched requested revision may return `status=ready` only after bounded
  snapshot schema, revision, proof, watch-row, and product-relative path
  validation.
- The comparison has one absolute 80 ms deadline that starts at status entry,
  includes token validation, and covers read-only SQLite state acquisition,
  supported proof inspection, and any mandatory checkpoint compare-and-swap.
  Tokens over 96 bytes are rejected before character traversal. Snapshot and
  watch rows are read in one transaction. The requested-revision path does not
  run doctor, retry a contended write, sleep, or scan canonical Markdown.
- Clean proof below all lazy checkpoint caps returns unchanged without writing.
  Crossing any cap (`records >= 8192`, returned bytes `>= 1 MiB`, or USN span
  `>= 1 MiB`) requires a successful zero-wait checkpoint compare-and-swap before
  returning unchanged. Contention or any other persistence failure fails closed.
- Before returning an unchanged result without a checkpoint write, KOB confirms
  that the snapshot used by the proof is still current. Concurrent snapshot or
  watch replacement therefore fails closed instead of mixing revisions.
- Invalid persisted revision strings or reasons are not projected. They return
  a fixed, bounded stale reason with empty persisted `index_revision`,
  `canonical_revision`, and `product_revision` fields and zero canonical scan.
- An equality comparison that cannot prove unchanged fails closed. If
  no canonical scan occurred, it returns `status=stale`, `unchanged=false`,
  `fallback_scan=false`, `scanned_count=0`, a bounded `stale_reason`, and the
  explicit recovery command `kob index rebuild --product <product>`.
- `fallback_scan=true` means canonical metadata was actually scanned; it is not
  used merely to label a fail-closed requested-revision response.

Read diagnostics expose:

```text
index_used
index_status
index_revision
canonical_revision
product_revision
requested_revision
unchanged
fallback_scan
scanned_count
proof_records_read
proof_bytes_read
proof_usn_span
proof_checkpoint_required
proof_checkpoint_persisted
matched_count
revision_check_ms
elapsed_ms
stale_reason
recovery
```

Proof counters are bounded and saturated. Diagnostics do not expose raw USNs,
journal or file identities, or workspace paths.

Ordinary item listing remains stable. Pass `--index-diagnostics` to emit the
diagnostic object on stderr.

## Mutation lifecycle

Canonical writes happen first and advance a lock-serialized receipt containing
the previous and current write revisions, operation, bounded source-ref hash,
content size, and content hash where applicable. Successful create, state,
Ready-field, reparent, relation, worklog, decision, and artifact mutations then
update the affected derived row. Tracked deletion removes its row. The index
accepts the incremental update only when the receipt is contiguous with the
snapshot fence and matches the canonical readback. A missing, malformed,
noncontiguous, or mismatched receipt invalidates the snapshot and requires a
rebuild. Batch schema repair rebuilds the product snapshot once after canonical
writes finish.

An incremental row update keeps a previously verified snapshot ready. If no
verified snapshot exists, the projection remains `incomplete` until a rebuild.
This prevents a partial producer run from becoming an authoritative read source.

On Windows, the `windows-ntfs-usn-v1` witness contains the product root's NTFS
volume serial and file ID, USN journal identity and verified USN endpoint, plus
watched canonical file and directory IDs. Verification fails closed on a
different root identity, journal reset or wrap, malformed or missing watch state,
budget exhaustion, or a relevant canonical USN record. This catches raw
same-size edits even when their modification time is restored.

Windows rebuild captures the receipt and NTFS witness before scanning, hashes
canonical rows, captures the resulting watch set, then verifies that neither
fence changed inside the rebuild window. Portable rebuild repeats canonical
write-receipt, inventory, and full-content validation as a second validation
fence after scanning and before publication. Only then does one SQLite
transaction replace product rows, compute revisions, and publish the ready v3
snapshot and next `product_revision`. A failed proof, validation, or transaction
publishes nothing; recovery is an explicit product-scoped rebuild.

## Operations

```bash
kob -P <product> index build --force --format json
kob -P <product> index refresh --format json
kob -P <product> index status --format json
kob -P <product> index query --item <ID-or-UID> --format json
kob -P <product> index doctor --format json
```

`build` and `refresh` both perform an atomic canonical rebuild. `doctor` verifies
row parity and source hashes. The recovery value in stale diagnostics points to
the product-scoped rebuild command.

The metadata rows and snapshots are disposable. The current SQLite container
also preserves ID sequence and reservation tables, so operators should use
`kob index build` instead of deleting the database file. Metadata schema
reconciliation replaces only derived tables and leaves sequence state intact.

## Performance evidence

`metadata_index_smoke_test` creates more than 600 canonical items and enforces:

- exact lookup p95 below 100 ms
- metadata query p95 below 500 ms
- bounded token query p95 below 2 seconds
- canonical revision check p95 below 100 ms
- matching requested-revision comparison p95 below 100 ms with zero canonical
  items scanned when a supported Windows witness is available
- mandatory checkpoint contention and concurrent snapshot replacement below
  100 ms, both failing closed without a canonical scan

The fixture also covers cold startup, explicit invalidation, tracked and
out-of-band mutations, create/reparent/state/decision lifecycle, deletion,
incomplete and corrupt databases, source-hash drift, raw same-size/restored-mtime
edits, deterministic rebuild-window mutation, fail-closed revision recovery, and
shared-database product isolation. Evidence includes deterministic equality
coverage, nonempty missing/corrupt zero-scan responses, and JSON/plain redaction
coverage for invalid persisted strings and reasons. Windows witness checks run
only where NTFS and USN are available. Portable assertions define conditional
contract coverage and do not claim Linux or macOS execution evidence.

## Boundaries

- The index is not canonical storage.
- It is not a semantic or vector index.
- It does not replace product or prefix resolution.
- It does not expose raw filesystem paths.
- Cache failure must not block local-first canonical reads.
