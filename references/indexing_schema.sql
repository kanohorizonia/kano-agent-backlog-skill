-- kano-agent-backlog-skill (file-first) optional DB index schema
--
-- This database is a rebuildable index/cache. Source of truth remains Markdown files under:
--   _kano/backlog/items/** and _kano/backlog/decisions/**
--
-- Notes:
-- - Keep schema compatible with SQLite; Postgres can map with minor type tweaks.
-- - Store raw frontmatter as JSON text to preserve unknown fields.
-- - Worklog is append-only in source; we index parsed entries for querying.

PRAGMA foreign_keys = ON;

-- Key/value metadata for the index itself.
CREATE TABLE IF NOT EXISTS schema_meta (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);

-- Initialize schema version to 0 (baseline; migrations will upgrade)
INSERT OR IGNORE INTO schema_meta(key, value) VALUES('schema_version', '0');

-- Backlog items (Epic/Feature/UserStory/Task/Bug, plus any process-defined types).
-- Schema aligned with canonical_schema.sql per ADR-0012
CREATE TABLE IF NOT EXISTS items (
  uid TEXT PRIMARY KEY,
  id TEXT NOT NULL,
  product TEXT NOT NULL,
  type TEXT NOT NULL,
  title TEXT NOT NULL,
  state TEXT,
  priority TEXT,
  parent_uid TEXT,
  area TEXT,
  iteration TEXT,
  owner TEXT,
  created TEXT,
  updated TEXT,
  path TEXT NOT NULL,
  mtime REAL,
  content_hash TEXT,
  frontmatter TEXT NOT NULL,
  tags TEXT,
  UNIQUE(product, id),
  UNIQUE(path)
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_items_path ON items(path);
CREATE UNIQUE INDEX IF NOT EXISTS idx_items_product_id ON items(product, id);
CREATE INDEX IF NOT EXISTS idx_items_product ON items(product);
CREATE INDEX IF NOT EXISTS idx_items_parent_uid ON items(parent_uid);
CREATE INDEX IF NOT EXISTS idx_items_state ON items(state);
CREATE INDEX IF NOT EXISTS idx_items_type ON items(type);
CREATE INDEX IF NOT EXISTS idx_items_id ON items(id);
CREATE INDEX IF NOT EXISTS idx_items_mtime ON items(mtime);

-- Tags: normalized for simple filtering/grouping.
CREATE TABLE IF NOT EXISTS item_tags (
  item_uid TEXT NOT NULL,
  tag TEXT NOT NULL,
  PRIMARY KEY(item_uid, tag),
  FOREIGN KEY(item_uid) REFERENCES items(uid) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_item_tags_tag ON item_tags(tag);

-- Links: typed relationships for graph queries (aligned with canonical schema).
CREATE TABLE IF NOT EXISTS links (
  source_uid TEXT NOT NULL,
  target_uid TEXT NOT NULL,
  type TEXT NOT NULL,
  PRIMARY KEY(source_uid, target_uid, type),
  FOREIGN KEY(source_uid) REFERENCES items(uid) ON DELETE CASCADE,
  FOREIGN KEY(target_uid) REFERENCES items(uid) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_links_source_uid ON links(source_uid);
CREATE INDEX IF NOT EXISTS idx_links_target_uid ON links(target_uid);
CREATE INDEX IF NOT EXISTS idx_links_type ON links(type);

-- Decisions/ADRs: we store decision references (links) from item frontmatter.
-- The decision_ref can be a wiki-link target, filename, or URL.
CREATE TABLE IF NOT EXISTS item_decisions (
  item_uid TEXT NOT NULL,
  decision_ref TEXT NOT NULL,
  PRIMARY KEY(item_uid, decision_ref),
  FOREIGN KEY(item_uid) REFERENCES items(uid) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_item_decisions_ref ON item_decisions(decision_ref);

-- Worklog: append-only audit trail (aligned with canonical schema per ADR-0012).
CREATE TABLE IF NOT EXISTS worklog (
  uid TEXT PRIMARY KEY,
  item_uid TEXT NOT NULL,
  timestamp TEXT NOT NULL,
  agent TEXT NOT NULL,
  content TEXT NOT NULL,
  FOREIGN KEY(item_uid) REFERENCES items(uid) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_worklog_item_uid ON worklog(item_uid);
CREATE INDEX IF NOT EXISTS idx_worklog_timestamp ON worklog(timestamp);
CREATE INDEX IF NOT EXISTS idx_worklog_agent ON worklog(agent);

