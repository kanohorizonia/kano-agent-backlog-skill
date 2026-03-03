#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

#include <drogon/drogon.h>

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
    .tree ul { list-style: none; padding-left: 18px; margin: 0; }
    .tree li { margin: 2px 0; }
    .tree details { margin: 2px 0; }
    .tree summary { cursor: pointer; }
    .tree summary::marker { color: #5a6d8f; }
    .tree .node-line { display: inline-flex; gap: 6px; align-items: center; }
    .tree .leaf-spacer { display: inline-block; width: 12px; }
    .btn { border: 1px solid #cfd9ea; background: #fff; border-radius: 6px; padding: 4px 10px; cursor: pointer; }
    .btn:hover { background: #f2f6ff; }
    .filters { display: flex; gap: 10px; flex-wrap: wrap; margin: 8px 0 10px 0; }
    .filters label { display: inline-flex; gap: 6px; align-items: center; font-size: 13px; }
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
    <label for="product">Product:</label>
    <select id="product"></select>
    <input id="search" placeholder="Search id/title" />
    <button id="refresh">Refresh</button>
    <span id="status" class="muted"></span>
  </div>

  <div class="panel">
    <div class="tabs">
      <button id="tab-tree" class="tab-btn active" data-tab="tree">Tree</button>
      <button id="tab-kanban" class="tab-btn" data-tab="kanban">Kanban</button>
      <button id="tab-context" class="tab-btn" data-tab="context">Context</button>
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
      <div class="filters" id="kanban-type-filters">
        <label><input type="checkbox" value="Epic" checked /> Epic</label>
        <label><input type="checkbox" value="Feature" checked /> Feature</label>
        <label><input type="checkbox" value="UserStory" checked /> UserStory</label>
        <label><input type="checkbox" value="Task" checked /> Task</label>
      </div>
      <div id="kanban" class="kanban"></div>
  </div>

  <div id="page-context" class="panel page">
    <h3>Context (ADR / Topic / Workset)</h3>
    <div id="context-summary" class="muted" style="margin-bottom:8px;"></div>
    <div id="context-list"></div>
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
      product: '',
      q: '',
      workspace: '',
      workspaces: [],
      treeOpen: new Set(),
      treeTouched: false,
      activeTab: 'tree',
      kanbanTypes: new Set(['Epic', 'Feature', 'UserStory', 'Task']),
      allItems: []
    };
    const lanes = ['Backlog', 'Doing', 'Blocked', 'Review', 'Done'];
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
        Bug: '🐞'
      };
      return map[type] || '•';
    }

    function renderTreeNode(node, depth = 0) {
      const children = node.children || [];
      const label = `<span class="node-line"><span>${typeIcon(node.type)}</span><code>${esc(node.id)}</code><a href="#" class="item-link" data-item-id="${escAttr(node.id)}">${esc(node.title)}</a><span class="muted">(${esc(node.type)} / ${esc(node.state)})</span></span>`;
      if (!children.length) {
        return `<li><span class="leaf-spacer"></span>${label}</li>`;
      }
      const isOpen = state.treeOpen.has(node.id) || depth <= 1 ? 'open' : '';
      return `<li><details data-node-id="${escAttr(node.id)}" ${isOpen}><summary>${label}</summary><ul>${children.map((child) => renderTreeNode(child, depth + 1)).join('')}</ul></details></li>`;
    }

    function bindTreeToggles() {
      document.querySelectorAll('#tree details[data-node-id]').forEach((detail) => {
        detail.addEventListener('toggle', () => {
          const id = detail.getAttribute('data-node-id');
          if (!id) return;
          state.treeTouched = true;
          if (detail.open) {
            state.treeOpen.add(id);
          } else {
            state.treeOpen.delete(id);
          }
        });
      });

      document.querySelectorAll('#tree .item-link[data-item-id]').forEach((link) => {
        link.addEventListener('click', async (event) => {
          event.preventDefault();
          event.stopPropagation();
          const id = link.getAttribute('data-item-id');
          if (!id) return;
          await openItemModal(id);
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

    async function openItemModal(itemId) {
      const data = await getJson(`/api/items/${encodeURIComponent(itemId)}?product=${encodeURIComponent(state.product)}`);
      const item = data?.data?.item;
      if (!item) {
        openModal(itemId, '<div class="muted">Item not found.</div>');
        return;
      }
      const contentHtml = renderMarkdownWithObsidian(item.content || '(no content)');
      const body = `<div class="row"><code>${esc(item.id)}</code><span class="muted">${esc(item.type)} / ${esc(item.state)} / ${esc(item.source_kind || '')}</span></div><div class="muted" style="margin-bottom:8px;">${esc(item.path || '')}</div><div class="md-view">${contentHtml}</div>`;
      openModal(item.title || item.id, body);

      document.querySelectorAll('#item-modal-body .obs-wikilink[data-item-id]').forEach((link) => {
        link.addEventListener('click', async (event) => {
          event.preventDefault();
          const target = link.getAttribute('data-item-id');
          if (!target) return;
          await openItemModal(target);
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

    async function loadProducts() {
      const result = await getJson('/api/products');
      const select = document.getElementById('product');
      select.innerHTML = '';
      for (const product of (result.data || [])) {
        const opt = document.createElement('option');
        opt.value = product;
        opt.textContent = product;
        select.appendChild(opt);
      }
      state.product = select.value || '';
    }

    async function loadTree() {
      const result = await getJson(`/api/tree?product=${encodeURIComponent(state.product)}`);
      const roots = result?.data?.roots || [];
      if (!state.treeTouched && state.treeOpen.size === 0) {
        for (const root of roots) {
          if (root.id) state.treeOpen.add(root.id);
        }
      }
      document.getElementById('tree').innerHTML = `<ul>${roots.map((node) => renderTreeNode(node, 0)).join('')}</ul>`;
      bindTreeToggles();
    }

    async function loadKanban() {
      const q = state.q ? `&q=${encodeURIComponent(state.q)}` : '';
      const result = await getJson(`/api/kanban?product=${encodeURIComponent(state.product)}${q}`);
      const lanesData = result?.data?.lanes || {};
      const html = lanes.map((lane) => {
        const cards = (lanesData[lane] || [])
          .filter((item) => state.kanbanTypes.has(item.type))
          .map((item) =>
          `<div class="card"><div><code>${esc(item.id)}</code></div><div><a href="#" class="item-link" data-item-id="${escAttr(item.id)}">${esc(item.title)}</a></div><div class="muted">${esc(item.type)} / ${esc(item.state)} / ${esc(item.source_kind || '')}</div></div>`
          ).join('');
        return `<div class="lane"><strong>${lane}</strong>${cards || '<div class="muted">No items</div>'}</div>`;
      }).join('');
      document.getElementById('kanban').innerHTML = html;
      document.querySelectorAll('#kanban .item-link[data-item-id]').forEach((link) => {
        link.addEventListener('click', async (event) => {
          event.preventDefault();
          const id = link.getAttribute('data-item-id');
          if (!id) return;
          await openItemModal(id);
        });
      });
    }

    async function loadContext() {
      const q = state.q ? `&q=${encodeURIComponent(state.q)}` : '';
      const result = await getJson(`/api/items?product=${encodeURIComponent(state.product)}${q}`);
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
        `<div class="card"><div><code>${esc(item.id)}</code></div><div><a href="#" class="item-link" data-item-id="${escAttr(item.id)}">${esc(item.title)}</a></div><div class="muted">${esc(item.type)} / ${esc(item.state)} / ${esc(item.source_kind || '')}</div></div>`
      ).join('');
      document.getElementById('context-list').innerHTML = listHtml || '<div class="muted">No context items</div>';

      document.querySelectorAll('#context-list .item-link[data-item-id]').forEach((link) => {
        link.addEventListener('click', async (event) => {
          event.preventDefault();
          const id = link.getAttribute('data-item-id');
          if (!id) return;
          await openItemModal(id);
        });
      });
    }

    function setActiveTab(tab) {
      state.activeTab = tab;
      const isTree = tab === 'tree';
      const isKanban = tab === 'kanban';
      const isContext = tab === 'context';
      document.getElementById('tab-tree').classList.toggle('active', isTree);
      document.getElementById('tab-kanban').classList.toggle('active', isKanban);
      document.getElementById('tab-context').classList.toggle('active', isContext);
      document.getElementById('page-tree').classList.toggle('active', isTree);
      document.getElementById('page-kanban').classList.toggle('active', isKanban);
      document.getElementById('page-context').classList.toggle('active', isContext);
    }

    async function refreshAll() {
      if (!state.product) {
        document.getElementById('status').textContent = 'No product found';
        return;
      }
      document.getElementById('status').textContent = 'Loading...';
      await Promise.all([loadTree(), loadKanban(), loadContext()]);
      document.getElementById('status').textContent = `Loaded ${state.product}`;
    }

    document.getElementById('product').addEventListener('change', async (e) => {
      state.product = e.target.value;
      state.treeOpen.clear();
      state.treeTouched = false;
      await refreshAll();
    });

    document.getElementById('search').addEventListener('input', async (e) => {
      state.q = e.target.value.trim();
      await Promise.all([loadKanban(), loadContext()]);
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

    document.querySelectorAll('#kanban-type-filters input[type="checkbox"]').forEach((checkbox) => {
      checkbox.addEventListener('change', async () => {
        const type = checkbox.value;
        if (checkbox.checked) {
          state.kanbanTypes.add(type);
        } else {
          state.kanbanTypes.delete(type);
        }
        await loadKanban();
      });
    });

    document.getElementById('refresh').addEventListener('click', async () => {
      if (!state.product) return;
      await getJson(`/api/refresh?product=${encodeURIComponent(state.product)}`);
      await refreshAll();
    });

    document.getElementById('expand-all').addEventListener('click', () => {
      document.querySelectorAll('#tree details[data-node-id]').forEach((detail) => {
        detail.open = true;
        const id = detail.getAttribute('data-node-id');
        if (id) state.treeOpen.add(id);
      });
      state.treeTouched = true;
    });

    document.getElementById('collapse-all').addEventListener('click', () => {
      document.querySelectorAll('#tree details[data-node-id]').forEach((detail) => {
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
  const auto productsRoot = ResolveProductsRoot(argc, argv);
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
  drogon::app().addListener("127.0.0.1", port);
  drogon::app().run();
  return 0;
}
