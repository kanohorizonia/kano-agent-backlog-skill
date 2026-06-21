#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

#include <drogon/drogon.h>

#include "kano/backlog_core/process/noninteractive_errors.hpp"
#include "KanoBacklog.BacklogWebviewService.hpp"

namespace {

std::filesystem::path ResolveProductsRoot(int argc, char** argv) {
  std::filesystem::path defaultRoot = "_kano/backlog/products";

  if (const char* envRoot = std::getenv("KANO_BACKLOG_PRODUCTS_ROOT"); envRoot != nullptr) {
    if (std::strlen(envRoot) > 0) {
      defaultRoot = envRoot;
    }
  }

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--backlog-root" && (i + 1) < argc) {
      return argv[i + 1];
    }
  }

  return defaultRoot;
}

uint16_t ResolvePort(int argc, char** argv) {
  uint16_t port = 8787;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--port" && (i + 1) < argc) {
      port = static_cast<uint16_t>(std::stoi(argv[i + 1]));
      return port;
    }
  }

  if (const char* envPort = std::getenv("KANO_WEBVIEW_PORT"); envPort != nullptr) {
    if (std::strlen(envPort) > 0) {
      port = static_cast<uint16_t>(std::stoi(envPort));
    }
  }
  return port;
}

std::string ResolveHost(int argc, char** argv) {
  std::string host = "127.0.0.1";
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--host" && (i + 1) < argc) {
      return argv[i + 1];
    }
  }

  if (const char* envHost = std::getenv("KANO_WEBVIEW_HOST"); envHost != nullptr) {
    if (std::strlen(envHost) > 0) {
      host = envHost;
    }
  }
  return host;
}

const char* kIndexHtml = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Kano Backlog Webview</title>
  <style>
    body { font-family: "Segoe UI", sans-serif; margin: 0; padding: 16px; background: #f7f8fa; color: #1a1f2e; }
    .app-shell { display: grid; grid-template-columns: 280px minmax(0, 1fr); gap: 12px; align-items: start; }
    .sidebar { position: sticky; top: 16px; }
    .workspace-list { display: flex; flex-direction: column; gap: 6px; margin-top: 8px; max-height: 45vh; overflow: auto; }
    .workspace-row { display: grid; grid-template-columns: minmax(0,1fr) auto auto; gap: 6px; align-items: center; }
    .workspace-item { text-align: left; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
    .workspace-item.active { background: #1f4fa3; color: #fff; border-color: #1f4fa3; }
    .workspace-meta { font-size: 11px; color: #70809f; margin-top: 2px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
    .icon-btn { border: 1px solid #cfd9ea; background: #fff; border-radius: 6px; padding: 4px 6px; cursor: pointer; }
    .icon-btn:hover { background: #f2f6ff; }
    .row { display: flex; gap: 12px; align-items: center; margin-bottom: 12px; }
    .panel { background: #fff; border: 1px solid #dde3f0; border-radius: 10px; padding: 12px; margin-bottom: 12px; }
    .tabs { display: flex; gap: 8px; }
    .tab-btn { border: 1px solid #cfd9ea; background: #fff; border-radius: 8px; padding: 6px 12px; cursor: pointer; }
    .tab-btn.active { background: #1f4fa3; color: #fff; border-color: #1f4fa3; }
    .page { display: none; }
    .page.active { display: block; }
    .kanban { display: grid; grid-template-columns: repeat(5, minmax(180px, 1fr)); gap: 10px; }
    .lane { background: #fff; border: 1px solid #dde3f0; border-radius: 10px; padding: 8px; min-height: 140px; }
    .card { border: 1px solid #cfd9ea; border-radius: 8px; padding: 8px; margin-bottom: 8px; background: #fcfdff; }
    .review-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 10px; }
    .review-lane { border: 1px solid #d8e1f0; border-radius: 8px; padding: 8px; background: #fff; min-height: 120px; }
    .evidence-row { display: grid; grid-template-columns: 130px 90px minmax(0, 1fr); gap: 8px; padding: 4px 0; border-bottom: 1px solid #edf1f7; }
    .pill { display: inline-block; border: 1px solid #cfd9ea; border-radius: 999px; padding: 2px 7px; font-size: 12px; background: #f8fbff; }
    .pill.passed { border-color: #86c48b; background: #f1fbf2; color: #245c2a; }
    .pill.failed { border-color: #db8a8a; background: #fff4f4; color: #8a2525; }
    .pill.blocked { border-color: #d5b15d; background: #fff9e8; color: #7a5610; }
    .pill.missing { border-color: #c5ccd9; background: #f4f6fa; color: #4f5a6e; }
    .text-input-wide { width: min(760px, 100%); }
    .tree ul { list-style: none; padding-left: 18px; margin: 0; }
    .tree li { margin: 2px 0; }
    .tree details { margin: 2px 0; }
    .tree summary { cursor: pointer; }
    .tree summary::marker { color: #5a6d8f; }
    .tree .node-line { display: inline-flex; gap: 6px; align-items: center; }
    .tree .leaf-spacer { display: inline-block; width: 12px; }
    .btn { border: 1px solid #cfd9ea; background: #fff; border-radius: 6px; padding: 4px 10px; cursor: pointer; }
    .btn:hover { background: #f2f6ff; }
    .filter-panel { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 12px; }
    .filter-group { display: flex; flex-direction: column; gap: 8px; }
    .filter-group-title { font-size: 12px; font-weight: 700; color: #3c4a63; text-transform: uppercase; }
    .filters { display: flex; gap: 10px; flex-wrap: wrap; margin: 0; }
    .filters label { display: inline-flex; gap: 6px; align-items: center; font-size: 13px; }
    .limit-input { width: 80px; }
    .item-link { color: #1f4fa3; text-decoration: none; }
    .item-link:hover { text-decoration: underline; }
    .modal-backdrop { position: fixed; inset: 0; background: rgba(20, 28, 44, 0.45); display: none; align-items: center; justify-content: center; padding: 24px; }
    .modal-backdrop.open { display: flex; }
    .modal { width: min(980px, 92vw); max-height: 88vh; overflow: auto; background: #fff; border-radius: 10px; border: 1px solid #d7dfef; }
    .modal-head { display: flex; justify-content: space-between; align-items: center; padding: 12px 14px; border-bottom: 1px solid #e7edf8; }
    .modal-body { padding: 12px 14px; }
    .modal pre { white-space: pre-wrap; word-break: break-word; background: #f6f8fc; border: 1px solid #dfe6f3; border-radius: 8px; padding: 10px; }
    .md-view h1,.md-view h2,.md-view h3,.md-view h4 { margin: 14px 0 8px 0; }
    .md-view p { margin: 8px 0; }
    .md-view ul,.md-view ol { margin: 8px 0 8px 22px; }
    .md-view blockquote { margin: 8px 0; padding: 8px 12px; border-left: 4px solid #9fb5de; background: #f5f8ff; }
    .md-view table { border-collapse: collapse; width: 100%; }
    .md-view th,.md-view td { border: 1px solid #d7dfef; padding: 6px 8px; text-align: left; }
    .md-view .obs-callout { border: 1px solid #c9d7ef; border-radius: 8px; padding: 8px 10px; background: #f8fbff; margin: 10px 0; }
    .md-view .obs-callout-title { font-weight: 600; margin-bottom: 6px; color: #35588f; }
    .md-view .obs-wikilink { color: #1f4fa3; text-decoration: none; }
    .md-view .obs-wikilink:hover { text-decoration: underline; }
    code { background: #eef2f8; padding: 1px 4px; border-radius: 4px; }
    .muted { color: #586074; font-size: 12px; }
    .graph-canvas { width: 100%; overflow: auto; border: 1px solid #dbe4f2; border-radius: 8px; background: #fbfcff; margin-bottom: 12px; }
    .graph-svg { min-width: 760px; display: block; }
    .graph-edge { stroke: #7a879d; stroke-width: 1.5; fill: none; }
    .graph-edge.blocks,.graph-edge.blocked_by { stroke: #b44646; stroke-width: 2; }
    .graph-edge.parent { stroke: #4f6fa9; }
    .graph-edge.topic-membership { stroke: #498264; stroke-dasharray: 5 4; }
    .graph-edge.relates { stroke: #7d6aa6; stroke-dasharray: 3 4; }
    .graph-node rect { fill: #fff; stroke: #b9c7de; rx: 8; }
    .graph-node.topic rect { fill: #eef8f2; stroke: #7eb58d; }
    .graph-node.missing rect { fill: #fff4f4; stroke: #d48b8b; stroke-dasharray: 5 4; }
    .graph-node.dependency rect { stroke: #c65f5f; }
    .graph-label { font-size: 12px; fill: #1a1f2e; }
    .graph-meta { font-size: 10px; fill: #65738b; }
    .graph-edge-label { font-size: 10px; fill: #47536a; }
    .graph-diagnostics { display: grid; gap: 6px; margin-bottom: 12px; }
  </style>
  <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/highlight.js@11.9.0/styles/github.min.css" />
</head>
<body>
  <div class="app-shell">
    <aside class="panel sidebar">
      <h3 style="margin-top:0;">Workspaces</h3>
      <div id="workspace-current" class="muted" style="margin-bottom:8px;"></div>
      <div class="row" style="margin-bottom:8px;">
        <input id="workspace-input" placeholder="Path: backlog root or products" style="width:100%;" />
      </div>
      <div class="row" style="margin-bottom:8px;">
        <button id="workspace-add" class="btn">Open Workspace</button>
      </div>
      <div class="muted" style="margin-bottom:8px;">Shortcut: Alt+1..9 switch workspace</div>
      <div id="workspace-list" class="workspace-list"></div>
    </aside>

    <main>
  <div class="row panel">
    <label for="product">Scope:</label>
    <select id="product"></select>
    <input id="search" placeholder="Search id/title/topic/content" />
    <label for="limit">Limit:</label>
    <input id="limit" class="limit-input" type="number" min="1" max="1000" value="200" />
    <button id="refresh">Refresh</button>
    <span id="status" class="muted"></span>
  </div>

  <div class="panel filter-panel">
    <div class="filter-group">
      <div class="filter-group-title">Products</div>
      <div class="filters" id="product-filters"></div>
    </div>
    <div class="filter-group">
      <div class="filter-group-title">States</div>
      <div class="filters" id="state-filters"></div>
    </div>
    <div class="filter-group">
      <div class="filter-group-title">Types</div>
      <div class="filters" id="type-filters"></div>
    </div>
  </div>

  <div class="panel">
    <div class="tabs">
      <button id="tab-tree" class="tab-btn active" data-tab="tree">Tree</button>
      <button id="tab-kanban" class="tab-btn" data-tab="kanban">Kanban</button>
      <button id="tab-context" class="tab-btn" data-tab="context">Context</button>
      <button id="tab-review" class="tab-btn" data-tab="review">Review</button>
      <button id="tab-graph" class="tab-btn" data-tab="graph">Graph</button>
      <button id="tab-runs" class="tab-btn" data-tab="runs">Runs</button>
      <button id="tab-command" class="tab-btn" data-tab="command">Command</button>
    </div>
  </div>

  <div id="page-tree" class="panel tree page active">
      <div class="row" style="margin: 0 0 8px 0;">
        <h3 style="margin: 0;">Tree</h3>
        <button id="expand-all" class="btn">Expand All</button>
        <button id="collapse-all" class="btn">Collapse All</button>
      </div>
      <div id="tree"></div>
  </div>

  <div id="page-kanban" class="panel page">
      <h3>Kanban</h3>
      <div id="kanban" class="kanban"></div>
  </div>

  <div id="page-context" class="panel page">
    <h3>Context (ADR / Topic / Workset)</h3>
    <div id="context-summary" class="muted" style="margin-bottom:8px;"></div>
    <div id="context-list"></div>
  </div>

  <div id="page-review" class="panel page">
    <h3>Human Review Inbox</h3>
    <div id="saved-views" class="row" style="align-items:flex-start; flex-wrap:wrap;"></div>
    <div id="review-inbox" class="review-grid"></div>
  </div>

  <div id="page-graph" class="panel page">
    <h3>Dependency Graph</h3>
    <div id="graph-summary" class="muted" style="margin-bottom:8px;"></div>
    <div id="graph-list"></div>
  </div>

  <div id="page-runs" class="panel page">
    <h3>Agent Run Board</h3>
    <div id="runs-summary" class="muted" style="margin-bottom:8px;"></div>
    <div id="runs-list"></div>
  </div>

  <div id="page-command" class="panel page">
    <h3>Command Palette Preview</h3>
    <div class="row">
      <input id="command-input" class="text-input-wide" placeholder="Example: show ready tasks" />
      <button id="command-preview" class="btn">Preview</button>
    </div>
    <div id="command-result"></div>
  </div>
    </main>
  </div>

  <div id="item-modal-backdrop" class="modal-backdrop">
    <div class="modal">
      <div class="modal-head">
        <strong id="item-modal-title">Item Detail</strong>
        <button id="item-modal-close" class="btn">Close</button>
      </div>
      <div id="item-modal-body" class="modal-body"></div>
    </div>
  </div>

)HTML"
R"HTML(  <script src="https://cdn.jsdelivr.net/npm/marked/marked.min.js"></script>
  <script src="https://cdn.jsdelivr.net/npm/highlight.js@11.9.0/lib/highlight.min.js"></script>
  <script>
    const state = {
      product: 'all',
      products: [],
      selectedProducts: new Set(['all']),
      selectedStates: new Set(['Proposed', 'Ready', 'InProgress', 'Blocked', 'Review', 'Done', 'Dropped']),
      selectedTypes: new Set(['Theme', 'Epic', 'Feature', 'UserStory', 'Task', 'Bug', 'Issue', 'ADR', 'Topic', 'Workset']),
      q: '',
      limit: 200,
      workspace: '',
      workspaces: [],
      treeOpen: new Set(),
      treeTouched: false,
      activeTab: 'tree',
      allItems: []
    };
    const lanes = ['Backlog', 'Doing', 'Blocked', 'Review', 'Done'];
    const itemStates = ['Proposed', 'Ready', 'InProgress', 'Blocked', 'Review', 'Done', 'Dropped'];
    const itemTypes = ['Theme', 'Epic', 'Feature', 'UserStory', 'Task', 'Bug', 'Issue', 'ADR', 'Topic', 'Workset'];
    const workspaceStorageKey = 'kano_webview_workspaces_v2';

    async function getJson(url) {
      const resp = await fetch(url);
      return resp.json();
    }

    function nowIso() {
      return new Date().toISOString();
    }

    function normalizeWorkspaceEntry(entry) {
      if (typeof entry === 'string') {
        const clean = entry.trim();
        if (!clean) return null;
        return { path: clean, label: clean.split(/[\\/]/).filter(Boolean).pop() || clean, lastUsed: '' };
      }
      if (!entry || typeof entry !== 'object') {
        return null;
      }
      const path = String(entry.path || '').trim();
      if (!path) {
        return null;
      }
      const labelRaw = String(entry.label || '').trim();
      const label = labelRaw || path.split(/[\\/]/).filter(Boolean).pop() || path;
      const lastUsed = String(entry.lastUsed || '');
      return { path, label, lastUsed };
    }

    function sortedWorkspaces() {
      return [...state.workspaces].sort((a, b) => {
        if (a.path === state.workspace) return -1;
        if (b.path === state.workspace) return 1;
        return (b.lastUsed || '').localeCompare(a.lastUsed || '');
      });
    }

    function loadSavedWorkspaces() {
      try {
        const raw = localStorage.getItem(workspaceStorageKey);
        if (!raw) {
          return [];
        }
        const parsed = JSON.parse(raw);
        if (!Array.isArray(parsed)) {
          return [];
        }
        const normalized = parsed
          .map(normalizeWorkspaceEntry)
          .filter((x) => x && x.path);
        const dedup = new Map();
        for (const w of normalized) {
          dedup.set(w.path, w);
        }
        return [...dedup.values()];
      } catch (_e) {
        return [];
      }
    }

    function saveWorkspaces() {
      try {
        localStorage.setItem(workspaceStorageKey, JSON.stringify(state.workspaces));
      } catch (_e) {
      }
    }

    function saveWorkspaces() {
      try {
        localStorage.setItem(workspaceStorageKey, JSON.stringify(state.workspaces));
      } catch (_e) {
      }
    }

    function upsertWorkspace(path, updates = {}) {
      const clean = String(path || '').trim();
      if (!clean) {
        return;
      }
      const idx = state.workspaces.findIndex((w) => w.path === clean);
      if (idx >= 0) {
        state.workspaces[idx] = {
          ...state.workspaces[idx],
          ...updates,
          path: clean,
        };
      } else {
        const baseLabel = clean.split(/[\\/]/).filter(Boolean).pop() || clean;
        state.workspaces.push({
          path: clean,
          label: String(updates.label || baseLabel),
          lastUsed: String(updates.lastUsed || ''),
        });
      }
      const sorted = sortedWorkspaces();
      state.workspaces = sorted.slice(0, 20);
      saveWorkspaces();
    }

    function touchWorkspace(path) {
      upsertWorkspace(path, { lastUsed: nowIso() });
    }

    function removeWorkspace(path) {
      state.workspaces = state.workspaces.filter((w) => w.path !== path);
      saveWorkspaces();
      renderWorkspaceList();
    }

    function renameWorkspace(path) {
      const current = state.workspaces.find((w) => w.path === path);
      if (!current) return;
      const renamed = prompt('Workspace label', current.label || '');
      if (renamed === null) return;
      const cleanLabel = renamed.trim();
      if (!cleanLabel) return;
      upsertWorkspace(path, { label: cleanLabel });
      renderWorkspaceList();
    }

    function addWorkspace(path) {
      upsertWorkspace(path, { lastUsed: nowIso() });
    }

    function renderWorkspaceList() {
      const list = sortedWorkspaces();
      const html = list.map((w, i) => {
        const active = w.path === state.workspace ? 'active' : '';
        const title = `${w.label}\n${w.path}`;
        const shortcut = i < 9 ? `<div class=\"workspace-meta\">Alt+${i + 1}</div>` : '<div class="workspace-meta"></div>';
        return `<div class="workspace-row"><button class="btn workspace-item ${active}" data-workspace-path="${escAttr(w.path)}" title="${escAttr(title)}">${esc(w.label)}</button><button class="icon-btn" data-workspace-rename="${escAttr(w.path)}" title="Rename">✎</button><button class="icon-btn" data-workspace-remove="${escAttr(w.path)}" title="Remove">✕</button>${shortcut}</div>`;
      }).join('');
      document.getElementById('workspace-list').innerHTML =
          html || '<div class="muted">No saved workspaces</div>';

      document.querySelectorAll('#workspace-list [data-workspace-path]').forEach((btn) => {
        btn.addEventListener('click', async () => {
          const path = btn.getAttribute('data-workspace-path');
          if (!path) {
            return;
          }
          await switchWorkspace(path, false);
        });
      });

      document.querySelectorAll('#workspace-list [data-workspace-rename]').forEach((btn) => {
        btn.addEventListener('click', (event) => {
          event.stopPropagation();
          const path = btn.getAttribute('data-workspace-rename');
          if (!path) return;
          renameWorkspace(path);
        });
      });

      document.querySelectorAll('#workspace-list [data-workspace-remove]').forEach((btn) => {
        btn.addEventListener('click', (event) => {
          event.stopPropagation();
          const path = btn.getAttribute('data-workspace-remove');
          if (!path) return;
          removeWorkspace(path);
        });
      });
    }

    function showWorkspaceCurrent(path) {
      document.getElementById('workspace-current').textContent =
          path ? `Current: ${path}` : 'Current: (unknown)';
    }

    async function loadWorkspaceInfo() {
      const result = await getJson('/api/workspace/info');
      const ws = result?.data?.workspace_root || '';
      state.workspace = ws;
      touchWorkspace(ws);
      showWorkspaceCurrent(ws);
      renderWorkspaceList();
    }

    async function switchWorkspace(path, updateInput = true) {
      const clean = String(path || '').trim();
      if (!clean) {
        return;
      }
      const result = await getJson(`/api/workspace/switch?path=${encodeURIComponent(clean)}`);
      if (!result?.ok) {
        document.getElementById('status').textContent =
            result?.data?.error || 'Failed to switch workspace';
        return;
      }
      const ws = result?.data?.workspace_root || clean;
      state.workspace = ws;
      touchWorkspace(ws);
      showWorkspaceCurrent(ws);
      renderWorkspaceList();
      if (updateInput) {
        document.getElementById('workspace-input').value = ws;
      }

      state.treeOpen.clear();
      state.treeTouched = false;
      await loadProducts();
      await refreshAll();
    }

    function esc(v) {
      return String(v ?? '').replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;');
    }

    function escAttr(v) {
      return esc(v).replaceAll('"', '&quot;');
    }

    function selectedProductValues() {
      const values = [...state.selectedProducts].filter((p) => p && p !== 'all');
      if (state.selectedProducts.has('all') || values.length === 0) {
        return ['all'];
      }
      return values;
    }

    function productScopeLabel() {
      const products = selectedProductValues();
      if (products.length === 1 && products[0] === 'all') {
        return 'all products';
      }
      if (products.length === 1) {
        return products[0];
      }
      return `${products.length} products`;
    }

    function selectedTokens(values, allValues) {
      if (values.size === allValues.length) {
        return '';
      }
      return values.size === 0 ? '__none__' : [...values].join(',');
    }

    function queryString() {
      const params = new URLSearchParams();
      const products = selectedProductValues();
      if (products.length === 1) {
        params.set('product', products[0]);
      } else {
        params.set('products', products.join(','));
      }
      if (state.q) {
        params.set('q', state.q);
      }
      const states = selectedTokens(state.selectedStates, itemStates);
      if (states) {
        params.set('state', states);
      }
      const types = selectedTokens(state.selectedTypes, itemTypes);
      if (types) {
        params.set('type', types);
      }
      params.set('limit', String(state.limit || 200));
      return params.toString();
    }

    function renderMeta(item) {
      const parts = [];
      if (item.product) parts.push(item.product);
      if (item.type) parts.push(item.type);
      if (item.state) parts.push(item.state);
      if (item.source_kind) parts.push(item.source_kind);
      parts.push(item.topic ? `Topic: ${item.topic}` : 'Topic: none');
      return parts.join(' / ');
    }

    function treeNodeKey(node) {
      return `${node.product || ''}::${node.id || ''}`;
    }

    function renderMarkdownWithObsidian(raw) {
      let text = String(raw || '');

      text = text.replace(/\[\[([^\]|]+)(\|([^\]]+))?\]\]/g, (_m, target, _p2, alias) => {
        const cleanTarget = String(target || '').trim();
        const label = String(alias || cleanTarget || '').trim();
        return `<a href="#" class="obs-wikilink" data-item-id="${escAttr(cleanTarget)}">${esc(label)}</a>`;
      });

      text = text.replace(/(^|\n)>\s*\[!([A-Za-z0-9_-]+)\]\s*(.*)\n((?:>.*\n?)*)/g,
        (_m, lead, kind, title, content) => {
          const body = String(content || '')
            .split('\n')
            .map((line) => line.replace(/^>\s?/, ''))
            .join('\n')
            .trim();
          const resolvedTitle = (title || kind || 'Callout').trim();
          return `${lead}<div class="obs-callout"><div class="obs-callout-title">${esc(resolvedTitle)}</div>\n\n${body}\n</div>\n`;
        });

      if (window.marked) {
        marked.setOptions({
          gfm: true,
          breaks: true,
          highlight(code, lang) {
            if (window.hljs) {
              try {
                if (lang && hljs.getLanguage(lang)) {
                  return hljs.highlight(code, { language: lang }).value;
                }
                return hljs.highlightAuto(code).value;
              } catch (_e) {
                return esc(code);
              }
            }
            return esc(code);
          }
        });
        return marked.parse(text);
      }

      return `<pre>${esc(text)}</pre>`;
    }

)HTML"
R"HTML(    function typeIcon(type) {
      const map = {
        Theme: '🧩',
        Epic: '👑',
        Feature: '🔷',
        UserStory: '📖',
        Task: '✅',
        Bug: '🐞',
        Issue: '!'
      };
      return map[type] || '•';
    }

    function pill(status) {
      const value = String(status || 'unknown');
      const cls = value === 'passed' ? 'passed' :
        value === 'failed' ? 'failed' :
        value === 'blocked' ? 'blocked' :
        (value === 'not-run' || value === 'unknown' ? 'missing' : '');
      return `<span class="pill ${cls}">${esc(value)}</span>`;
    }

    function renderItemCard(item) {
      return `<div class="card"><div><code>${esc(item.id || '')}</code></div><div><a href="#" class="item-link" data-item-id="${escAttr(item.id || '')}" data-item-product="${escAttr(item.product || '')}">${esc(item.title || item.id || '')}</a></div><div class="muted">${esc(renderMeta(item))}</div></div>`;
    }

    function bindItemLinks(selector) {
      document.querySelectorAll(`${selector} .item-link[data-item-id]`).forEach((link) => {
        link.addEventListener('click', async (event) => {
          event.preventDefault();
          const id = link.getAttribute('data-item-id');
          const product = link.getAttribute('data-item-product') || '';
          if (!id) return;
          await openItemModal(id, product);
        });
      });
    }

    function renderEvidenceMatrix(evidence) {
      const matrix = evidence?.validation_matrix || [];
      const rows = matrix.map((check) =>
        `<div class="evidence-row"><strong>${esc(check.name || '')}</strong><span>${pill(check.status)}</span><span class="muted">${esc(check.evidence || '')}</span></div>`
      ).join('');
      const missing = (evidence?.missing || []).map((x) => `<span class="pill missing">${esc(x)}</span>`).join(' ');
      return `<div class="panel" style="margin:0 0 12px 0;"><h4>Evidence</h4><div class="muted">Score: ${esc(String(evidence?.score || 0))} ${missing ? `| Missing: ${missing}` : ''}</div>${rows || '<div class="muted">No validation matrix</div>'}</div>`;
    }

    function renderTimeline(events) {
      const rows = (events || []).slice(0, 12).map((event) =>
        `<div class="card"><div><code>${esc(event.timestamp || '')}</code> ${pill(event.kind || 'worklog')}</div><div>${esc(event.text || '')}</div><div class="muted">${esc(event.agent || 'unknown')}</div></div>`
      ).join('');
      return `<div class="panel" style="margin:0 0 12px 0;"><h4>Timeline</h4>${rows || '<div class="muted">No work-order timeline events</div>'}</div>`;
    }

    function renderTreeNode(node, depth = 0) {
      const children = node.children || [];
      const nodeKey = treeNodeKey(node);
      const label = `<span class="node-line"><span>${typeIcon(node.type)}</span><code>${esc(node.id)}</code><a href="#" class="item-link" data-item-id="${escAttr(node.id)}" data-item-product="${escAttr(node.product || '')}">${esc(node.title)}</a><span class="muted">(${esc(renderMeta(node))})</span></span>`;
      if (!children.length) {
        return `<li><span class="leaf-spacer"></span>${label}</li>`;
      }
      const isOpen = state.treeOpen.has(nodeKey) || depth <= 1 ? 'open' : '';
      return `<li><details data-node-key="${escAttr(nodeKey)}" ${isOpen}><summary>${label}</summary><ul>${children.map((child) => renderTreeNode(child, depth + 1)).join('')}</ul></details></li>`;
    }

    function bindTreeToggles() {
      document.querySelectorAll('#tree details[data-node-key]').forEach((detail) => {
        detail.addEventListener('toggle', () => {
          const key = detail.getAttribute('data-node-key');
          if (!key) return;
          state.treeTouched = true;
          if (detail.open) {
            state.treeOpen.add(key);
          } else {
            state.treeOpen.delete(key);
          }
        });
      });

      document.querySelectorAll('#tree .item-link[data-item-id]').forEach((link) => {
        link.addEventListener('click', async (event) => {
          event.preventDefault();
          event.stopPropagation();
          const id = link.getAttribute('data-item-id');
          const product = link.getAttribute('data-item-product') || '';
          if (!id) return;
          await openItemModal(id, product);
        });
      });
    }

    function openModal(title, bodyHtml) {
      document.getElementById('item-modal-title').textContent = title;
      document.getElementById('item-modal-body').innerHTML = bodyHtml;
      document.getElementById('item-modal-backdrop').classList.add('open');
    }

    function closeModal() {
      document.getElementById('item-modal-backdrop').classList.remove('open');
    }

    async function openItemModal(itemId, product = '') {
      const productScope = product || (selectedProductValues().length === 1 ? selectedProductValues()[0] : 'all');
      const data = await getJson(`/api/items/${encodeURIComponent(itemId)}?product=${encodeURIComponent(productScope)}`);
      const item = data?.data?.item;
      if (!item) {
        openModal(itemId, '<div class="muted">Item not found.</div>');
        return;
      }
      const evidenceResult = await getJson(`/api/review/evidence/${encodeURIComponent(itemId)}?product=${encodeURIComponent(item.product || productScope)}`);
      const timelineResult = await getJson(`/api/review/timeline?item=${encodeURIComponent(itemId)}&product=${encodeURIComponent(item.product || productScope)}&limit=200`);
      const evidence = evidenceResult?.data?.evidence || {};
      const timeline = timelineResult?.data?.events || [];
      const contentHtml = renderMarkdownWithObsidian(item.content || '(no content)');
      const body = `<div class="row"><code>${esc(item.id)}</code><span class="muted">${esc(renderMeta(item))}</span></div><div class="muted" style="margin-bottom:8px;">${esc(item.path || '')}</div>${renderEvidenceMatrix(evidence)}${renderTimeline(timeline)}<div class="md-view">${contentHtml}</div>`;
      openModal(item.title || item.id, body);

      document.querySelectorAll('#item-modal-body .obs-wikilink[data-item-id]').forEach((link) => {
        link.addEventListener('click', async (event) => {
          event.preventDefault();
          const target = link.getAttribute('data-item-id');
          if (!target) return;
          await openItemModal(target, item.product || product);
        });
      });

      if (window.hljs) {
        document.querySelectorAll('#item-modal-body pre code').forEach((block) => {
          try {
            hljs.highlightElement(block);
          } catch (_e) {
          }
        });
      }
    }

)HTML"
R"HTML(
    function syncProductSelect() {
      const select = document.getElementById('product');
      const products = selectedProductValues();
      const customValue = '__selected__';
      const custom = select.querySelector(`option[value="${customValue}"]`);
      if (products.length > 1) {
        if (!custom) {
          const opt = document.createElement('option');
          opt.value = customValue;
          select.insertBefore(opt, select.firstChild?.nextSibling || null);
        }
        select.querySelector(`option[value="${customValue}"]`).textContent =
            `${products.length} selected products`;
        select.value = customValue;
      } else {
        if (custom) {
          custom.remove();
        }
        select.value = products[0] || 'all';
      }
      state.product = products.length === 1 ? products[0] : customValue;
    }

    function renderProductFilters() {
      const allChecked = state.selectedProducts.has('all');
      const entries = [
        { value: 'all', label: 'All products', checked: allChecked },
        ...state.products.map((product) => ({
          value: product,
          label: product,
          checked: !allChecked && state.selectedProducts.has(product),
        })),
      ];
      document.getElementById('product-filters').innerHTML = entries.map((entry) =>
        `<label><input type="checkbox" data-filter-product="${escAttr(entry.value)}" ${entry.checked ? 'checked' : ''} /> ${esc(entry.label)}</label>`
      ).join('');
      syncProductSelect();
    }

    function renderTokenFilters(containerId, values, selected, attrName) {
      document.getElementById(containerId).innerHTML = values.map((value) =>
        `<label><input type="checkbox" ${attrName}="${escAttr(value)}" ${selected.has(value) ? 'checked' : ''} /> ${esc(value)}</label>`
      ).join('');
    }

    function renderFilters() {
      renderProductFilters();
      renderTokenFilters('state-filters', itemStates, state.selectedStates, 'data-filter-state');
      renderTokenFilters('type-filters', itemTypes, state.selectedTypes, 'data-filter-type');
    }

    async function loadProducts() {
      const result = await getJson('/api/products');
      const select = document.getElementById('product');
      state.products = result.data || [];
      select.innerHTML = '';
      const allOpt = document.createElement('option');
      allOpt.value = 'all';
      allOpt.textContent = 'All products';
      select.appendChild(allOpt);
      for (const product of state.products) {
        const opt = document.createElement('option');
        opt.value = product;
        opt.textContent = product;
        select.appendChild(opt);
      }
      const currentProducts = selectedProductValues();
      const validSelection = currentProducts.length === 1 &&
          (currentProducts[0] === 'all' || state.products.includes(currentProducts[0]));
      if (!validSelection) {
        state.selectedProducts = new Set(['all']);
      }
      renderFilters();
    }

    async function loadTree() {
      const result = await getJson(`/api/tree?${queryString()}`);
      const roots = result?.data?.roots || [];
      if (!state.treeTouched && state.treeOpen.size === 0) {
        for (const root of roots) {
          if (root.id) state.treeOpen.add(treeNodeKey(root));
        }
      }
      document.getElementById('tree').innerHTML = `<ul>${roots.map((node) => renderTreeNode(node, 0)).join('')}</ul>`;
      bindTreeToggles();
    }

    async function loadKanban() {
      const result = await getJson(`/api/kanban?${queryString()}`);
      const lanesData = result?.data?.lanes || {};
      const html = lanes.map((lane) => {
        const cards = (lanesData[lane] || [])
          .map((item) =>
          `<div class="card"><div><code>${esc(item.id)}</code></div><div><a href="#" class="item-link" data-item-id="${escAttr(item.id)}" data-item-product="${escAttr(item.product || '')}">${esc(item.title)}</a></div><div class="muted">${esc(renderMeta(item))}</div></div>`
          ).join('');
        return `<div class="lane"><strong>${lane}</strong>${cards || '<div class="muted">No items</div>'}</div>`;
      }).join('');
      document.getElementById('kanban').innerHTML = html;
      document.querySelectorAll('#kanban .item-link[data-item-id]').forEach((link) => {
        link.addEventListener('click', async (event) => {
          event.preventDefault();
          const id = link.getAttribute('data-item-id');
          const product = link.getAttribute('data-item-product') || '';
          if (!id) return;
          await openItemModal(id, product);
        });
      });
    }

    async function loadContext() {
      const result = await getJson(`/api/items?${queryString()}`);
      const items = (result?.data?.items || []);
      state.allItems = items;
      const contextItems = items.filter((item) => {
        const t = item.type;
        return t === 'ADR' || t === 'Topic' || t === 'Workset';
      });

      const counts = contextItems.reduce((acc, item) => {
        acc[item.type] = (acc[item.type] || 0) + 1;
        return acc;
      }, {});

      document.getElementById('context-summary').textContent =
          `ADR: ${counts.ADR || 0} | Topic: ${counts.Topic || 0} | Workset: ${counts.Workset || 0}`;

      const listHtml = contextItems.map((item) =>
        `<div class="card"><div><code>${esc(item.id)}</code></div><div><a href="#" class="item-link" data-item-id="${escAttr(item.id)}" data-item-product="${escAttr(item.product || '')}">${esc(item.title)}</a></div><div class="muted">${esc(renderMeta(item))}</div></div>`
      ).join('');
      document.getElementById('context-list').innerHTML = listHtml || '<div class="muted">No context items</div>';

      document.querySelectorAll('#context-list .item-link[data-item-id]').forEach((link) => {
        link.addEventListener('click', async (event) => {
          event.preventDefault();
          const id = link.getAttribute('data-item-id');
          const product = link.getAttribute('data-item-product') || '';
          if (!id) return;
          await openItemModal(id, product);
        });
      });
    }

    async function loadReview() {
      const viewsResult = await getJson('/api/review/saved-views');
      const views = viewsResult?.data?.views || [];
      document.getElementById('saved-views').innerHTML = views.map((view) =>
        `<button class="btn" data-saved-view="${escAttr(view.id)}">${esc(view.title)}</button>`
      ).join('');

      const inboxResult = await getJson(`/api/review/inbox?${queryString()}`);
      const lanesData = inboxResult?.data?.lanes || {};
      const laneNames = Object.keys(lanesData);
      document.getElementById('review-inbox').innerHTML = laneNames.map((lane) => {
        const bundles = lanesData[lane] || [];
        const cards = bundles.map((bundle) => renderItemCard(bundle.item || {})).join('');
        return `<div class="review-lane"><strong>${esc(lane)}</strong><div class="muted">${bundles.length} item(s)</div>${cards || '<div class="muted">No items</div>'}</div>`;
      }).join('');
      bindItemLinks('#review-inbox');

      document.querySelectorAll('#saved-views [data-saved-view]').forEach((button) => {
        button.addEventListener('click', async () => {
          const viewId = button.getAttribute('data-saved-view');
          const result = await getJson(`/api/review/saved-views/${encodeURIComponent(viewId)}?${queryString()}`);
          const items = result?.data?.result?.items || [];
          document.getElementById('review-inbox').innerHTML =
            `<div class="review-lane"><strong>${esc(result?.data?.view?.title || viewId)}</strong><div class="muted">${items.length} item(s)</div>${items.map(renderItemCard).join('') || '<div class="muted">No items</div>'}</div>`;
          bindItemLinks('#review-inbox');
        });
      });
    }

)HTML"
R"HTML(
    function graphEdgeClass(kind) {
      return String(kind || '').replace(/[^A-Za-z0-9_-]/g, '-');
    }

    function graphNodeClass(node, incomingDependency) {
      const classes = ['graph-node'];
      if (String(node.id || '').startsWith('topic:')) classes.push('topic');
      if (node.missing || node.kind === 'Missing') classes.push('missing');
      if (incomingDependency) classes.push('dependency');
      return classes.join(' ');
    }

    function shortGraphLabel(value, max = 28) {
      const text = String(value || '');
      return text.length > max ? `${text.slice(0, max - 1)}...` : text;
    }

    function renderGraphSvg(nodes, edges) {
      if (!nodes.length) {
        return '<div class="muted">No graph nodes for the current filters.</div>';
      }

      const incomingDependency = new Set(edges
        .filter((edge) => edge.dependency)
        .map((edge) => edge.to));
      const byId = new Map(nodes.map((node) => [node.id, node]));
      const sortedNodes = [...nodes].sort((a, b) => {
        const layerA = Number(a.visual_layer ?? 1);
        const layerB = Number(b.visual_layer ?? 1);
        if (layerA !== layerB) return layerA - layerB;
        return String(a.id || '').localeCompare(String(b.id || ''));
      });

      const layerCounts = new Map();
      const positioned = new Map();
      const nodeW = 180;
      const nodeH = 54;
      const layerGap = 250;
      const rowGap = 86;
      const margin = 28;
      for (const node of sortedNodes) {
        const layer = Number(node.visual_layer ?? 1);
        const row = layerCounts.get(layer) || 0;
        layerCounts.set(layer, row + 1);
        positioned.set(node.id, {
          node,
          x: margin + layer * layerGap,
          y: margin + row * rowGap,
          w: nodeW,
          h: nodeH,
        });
      }

      const maxLayer = Math.max(0, ...[...layerCounts.keys()]);
      const maxRows = Math.max(1, ...[...layerCounts.values()]);
      const width = margin * 2 + (maxLayer + 1) * layerGap + nodeW;
      const height = margin * 2 + maxRows * rowGap + nodeH;

      const edgeMarkup = edges.slice(0, 160).map((edge, index) => {
        const from = positioned.get(edge.from);
        const to = positioned.get(edge.to);
        if (!from || !to) return '';
        const x1 = from.x + from.w;
        const y1 = from.y + from.h / 2;
        const x2 = to.x;
        const y2 = to.y + to.h / 2;
        const midX = (x1 + x2) / 2;
        const c1x = x1 + Math.max(40, Math.abs(x2 - x1) / 3);
        const c2x = x2 - Math.max(40, Math.abs(x2 - x1) / 3);
        const labelY = (y1 + y2) / 2 - 5 - (index % 3) * 12;
        return `<path class="graph-edge ${escAttr(graphEdgeClass(edge.kind))}" d="M ${x1} ${y1} C ${c1x} ${y1}, ${c2x} ${y2}, ${x2} ${y2}" marker-end="url(#graph-arrow)" />` +
          `<text class="graph-edge-label" x="${midX}" y="${labelY}" text-anchor="middle">${esc(edge.kind || '')}</text>`;
      }).join('');

      const nodeMarkup = sortedNodes.map((node) => {
        const pos = positioned.get(node.id);
        const meta = [node.product, node.kind, node.state].filter(Boolean).join(' / ');
        return `<g class="${escAttr(graphNodeClass(node, incomingDependency.has(node.id)))}" transform="translate(${pos.x}, ${pos.y})">` +
          `<rect width="${pos.w}" height="${pos.h}"></rect>` +
          `<text class="graph-label" x="10" y="20">${esc(shortGraphLabel(node.item_id || node.id, 24))}</text>` +
          `<text class="graph-meta" x="10" y="38">${esc(shortGraphLabel(node.label || '', 30))}</text>` +
          `<title>${esc(`${node.id || ''} ${meta}`)}</title>` +
        `</g>`;
      }).join('');

      return `<div class="graph-canvas"><svg class="graph-svg" viewBox="0 0 ${width} ${height}" role="img" aria-label="Dependency graph visualization">` +
        `<defs><marker id="graph-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M 0 0 L 10 5 L 0 10 z" fill="#6c778d"></path></marker></defs>` +
        edgeMarkup + nodeMarkup +
      `</svg></div>`;
    }

    async function loadGraph() {
      const result = await getJson(`/api/review/graph?${queryString()}`);
      const data = result?.data || {};
      const nodes = data.nodes || [];
      const edges = data.edges || [];
      const missing = data.missing_nodes || [];
      const truncated = data.truncated ? ' truncated' : '';
      document.getElementById('graph-summary').textContent =
        `${nodes.length} node(s), ${edges.length} edge(s), ${missing.length} missing node(s)${truncated}`;

      const diagnostics = [
        ...missing.slice(0, 20).map((node) =>
          `<div class="card"><strong>Missing</strong> <code>${esc(node.id || node.ref || '')}</code><div class="muted">${esc(node.kind || '')} from ${esc(node.source || '')}</div></div>`
        ),
        ...((data.invalid_refs || []).slice(0, 20).map((ref) =>
          `<div class="card"><strong>Invalid ref</strong> <code>${esc(ref.ref || '')}</code><div class="muted">${esc(ref.kind || '')} from ${esc(ref.source || '')}</div></div>`
        )),
        ...((data.dependency_cycles || []).slice(0, 10).map((cycle) =>
          `<div class="card"><strong>Dependency cycle</strong><div class="muted">${esc((cycle || []).join(' -> '))}</div></div>`
        )),
      ].join('');

      document.getElementById('graph-list').innerHTML = [
        renderGraphSvg(nodes, edges),
        diagnostics ? `<div class="graph-diagnostics">${diagnostics}</div>` : '',
        `<h4>Edge details</h4>`,
        ...(edges.slice(0, 120).map((edge) =>
          `<div class="card"><code>${esc(edge.from || '')}</code> -> <code>${esc(edge.to || '')}</code><div class="muted">${esc(edge.kind || '')} / ${esc(edge.semantic || '')}</div></div>`
        )),
        `<h4>Node details</h4>`,
        ...(nodes.slice(0, 120).map((node) =>
          `<div class="card"><code>${esc(node.id || '')}</code><div>${esc(node.label || '')}</div><div class="muted">${esc(node.kind || '')} ${esc(node.state || '')}</div></div>`
        )),
      ].join('');
    }

    async function loadRuns() {
      const result = await getJson(`/api/review/runs?${queryString()}`);
      const runs = result?.data?.runs || [];
      document.getElementById('runs-summary').textContent = `${runs.length} run(s)`;
      document.getElementById('runs-list').innerHTML = runs.map((run) =>
        `<div class="card"><div><code>${esc(run.item_id || '')}</code> ${pill(run.state)}</div><div>${esc(run.agent || '')}</div><div class="muted">${esc(run.latest_event?.text || '')}</div></div>`
      ).join('') || '<div class="muted">No run records</div>';
    }

    async function loadCommandPreview() {
      const input = document.getElementById('command-input').value.trim();
      if (!input) {
        document.getElementById('command-result').innerHTML = '<div class="muted">Enter a supported read-only command.</div>';
        return;
      }
      const result = await getJson(`/api/review/command-preview?q=${encodeURIComponent(input)}&${queryString()}`);
      if (!result.ok) {
        document.getElementById('command-result').innerHTML = `<div class="muted">${esc(result?.data?.error || 'Unsupported command')}</div>`;
        return;
      }
      const data = result.data || {};
      const items = data.preview?.items || [];
      document.getElementById('command-result').innerHTML =
        `<div class="card"><strong>KOBQL</strong><div><code>${esc(data.generated_kobql || '')}</code></div><div class="muted">Read-only preview, ${items.length} visible item(s)</div></div>${items.map(renderItemCard).join('')}`;
      bindItemLinks('#command-result');
    }

    function setActiveTab(tab) {
      state.activeTab = tab;
      const isTree = tab === 'tree';
      const isKanban = tab === 'kanban';
      const isContext = tab === 'context';
      const isReview = tab === 'review';
      const isGraph = tab === 'graph';
      const isRuns = tab === 'runs';
      const isCommand = tab === 'command';
      document.getElementById('tab-tree').classList.toggle('active', isTree);
      document.getElementById('tab-kanban').classList.toggle('active', isKanban);
      document.getElementById('tab-context').classList.toggle('active', isContext);
      document.getElementById('tab-review').classList.toggle('active', isReview);
      document.getElementById('tab-graph').classList.toggle('active', isGraph);
      document.getElementById('tab-runs').classList.toggle('active', isRuns);
      document.getElementById('tab-command').classList.toggle('active', isCommand);
      document.getElementById('page-tree').classList.toggle('active', isTree);
      document.getElementById('page-kanban').classList.toggle('active', isKanban);
      document.getElementById('page-context').classList.toggle('active', isContext);
      document.getElementById('page-review').classList.toggle('active', isReview);
      document.getElementById('page-graph').classList.toggle('active', isGraph);
      document.getElementById('page-runs').classList.toggle('active', isRuns);
      document.getElementById('page-command').classList.toggle('active', isCommand);
    }

)HTML"
R"HTML(
    async function refreshAll() {
      if (selectedProductValues().length === 0) {
        document.getElementById('status').textContent = 'No product found';
        return;
      }
      document.getElementById('status').textContent = 'Loading...';
      await Promise.all([loadTree(), loadKanban(), loadContext(), loadReview(), loadGraph(), loadRuns()]);
      document.getElementById('status').textContent = `Loaded ${productScopeLabel()}`;
    }

    document.getElementById('product').addEventListener('change', async (e) => {
      const value = e.target.value;
      if (value === '__selected__') {
        return;
      }
      state.selectedProducts = new Set([value || 'all']);
      renderProductFilters();
      state.treeOpen.clear();
      state.treeTouched = false;
      await refreshAll();
    });

    document.getElementById('search').addEventListener('input', async (e) => {
      state.q = e.target.value.trim();
      state.treeTouched = false;
      state.treeOpen.clear();
      await refreshAll();
    });

    document.getElementById('workspace-add').addEventListener('click', async () => {
      await switchWorkspace(document.getElementById('workspace-input').value);
    });

    document.getElementById('workspace-input').addEventListener('keydown', async (event) => {
      if (event.key === 'Enter') {
        event.preventDefault();
        await switchWorkspace(document.getElementById('workspace-input').value);
      }
    });

    document.addEventListener('keydown', async (event) => {
      if (!event.altKey || event.ctrlKey || event.metaKey || event.shiftKey) {
        return;
      }
      const idx = Number.parseInt(event.key, 10);
      if (!Number.isFinite(idx) || idx < 1 || idx > 9) {
        return;
      }
      const list = sortedWorkspaces();
      const target = list[idx - 1];
      if (!target) {
        return;
      }
      event.preventDefault();
      await switchWorkspace(target.path, true);
    });

    document.querySelectorAll('.tab-btn[data-tab]').forEach((btn) => {
      btn.addEventListener('click', () => {
        setActiveTab(btn.getAttribute('data-tab') || 'tree');
      });
    });

    document.getElementById('product-filters').addEventListener('change', async (event) => {
      const checkbox = event.target;
      if (!checkbox?.matches?.('input[data-filter-product]')) {
        return;
      }
      const value = checkbox.getAttribute('data-filter-product');
      if (value === 'all') {
        state.selectedProducts = new Set(['all']);
      } else {
        const next = new Set([...state.selectedProducts].filter((p) => p !== 'all'));
        if (checkbox.checked) {
          next.add(value);
        } else {
          next.delete(value);
        }
        state.selectedProducts = next.size ? next : new Set(['all']);
      }
      renderProductFilters();
      state.treeOpen.clear();
      state.treeTouched = false;
      await refreshAll();
    });

    document.getElementById('state-filters').addEventListener('change', async (event) => {
      const checkbox = event.target;
      if (!checkbox?.matches?.('input[data-filter-state]')) {
        return;
      }
      const value = checkbox.getAttribute('data-filter-state');
      if (checkbox.checked) {
        state.selectedStates.add(value);
      } else {
        state.selectedStates.delete(value);
      }
      state.treeOpen.clear();
      state.treeTouched = false;
      await refreshAll();
    });

    document.getElementById('type-filters').addEventListener('change', async (event) => {
      const checkbox = event.target;
      if (!checkbox?.matches?.('input[data-filter-type]')) {
        return;
      }
      const value = checkbox.getAttribute('data-filter-type');
      if (checkbox.checked) {
        state.selectedTypes.add(value);
      } else {
        state.selectedTypes.delete(value);
      }
      state.treeOpen.clear();
      state.treeTouched = false;
      await refreshAll();
    });

    document.getElementById('limit').addEventListener('change', async (event) => {
      const parsed = Number.parseInt(event.target.value, 10);
      state.limit = Number.isFinite(parsed) ? Math.max(1, Math.min(parsed, 1000)) : 200;
      event.target.value = String(state.limit);
      await refreshAll();
    });

    document.getElementById('refresh').addEventListener('click', async () => {
      const products = selectedProductValues();
      const refreshScope = products.length === 1 ? products[0] : 'all';
      await getJson(`/api/refresh?product=${encodeURIComponent(refreshScope)}`);
      await refreshAll();
    });

    document.getElementById('command-preview').addEventListener('click', loadCommandPreview);
    document.getElementById('command-input').addEventListener('keydown', async (event) => {
      if (event.key === 'Enter') {
        event.preventDefault();
        await loadCommandPreview();
      }
    });

    document.getElementById('expand-all').addEventListener('click', () => {
      document.querySelectorAll('#tree details[data-node-key]').forEach((detail) => {
        detail.open = true;
        const key = detail.getAttribute('data-node-key');
        if (key) state.treeOpen.add(key);
      });
      state.treeTouched = true;
    });

    document.getElementById('collapse-all').addEventListener('click', () => {
      document.querySelectorAll('#tree details[data-node-key]').forEach((detail) => {
        detail.open = false;
      });
      state.treeOpen.clear();
      state.treeTouched = true;
    });

    document.getElementById('item-modal-close').addEventListener('click', closeModal);
    document.getElementById('item-modal-backdrop').addEventListener('click', (event) => {
      if (event.target.id === 'item-modal-backdrop') {
        closeModal();
      }
    });

    (async () => {
      setActiveTab('tree');
      state.workspaces = loadSavedWorkspaces();
      renderWorkspaceList();
      await loadWorkspaceInfo();
      document.getElementById('workspace-input').value = state.workspace || '';
      await loadProducts();
      await refreshAll();
    })();
  </script>
</body>
</html>
)HTML";

}  // namespace

int main(int argc, char** argv) {
  kano::backlog_core::ConfigureNoninteractiveErrorHandling();

  const auto productsRoot = ResolveProductsRoot(argc, argv);
  const auto host = ResolveHost(argc, argv);
  const auto port = ResolvePort(argc, argv);

  kano::backlog::webview::BacklogWebviewService service(productsRoot);

  auto appendMeta = [&](const drogon::HttpRequestPtr&, Json::Value& body) {
    body["meta"]["products_root"] = service.GetProductsRoot().generic_string();
  };

  kano::backlog::webview::RegisterBacklogWebviewRoutes(service, appendMeta);

  drogon::app().registerHandler(
      "/",
      [](const drogon::HttpRequestPtr&,
         std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto response = drogon::HttpResponse::newHttpResponse();
        response->setStatusCode(drogon::k200OK);
        response->setContentTypeCode(drogon::CT_TEXT_HTML);
        response->setBody(kIndexHtml);
        callback(response);
      },
      {drogon::Get});

  drogon::app().setLogLevel(trantor::Logger::kWarn);
  drogon::app().setThreadNum(1);
  drogon::app().addListener(host, port);
  drogon::app().run();
  return 0;
}
