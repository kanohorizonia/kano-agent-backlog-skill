# KOA Metadata Index Consumer Contract

Status: versioned public KOB contract with executable adapter fixtures.

## Goal

KOA backlog.item_search and status.overview may consume KOB metadata queries
and revision status without parsing KOB databases, product roots, item files,
or change-proof records. KOB remains the sole index owner and canonical
frontmatter remains authoritative.

The consumer contract is:

~~~text
kob.koa-metadata-index-consumer.v1
~~~

The fixture output contract is:

~~~text
kob.koa-metadata-index-adapter.v1
~~~

## Supported KOB Envelopes

Version 1 accepts exactly these upstream schemas:

| Operation | Required schemas |
| --- | --- |
| item_search | kob.metadata-index-query.v1 plus kob.metadata-index-snapshot.v2 |
| status_overview | kob.metadata-index-status.v2 |

Internal metadata row and SQLite snapshot schema version 3 is not a consumer
contract. KOA must reject an unknown public envelope before interpreting its
payload, even when the payload otherwise resembles a supported version.

## Request Contract

Both operations require:

- consumer_schema: exact supported consumer version.
- operation: item_search or status_overview.
- product: canonical product slug after KOA alias resolution.

item_search accepts:

- exact_ref: canonical display ID or UID, maximum 160 bytes, with no path
  separators.
- query: bounded metadata query, maximum 512 bytes and 16 tokens.
- state and type: canonical filters.
- limit: 1 through 20,000.
- case_sensitive: consumer matching preference. Product alias resolution
  remains case-insensitive.

At least one exact reference, query, or metadata filter is expected. Exact
references map to KOB --item; metadata and token queries map to KOB --query,
--state, and --type.

status_overview accepts:

- requested_revision: optional authoritative kob-pr-v1: token, maximum 96
  bytes.

Requested-revision verification has an 80 ms KOB budget. A consumer must not
extend that budget with retries or treat a fallback scan as revision proof.

## Deterministic Ordering and Scope

Every query is product-scoped. An item whose public product differs from the
request is an invalid upstream response.

The stable item ordering is:

1. updated_desc
2. item_id_asc

The adapter preserves upstream order. It does not re-rank, merge products, or
perform semantic retrieval.

## Consumer Response

The adapter response preserves:

- product
- canonical item id and uid
- type, state, title, parent, and updated
- matched_count, returned_count, and scanned_count
- elapsed_ms
- cache_status, fallback_scan, and unchanged
- bounded product_revision, index_revision, and stale_reason

The response deliberately omits:

- backlog_root
- raw or product-relative source_ref
- source_hash
- index_ref
- SQLite details
- volume, file, journal, USN, or watch identities
- unbounded logs or command output

KOA may add its own normal tool envelope around this projection, but it must
not widen the KOB payload or expose producer-private fields.

## Freshness and Fallback

### Indexed query

cache_status=ready, fallback_scan=false, and index_used=true describe a
validated indexed result.

### Missing, stale, or corrupt query

KOB may return a bounded canonical fallback with:

- index_used=false
- fallback_scan=true
- an explicit cache_status
- bounded counts, timing, stale reason, and recovery guidance

The adapter preserves the canonical items and bounded diagnostics. It never
labels fallback output as indexed or unchanged.

### Requested status

When platform change proof supports the requested revision, KOB may return
status=unchanged, unchanged=true, and zero canonical scan work.

On platforms without supported proof, the same revision fails closed as stale
with change_proof_unsupported, unchanged=false, and zero scan work. Missing or
corrupt revision receipts likewise fail closed. The adapter must preserve that
distinction and must not retry or scan to manufacture proof.

## Error Codes

The adapter fails closed with bounded codes:

| Code | Meaning |
| --- | --- |
| unsupported_consumer_contract | consumer_schema is not the exact supported version. |
| unsupported_upstream_schema | KOB query, snapshot, or status schema is unknown. |
| invalid_request | Product, query, exact ref, limit, or revision violates bounds. |
| invalid_upstream_response | Product scope, array shape, or required diagnostics are invalid. |
| unsupported_operation | Operation is not item_search or status_overview. |

Errors contain no producer paths, database errors, proof identities, or raw
payload dumps.

## Upgrade Rules

1. KOB owns this schema and publishes a new consumer version for breaking
   request, response, ordering, or freshness changes.
2. Additive upstream fields do not become KOA fields automatically. The
   allowlisted projection changes only with contract review and fixtures.
3. KOA advertises one exact consumer version and the accepted KOB query,
   snapshot, and status schemas.
4. Unknown consumer or upstream versions fail before item or diagnostic
   interpretation.
5. KOB internal row, SQLite, or proof schema revisions do not require a
   consumer version when public envelopes remain unchanged.
6. Pre-1.0 development replaces this contract in place through an explicit
   version bump; no compatibility shim parses both incompatible shapes.

## Executable Fixtures

The reference artifacts are:

- [koa-metadata-index-consumer.schema.json](../../references/koa-metadata-index-consumer.schema.json)
- [koa-metadata-index-consumer.fixture.json](../../references/koa-metadata-index-consumer.fixture.json)

metadata_index_smoke_test parses both artifacts and executes these scenarios:

- exact indexed lookup
- metadata-filtered query
- bounded token query
- unchanged requested status
- stale canonical fallback
- corrupt canonical fallback
- missing-index canonical fallback
- unsupported consumer version
- unsupported upstream version

The test validates structured parity and recursively rejects forbidden fields
from every adapter response. KOA source remains a read-only consumer reference;
the fixture does not transfer index ownership or parse an index file.
