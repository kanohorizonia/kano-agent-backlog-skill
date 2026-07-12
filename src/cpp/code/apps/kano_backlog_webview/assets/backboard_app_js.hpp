#pragma once

#include <string>
#include <string_view>

namespace kano::backlog::webview::assets {

inline constexpr std::string_view kBackboardAppJsPart1 = R"JS(
    const state = {
      product: 'all',
      products: [],
      selectedProducts: new Set(['all']),
      selectedStates: new Set(['Proposed', 'Ready', 'InProgress', 'Blocked', 'Review', 'Done', 'Dropped']),
      selectedTypes: new Set(['Theme', 'Initiative', 'Epic', 'Feature', 'UserStory', 'Task', 'Bug', 'Issue', 'ADR', 'Topic', 'Workset']),
      q: '',
      limit: 200,
      workspace: '',
      workspaces: [],
      treeOpen: new Set(),
      treeTouched: false,
      activeTab: 'review',
      allItems: [],
      loadedTabs: new Set(),
      staleTabs: new Set(),
      tabCacheStatus: {},
      refreshSeq: 0,
      refreshAbort: null,
      refreshTimer: null,
      refreshTickTimer: null,
      refreshCancelRequested: false,
      activeRefresh: null,
      lastRefreshDiagnostic: null,
      detailSeq: 0,
      lastDetailDiagnostic: null,
      selectedItemKey: '',
      selectedItemId: '',
      selectedItemProduct: '',
      selectedItemVisibleIndex: -1,
      shortcutHelpOpen: false,
      reviewActorAlias: 'human-reviewer',
      graphMode: 'dependency',
      graphModePresets: [],
      graphItemId: '',
      graphItemProduct: '',
      graphBaseItemId: '',
      graphBaseItemProduct: '',
      graphMaxDepth: 2,
      graphBaseMaxDepth: 2,
       graphIsolationMode: 'fade',
       graphMaxChildrenPerNode: 25,
       graphMaxTotalNodes: 80,
       graphMaxTotalEdges: 120,
       graphPayload: null,
       graphViewport: {
         scale: 1,
         x: 0,
         y: 0,
         viewportWidth: 0,
         viewportHeight: 0,
         fullBounds: null,
         focusBounds: null,
         focusNodeCount: 0,
         defaultMode: 'all',
         dragging: null,
         suppressClick: false,
         resizeObserver: null,
       }
     };
    const lanes = ['Backlog', 'Doing', 'Blocked', 'Review', 'Done'];
    const reviewQueueOrder = ['Needs Review', 'Done Candidate', 'False Done Suspect', 'Evidence Gap', 'Blocked/Dirty', 'Stale/Drift', 'Ready Frontier'];
    const itemStates = ['Proposed', 'Ready', 'InProgress', 'Blocked', 'Review', 'Done', 'Dropped'];
    const itemTypes = ['Theme', 'Initiative', 'Epic', 'Feature', 'UserStory', 'Task', 'Bug', 'Issue', 'ADR', 'Topic', 'Workset'];
    const refreshableTabs = ['review', 'handoff', 'tree', 'roadmap', 'decision-radar', 'kanban', 'context', 'graph', 'runs'];
    const graphModeOrder = ['dependency', 'structure', 'cycles', 'related', 'product_memory'];
    const defaultGraphCaps = Object.freeze({
      maxDepth: 2,
      maxChildrenPerNode: 25,
      maxTotalNodes: 80,
      maxTotalEdges: 120,
    });
     const graphDepthBounds = Object.freeze({
       min: 1,
       max: 32,
     });
     const graphViewportBounds = Object.freeze({
       minScale: 0.35,
       maxScale: 3,
       zoomStep: 1.18,
       fitPadding: 36,
       panPadding: 84,
       keyboardPanStep: 72,
       minViewportWidth: 320,
       minViewportHeight: 240,
     });
     const graphIsolationModeOrder = ['fade', 'hide'];
    const tabLabels = {
      review: 'Review Inbox',
      handoff: 'Handoff Readiness',
      tree: 'Product Map',
      roadmap: 'Roadmap',
      'decision-radar': 'Decision Radar',
      kanban: 'Flow',
      context: 'Context',
      graph: 'Dependencies',
      runs: 'Agent Runs',
      command: 'Command',
    };
    const workspaceStorageKey = 'kano_webview_workspaces_v2';

    function tokenSetFromQuery(value, fallback) {
      if (!value) return new Set(fallback);
      if (value === '__none__') return new Set();
      return new Set(String(value).split(',').map((x) => x.trim()).filter(Boolean));
    }

    function boundedPositiveInt(value, fallback, max) {
      const parsed = Number.parseInt(value, 10);
      if (!Number.isFinite(parsed) || parsed <= 0) {
        return fallback;
      }
      return Math.min(parsed, max);
    }

)JS";

inline constexpr std::string_view kBackboardAppJsPart1a = R"JS(

    (function applyInitialQueryState() {
      const query = window.KobUi?.readQueryState?.() || {};
      const locale = String(navigator.language || '').trim();
      document.documentElement.lang = /^[A-Za-z]{2,3}(?:-[A-Za-z0-9]{2,8})*$/.test(locale) ? locale : 'en';
      if (query.products) {
        state.selectedProducts = tokenSetFromQuery(query.products, []);
      } else if (query.product) {
        state.selectedProducts = new Set([query.product]);
      }
      state.selectedStates = tokenSetFromQuery(query.state, itemStates);
      state.selectedTypes = tokenSetFromQuery(query.type, itemTypes);
      if (query.q) state.q = query.q;
      if (query.limit) {
        const parsed = Number.parseInt(query.limit, 10);
        if (Number.isFinite(parsed)) {
          state.limit = Math.max(1, Math.min(parsed, 1000));
        }
      }
      if (query.tab && ['review', 'handoff', 'tree', 'roadmap', 'decision-radar', 'kanban', 'context', 'graph', 'runs', 'command'].includes(query.tab)) {
        state.activeTab = query.tab;
      }
      if (graphModeOrder.includes(query.mode)) {
        state.graphMode = query.mode;
      }
      state.graphMaxDepth = boundedPositiveInt(query.max_depth, defaultGraphCaps.maxDepth, graphDepthBounds.max);
      state.graphIsolationMode = normalizeGraphIsolationMode(query.graph_isolation);
      if (query.item) {
        const rootProduct = String(query.root_product || '').trim();
        const fallbackProduct = String(query.product || '').trim();
        const graphProduct = rootProduct || (fallbackProduct && fallbackProduct !== 'all' ? fallbackProduct : '');
        state.graphItemId = String(query.item).trim();
        state.graphItemProduct = graphProduct;
        state.graphBaseItemId = state.graphItemId;
        state.graphBaseItemProduct = state.graphItemProduct;
        state.graphBaseMaxDepth = state.graphMaxDepth;
        state.graphMaxChildrenPerNode = boundedPositiveInt(query.max_children_per_node, defaultGraphCaps.maxChildrenPerNode, 1000);
        state.graphMaxTotalNodes = boundedPositiveInt(query.max_total_nodes || query.node_limit, defaultGraphCaps.maxTotalNodes, 1000);
        state.graphMaxTotalEdges = boundedPositiveInt(query.max_total_edges || query.edge_limit, defaultGraphCaps.maxTotalEdges, 1000);
      }
    })();

    function nowIso() {
      return new Date().toISOString();
    }

    function endpointPath(url) {
      try {
        return new URL(url, window.location.href).pathname;
      } catch (_error) {
        return String(url || '').split('?')[0];
      }
    }

    function beginStageTiming(refresh, stage, method, url) {
      if (!refresh) {
        return null;
      }
      const startedAt = Date.now();
      const timing = {
        stage: stage || endpointPath(url),
        method: method || 'GET',
        endpoint: endpointPath(url),
        operation: `${method || 'GET'} ${url}`,
        started_at: new Date(startedAt).toISOString(),
        started_at_ms: startedAt,
        status: 'running',
      };
      refresh.current_stage = timing.stage;
      refresh.active_endpoint = timing.endpoint;
      refresh.active_operation = timing.operation;
      refresh.stage_timings.push(timing);
      updateBusyFromRefresh(refresh);
      return timing;
    }

    function finishStageTiming(timing, refresh, result = {}) {
      if (!timing) {
        return;
      }
      timing.finished_at = nowIso();
      timing.elapsed_ms = Date.now() - timing.started_at_ms;
      timing.http_status = result.http_status || 0;
      timing.status = result.ok === false ? 'failed' : 'loaded';
      if (result.cache_status) {
        timing.cache_status = result.cache_status;
        if (refresh) {
          refresh.cache_status = result.cache_status;
        }
      }
      if (result.aborted) {
        timing.status = 'aborted';
        timing.aborted = true;
      }
      if (result.error) {
        timing.error_name = result.error.name || '';
        timing.error = result.error.message || String(result.error);
      }
      if (refresh) {
        updateBusyFromRefresh(refresh);
      }
    }

    async function getJson(url, options = {}) {
      const timing = beginStageTiming(options.refresh, options.stage, 'GET', url);
      try {
        const resp = await fetch(url, { signal: options.signal });
        let body = null;
        try {
          body = await resp.json();
        } catch (_e) {
          body = {};
        }
        if (!resp.ok || body?.ok === false) {
          const detail = body?.data?.error || body?.error || `HTTP ${resp.status}`;
          const error = new Error(detail);
          error.httpStatus = resp.status;
          throw error;
        }
        finishStageTiming(timing, options.refresh, {
          ok: true,
          http_status: resp.status,
          cache_status: body?.data?.cache_status || body?.cache_status || '',
        });
        return body;
      } catch (error) {
        finishStageTiming(timing, options.refresh, {
          ok: false,
          http_status: error.httpStatus || 0,
          aborted: error?.name === 'AbortError',
          error,
        });
        throw error;
      }
    }

    async function postJson(url, payload, options = {}) {
      const timing = beginStageTiming(options.refresh, options.stage, 'POST', url);
      try {
        const resp = await fetch(url, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload || {}),
          signal: options.signal
        });
        let body = null;
        try {
          body = await resp.json();
        } catch (_e) {
          body = {};
        }
        if (!resp.ok || body?.ok === false) {
          const detail = body?.data?.error || body?.error || `HTTP ${resp.status}`;
          const error = new Error(detail);
          error.httpStatus = resp.status;
          throw error;
        }
        finishStageTiming(timing, options.refresh, {
          ok: true,
          http_status: resp.status,
          cache_status: body?.data?.cache_status || body?.cache_status || '',
        });
        return body;
      } catch (error) {
        finishStageTiming(timing, options.refresh, {
          ok: false,
          http_status: error.httpStatus || 0,
          aborted: error?.name === 'AbortError',
          error,
        });
        throw error;
      }
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
        const shortcut = i < 9 ? `<div class="workspace-meta">Alt+${i + 1}</div>` : '<div class="workspace-meta"></div>';
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
      clearLoadedViewState('workspace switched');
      await loadProducts();
      await refreshActiveTab({ force: true, reason: 'workspace switched' });
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

    function formatElapsed(startedAt) {
      const seconds = Math.max(0, Math.floor((Date.now() - startedAt) / 1000));
      if (seconds < 60) {
        return `${seconds}s`;
      }
      return `${Math.floor(seconds / 60)}m ${String(seconds % 60).padStart(2, '0')}s`;
    }

    function setStatus(text, kind = '') {
      const status = document.getElementById('status');
      status.textContent = text || '';
      status.classList.toggle('status-error', kind === 'error');
    }
)JS";

inline constexpr std::string_view kBackboardAppJsPart1b = R"JS(

    const defaultGraphModePresets = [
      {
        id: 'dependency',
        label: 'Dependencies',
        description: 'Review blockers, blocked items, and the nearby structure already recorded for this backlog area.',
        review_question: 'What blocks delivery here, what is blocked, and what surrounding context matters for review?',
        scope_note: 'Keeps the current dependency graph breadth so existing Focus Graph and dependency review flows stay readable.'
      },
      {
        id: 'structure',
        label: 'Structure',
        description: 'Show the explicit parent-child hierarchy without mixing in blocking or non-blocking references.',
        review_question: 'How is this work item structured inside the current Product Map hierarchy?',
        scope_note: 'Uses only recorded parent links; no extra dependency or design-history inference is added.'
      },
      {
        id: 'cycles',
        label: 'Dependency cycles',
        description: 'Reduce the canvas to dependency edges so cycle diagnostics stay focused on blocker relationships.',
        review_question: 'Is there a blocker loop that needs a human break-the-cycle decision?',
        scope_note: 'Dependency cycle semantics remain based only on dependency edges.'
      },
      {
        id: 'related',
        label: 'Related refs',
        description: 'Show non-blocking reference links that connect nearby work without claiming a delivery dependency.',
        review_question: 'Which neighboring items are relevant context but not true blockers?',
        scope_note: 'Uses recorded relates links only; it does not invent implied dependencies.'
      },
      {
        id: 'product_memory',
        label: 'Product memory',
        description: 'Show grouping and context edges already present in the backlog metadata.',
        review_question: 'What shared topic context is already recorded for this work?',
        scope_note: 'Currently limited to recorded topic membership; it does not promise unsupported design-history or evolution edges.'
      },
    ];

    function isRefreshableTab(tab = state.activeTab) {
      return refreshableTabs.includes(tab);
    }

    function activePageElement(tab = state.activeTab) {
      return document.getElementById(`page-${tab}`);
    }

    function cacheStatusFor(tab = state.activeTab) {
      if (state.staleTabs.has(tab)) {
        return 'stale';
      }
      if (state.loadedTabs.has(tab)) {
        return 'cached';
      }
      return 'cold';
    }

    function setPageRefreshState(tab, refreshing = false, note = '') {
      const page = activePageElement(tab);
      if (!page) {
        return;
      }
      const stale = state.staleTabs.has(tab);
      page.classList.toggle('is-refreshing', !!refreshing);
      page.classList.toggle('is-stale', stale && !refreshing);
      if (refreshing || stale) {
        const label = tabLabels[tab] || tab;
        const defaultNote = refreshing
          ? `Refreshing ${label}; visible data stays usable`
          : `Showing stale ${label}; refresh pending`;
        page.setAttribute('data-refresh-note', note || defaultNote);
      } else {
        page.removeAttribute('data-refresh-note');
      }
    }

    function updateAllPageRefreshStates() {
      refreshableTabs.forEach((tab) => setPageRefreshState(tab, false));
    }

    function markLoadedViewsStale(reason) {
      for (const tab of state.loadedTabs) {
        if (!isRefreshableTab(tab)) {
          continue;
        }
        state.staleTabs.add(tab);
        state.tabCacheStatus[tab] = {
          status: 'stale',
          reason: reason || 'filters changed',
          updated_at: nowIso(),
        };
      }
      updateAllPageRefreshStates();
    }

    function clearLoadedViewState(reason = '') {
      state.loadedTabs.clear();
      state.staleTabs.clear();
      state.tabCacheStatus = {};
      if (reason) {
        state.lastRefreshDiagnostic = makeRefreshDiagnostic('reset', reason, Date.now(), []);
      }
      updateAllPageRefreshStates();
    }

    function createRefreshContext(requestId, tab, reason, overrides = {}) {
      const startedAt = Date.now();
      return {
        request_id: `backboard-refresh-${requestId}`,
        status: 'running',
        view: tab,
        view_label: tabLabels[tab] || tab,
        reason: reason || 'active tab refresh',
        generated_at: nowIso(),
        started_at: new Date(startedAt).toISOString(),
        started_at_ms: startedAt,
        product_scope: selectedProductValues(),
        product_scope_label: productScopeLabel(),
        query: state.q,
        states: [...state.selectedStates],
        types: [...state.selectedTypes],
        limit: state.limit,
        current_stage: '',
        active_endpoint: '',
        active_operation: '',
        stage_timings: [],
        completed: 0,
        total: 1,
        cache_status: cacheStatusFor(tab),
        stale_reason: state.staleTabs.has(tab)
          ? state.tabCacheStatus[tab]?.reason || 'visible data retained while refreshing'
          : '',
        cancel_reason: '',
        abort_reason: '',
        ...overrides,
      };
    }

    function setBusy(active, recoverable = false) {
      document.getElementById('status-wrap').classList.toggle('busy', active);
      document.getElementById('busy-banner').classList.toggle('visible', active || recoverable);
      document.getElementById('busy-cancel').hidden = !active;
      document.getElementById('busy-retry').hidden = !recoverable;
      document.getElementById('busy-copy').hidden = !(active || recoverable);
      if (!active && state.refreshTickTimer) {
        clearInterval(state.refreshTickTimer);
        state.refreshTickTimer = null;
      }
    }

    function updateBusy(title, detail) {
      document.getElementById('busy-title').textContent = title;
      document.getElementById('busy-detail').textContent = detail;
    }

    function updateBusyFromRefresh(refresh) {
      if (!refresh) {
        return;
      }
      const elapsed = formatElapsed(refresh.started_at_ms || Date.now());
      const stage = refresh.current_stage || refresh.reason || 'starting';
      const endpoint = refresh.active_endpoint || 'pending';
      const operation = refresh.active_operation || 'waiting for endpoint';
      const stale = refresh.stale_reason ? `; stale=${refresh.stale_reason}` : '';
      const cancel = refresh.cancel_reason ? `; cancel=${refresh.cancel_reason}` : '';
      const abort = refresh.abort_reason ? `; abort=${refresh.abort_reason}` : '';
      updateBusy(
        `Loading ${refresh.view_label}`,
        `view=${refresh.view_label}; scope=${refresh.product_scope_label}; stage=${stage}; endpoint=${endpoint}; operation=${operation}; elapsed=${elapsed}; cache=${refresh.cache_status || cacheStatusFor(refresh.view)}${stale}${cancel}${abort}`,
      );
    }

    function describeRefreshError(error) {
      if (error?.name === 'AbortError') {
        return state.refreshCancelRequested
          ? 'Refresh canceled by user'
          : 'Refresh request aborted before completion';
      }
      return error?.message || String(error || 'Unknown refresh error');
    }

    function makeRefreshDiagnostic(status, detail, startedAt, failures = [], refresh = state.activeRefresh) {
      const active = refresh || {};
      const startedAtMs = active.started_at_ms || startedAt || Date.now();
      const stageTimings = (active.stage_timings || []).map((timing) => ({
        stage: timing.stage || '',
        method: timing.method || '',
        endpoint: timing.endpoint || '',
        operation: timing.operation || '',
        status: timing.status || '',
        http_status: timing.http_status || 0,
        elapsed_ms: timing.elapsed_ms || 0,
        started_at: timing.started_at || '',
        finished_at: timing.finished_at || '',
        cache_status: timing.cache_status || '',
        aborted: !!timing.aborted,
        error_name: timing.error_name || '',
        error: timing.error || '',
      }));
      return {
        request_id: active.request_id || `backboard-refresh-${state.refreshSeq}`,
        status,
        detail,
        generated_at: nowIso(),
        started_at: active.started_at || new Date(startedAtMs).toISOString(),
        elapsed: formatElapsed(startedAtMs),
        elapsed_ms: Date.now() - startedAtMs,
        workspace: state.workspace || '',
        product_scope: active.product_scope || selectedProductValues(),
        product_scope_label: active.product_scope_label || productScopeLabel(),
        query: state.q,
        states: [...state.selectedStates],
        types: [...state.selectedTypes],
        limit: state.limit,
        active_tab: active.view || state.activeTab,
        view: active.view || state.activeTab,
        view_label: active.view_label || tabLabels[state.activeTab] || state.activeTab,
        active_endpoint: active.active_endpoint || '',
        active_operation: active.active_operation || '',
        current_stage: active.current_stage || '',
        cache_status: active.cache_status || cacheStatusFor(active.view || state.activeTab),
        stale_reason: active.stale_reason || '',
        cancel_reason: active.cancel_reason || '',
        abort_reason: active.abort_reason || '',
        aborted: !!active.abort_reason || failures.some((failure) => (failure.reason || failure)?.name === 'AbortError'),
        stage_timings: stageTimings,
        last_detail_request: state.lastDetailDiagnostic,
        failures: failures.map((failure) => ({
          status: failure.status || 'rejected',
          stage: failure.stage || '',
          endpoint: failure.endpoint || '',
          reason: describeRefreshError(failure.reason || failure),
          error_name: (failure.reason || failure)?.name || '',
        })),
      };
    }

    async function copyRefreshDiagnostic() {
      const diagnostic = state.lastRefreshDiagnostic ||
          makeRefreshDiagnostic('idle', 'No refresh diagnostic is available', Date.now(), []);
      const text = JSON.stringify(diagnostic, null, 2);
      try {
        await navigator.clipboard.writeText(text);
        setStatus('Refresh diagnostics copied');
      } catch (_error) {
        setStatus('Unable to copy diagnostics', 'error');
      }
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

    function normalizeGraphMode(value) {
      const mode = String(value || '').trim();
      return graphModeOrder.includes(mode) ? mode : 'dependency';
    }

    function normalizeGraphIsolationMode(value) {
      const mode = String(value || '').trim().toLowerCase();
      return graphIsolationModeOrder.includes(mode) ? mode : 'fade';
    }

    function graphIsolationModeLabel(mode) {
      return normalizeGraphIsolationMode(mode) === 'hide'
        ? 'hide unrelated nodes'
        : 'fade unrelated nodes';
    }

    function graphFetchMaxDepth() {
      const localDepth = boundedPositiveInt(state.graphMaxDepth, defaultGraphCaps.maxDepth, graphDepthBounds.max);
      return Math.min(localDepth + 1, graphDepthBounds.max);
    }

    function graphIsolationReasonLabel(reason) {
      const value = String(reason || '').trim();
      if (value === 'focus-root') return 'focus root';
      if (value === 'within-depth') return 'within depth';
      if (value === 'outside-depth') return 'outside requested depth';
      if (value === 'disconnected') return 'disconnected from focused root';
      if (value === 'root-missing') return 'focused root missing from graph response';
      return value || 'included';
    }

    function graphModePresetsFor(data) {
      const presets = Array.isArray(data?.mode_presets)
        ? data.mode_presets.filter((preset) => preset && graphModeOrder.includes(String(preset.id || '')))
        : [];
      return presets.length ? presets : defaultGraphModePresets;
    }

    function graphPresetForMode(presets, mode) {
      return presets.find((preset) => String(preset.id || '') === mode) || presets[0] || defaultGraphModePresets[0];
    }

    function updateGraphModeHelp(preset) {
      const target = document.getElementById('graph-mode-help');
      if (!target) return;
      const details = [
        preset?.description ? `<span>${esc(preset.description)}</span>` : '',
        preset?.review_question ? `<span><strong>Review question:</strong> ${esc(preset.review_question)}</span>` : '',
        preset?.scope_note ? `<span>${esc(preset.scope_note)}</span>` : '',
      ].filter(Boolean);
      target.innerHTML = details.join(' ');
    }

    function syncGraphModeControls(data = null) {
      const presets = graphModePresetsFor(data);
      state.graphModePresets = presets;
      const select = document.getElementById('graph-mode');
      const signature = presets.map((preset) => `${preset.id}:${preset.label || preset.id}`).join('|');
      if (select && select.dataset.signature !== signature) {
        select.innerHTML = presets.map((preset) =>
          `<option value="${escAttr(preset.id || 'dependency')}">${esc(preset.label || preset.id || 'dependency')}</option>`
        ).join('');
        select.dataset.signature = signature;
      }
      state.graphMode = normalizeGraphMode(data?.mode || state.graphMode);
      if (select) {
        select.value = state.graphMode;
      }
      const preset = graphPresetForMode(presets, state.graphMode);
      updateGraphModeHelp(preset);
      return preset;
    }

)JS";

inline constexpr std::string_view kBackboardAppJsPart1bb = R"JS(

    function graphScopeDirty() {
      const baseId = String(state.graphBaseItemId || '');
      const baseProduct = String(state.graphBaseItemProduct || '');
      const currentId = String(state.graphItemId || '');
      const currentProduct = String(state.graphItemProduct || '');
      const baseDepth = boundedPositiveInt(state.graphBaseMaxDepth, defaultGraphCaps.maxDepth, graphDepthBounds.max);
      return currentId !== baseId ||
        currentProduct !== baseProduct ||
        state.graphMaxDepth !== baseDepth ||
        normalizeGraphIsolationMode(state.graphIsolationMode) !== 'fade';
    }

    function syncGraphIsolationControls() {
      const depthInput = document.getElementById('graph-max-depth');
      if (depthInput) {
        depthInput.value = String(state.graphMaxDepth);
      }
      const modeSelect = document.getElementById('graph-isolation-mode');
      if (modeSelect && !modeSelect.dataset.signature) {
        modeSelect.innerHTML = [
          { id: 'fade', label: 'Fade unrelated' },
          { id: 'hide', label: 'Hide unrelated' },
        ].map((option) =>
          `<option value="${escAttr(option.id)}">${esc(option.label)}</option>`
        ).join('');
        modeSelect.dataset.signature = 'fade|hide';
      }
      if (modeSelect) {
        modeSelect.value = normalizeGraphIsolationMode(state.graphIsolationMode);
      }
      const resetButton = document.getElementById('graph-reset-scope');
      if (resetButton) {
        resetButton.disabled = !graphScopeDirty();
      }
      const scopeHelp = document.getElementById('graph-scope-help');
      if (scopeHelp) {
        scopeHelp.textContent = state.graphItemId
          ? 'Click a node to re-root this bounded graph. Depth updates the bounded neighborhood; unrelated nodes stay diagnosable when faded or hidden.'
          : 'Select an item to enable bounded graph isolation. Reset scope restores the incoming root or scaffold.';
      }
    }

    function graphQueryString() {
      const params = new URLSearchParams();
      params.set('product', 'all');
      params.set('limit', '1000');
      params.set('offset', '0');
      params.set('mode', normalizeGraphMode(state.graphMode));
      params.set('graph_isolation', normalizeGraphIsolationMode(state.graphIsolationMode));
      params.set('max_depth', String(graphFetchMaxDepth()));
      if (state.graphItemId) {
        params.set('item', state.graphItemId);
        if (state.graphItemProduct) {
          params.set('root_product', state.graphItemProduct);
        }
        params.set('max_children_per_node', String(state.graphMaxChildrenPerNode));
        params.set('max_total_nodes', String(state.graphMaxTotalNodes));
        params.set('max_total_edges', String(state.graphMaxTotalEdges));
      }
      return params.toString();
    }

    function focusGraphProductScope() {
      if (state.graphItemProduct) {
        return state.graphItemProduct;
      }
      const products = selectedProductValues();
      return products.length === 1 && products[0] !== 'all' ? products[0] : '';
    }

    function focusGraphReviewHref() {
      const params = new URLSearchParams();
      params.set('tab', 'review');
      const product = focusGraphProductScope();
      if (product) {
        params.set('product', product);
      }
      if (state.graphItemId) {
        params.set('item', state.graphItemId);
      }
      return `/?${params.toString()}`;
    }

    function updateFocusGraphPageChrome() {
      const rootLabel = document.getElementById('focus-graph-root-label');
      const backLink = document.getElementById('focus-graph-back-link');
      const product = focusGraphProductScope();
      if (rootLabel) {
        rootLabel.textContent = state.graphItemId
          ? `${product ? `${state.graphItemId} / ${product}` : state.graphItemId} • depth ${state.graphMaxDepth} • ${graphIsolationModeLabel(state.graphIsolationMode)}`
          : 'No item root selected';
      }
      if (backLink) {
        backLink.href = focusGraphReviewHref();
        backLink.textContent = state.graphItemId ? 'Back to Item Review' : 'Back to Review Inbox';
      }
      syncGraphIsolationControls();
    }

    function renderFocusGraphScaffold() {
      state.graphPayload = null;
      state.graphViewport.fullBounds = null;
      state.graphViewport.focusBounds = null;
      state.graphViewport.focusNodeCount = 0;
      state.graphViewport.dragging = null;
      disconnectGraphViewportResizeObserver();
      syncGraphModeControls();
      updateFocusGraphPageChrome();
      document.getElementById('graph-summary').textContent = '';
      document.getElementById('graph-list').innerHTML =
        '<div class="card graph-empty-state"><div class="muted">Select an item to open a bounded Focus Graph canvas.</div></div>';
      updateGraphViewportButtons();
    }

    function markGraphTabStale(reason) {
      if (state.loadedTabs.has('graph')) {
        state.staleTabs.add('graph');
        state.tabCacheStatus.graph = {
          status: 'stale',
          reason: reason || 'graph scope changed',
          updated_at: nowIso(),
          request_id: '',
        };
      }
    }

    function resetGraphScope() {
      state.graphItemId = String(state.graphBaseItemId || '').trim();
      state.graphItemProduct = String(state.graphBaseItemProduct || '').trim();
      state.graphMaxDepth = boundedPositiveInt(state.graphBaseMaxDepth, defaultGraphCaps.maxDepth, graphDepthBounds.max);
      state.graphIsolationMode = 'fade';
      updateFocusGraphPageChrome();
      updateUrlState();
      markGraphTabStale(state.graphItemId ? 'graph scope reset to initial root' : 'graph scope reset to scaffold');
      if (state.activeTab === 'graph') {
        scheduleRefresh(0);
        return;
      }
      updateAllPageRefreshStates();
      setStatus(state.graphItemId ? `Graph scope reset to ${state.graphItemId}` : 'Graph scope reset');
    }

    function setGraphRoot(itemId, product = '', options = {}) {
      const nextId = String(itemId || '').trim();
      const nextProduct = String(product || '').trim();
      if (!nextId) {
        return;
      }
      if (!state.graphBaseItemId) {
        state.graphBaseItemId = state.graphItemId || nextId;
        state.graphBaseItemProduct = state.graphItemProduct || nextProduct;
        state.graphBaseMaxDepth = state.graphMaxDepth;
      }
      const changed = nextId !== state.graphItemId || nextProduct !== state.graphItemProduct;
      state.graphItemId = nextId;
      state.graphItemProduct = nextProduct;
      updateFocusGraphPageChrome();
      updateUrlState();
      markGraphTabStale(options.reason || 'graph root changed');
      if (state.activeTab === 'graph') {
        scheduleRefresh(0);
      } else {
        updateAllPageRefreshStates();
      }
      if (changed && options.announce !== false) {
        setStatus(`Focused graph root ${nextId}`);
      }
    }

)JS";

inline constexpr std::string_view kBackboardAppJsPart1c = R"JS(

    function updateUrlState() {
      const products = selectedProductValues();
      const graphTabActive = state.activeTab === 'graph';
      const graphRouteActive = graphTabActive && !!state.graphItemId;
      const update = {
        tab: state.activeTab,
        product: null,
        products: null,
        root_product: null,
        mode: graphTabActive || state.graphMode !== 'dependency'
          ? (state.graphMode === 'dependency' ? null : state.graphMode)
          : null,
        graph_isolation: graphTabActive ? normalizeGraphIsolationMode(state.graphIsolationMode) : null,
        item: graphRouteActive ? state.graphItemId : null,
        max_depth: graphTabActive ? String(state.graphMaxDepth) : null,
        max_children_per_node: graphRouteActive ? String(state.graphMaxChildrenPerNode) : null,
        max_total_nodes: graphRouteActive ? String(state.graphMaxTotalNodes) : null,
        max_total_edges: graphRouteActive ? String(state.graphMaxTotalEdges) : null,
        q: state.q || null,
        state: selectedTokens(state.selectedStates, itemStates) || null,
        type: selectedTokens(state.selectedTypes, itemTypes) || null,
        limit: String(state.limit || 200),
      };
      if (graphRouteActive) {
        update.product = 'all';
        update.products = null;
        update.root_product = state.graphItemProduct || null;
      } else if (products.length === 1) {
        update.product = products[0];
      } else {
        update.products = products.join(',');
      }
      window.KobUi?.setQueryState?.(update, { replace: true });
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

    function logicalRefLabel(ref) {
      return ref?.adr_id || ref?.item_id || ref?.evidence_id || ref?.topic_id || ref?.uid || ref?.vision_id || ref?.product || '';
    }

    function renderTreeRefChip(ref) {
      const label = logicalRefLabel(ref);
      if (!label) return '';
      const product = ref?.product || '';
      const itemId = ref?.adr_id || ref?.item_id || '';
      if (itemId) {
        return `<a href="#" class="pill item-link" data-item-id="${escAttr(itemId)}" data-item-product="${escAttr(product)}">${esc(label)}</a>`;
      }
      return `<span class="pill">${esc(label)}</span>`;
    }

    function renderTreeNavigation(node) {
      const nav = node.navigation || {};
      const adrRefs = nav.adr_refs || [];
      const evidenceRefs = nav.evidence_refs || [];
      const diagnostics = nav.diagnostics || [];
      if (!adrRefs.length && !evidenceRefs.length && !diagnostics.length) return '';
      const chips = [];
      if (adrRefs.length) {
        chips.push('<span class="muted">ADR</span>');
        chips.push(...adrRefs.map(renderTreeRefChip));
      }
      if (evidenceRefs.length) {
        chips.push('<span class="muted">Evidence</span>');
        chips.push(...evidenceRefs.map(renderTreeRefChip));
      }
      if (diagnostics.length) {
        chips.push(`<span class="pill missing">Gaps ${diagnostics.length}</span>`);
      }
      return `<span class="tree-nav" aria-label="Product Map refs">${chips.join('')}</span>`;
    }

    function safeHref(href) {
      const value = String(href || '').trim();
      if (!value) return '#';
      if (/^(https?:|mailto:|#|\/)/i.test(value)) return value;
      if (/^[A-Za-z][A-Za-z0-9+.-]*:/i.test(value)) return '#';
      return value;
    }
)JS";

inline constexpr std::string_view kBackboardAppJsPart2 = R"JS(

    function renderInlineMarkdown(raw) {
      const text = String(raw || '');
      const pattern = /`([^`]+)`|\[\[([^\]|]+)(?:\|([^\]]+))?\]\]|\[([^\]]+)\]\(([^)\s]+)\)/g;
      let out = '';
      let last = 0;
      let match = null;
      while ((match = pattern.exec(text)) !== null) {
        out += esc(text.slice(last, match.index));
        if (match[1] !== undefined) {
          out += `<code>${esc(match[1])}</code>`;
        } else if (match[2] !== undefined) {
          const cleanTarget = String(match[2] || '').trim();
          const label = String(match[3] || cleanTarget || '').trim();
          out += `<a href="#" class="obs-wikilink" data-item-id="${escAttr(cleanTarget)}">${esc(label)}</a>`;
        } else {
          const label = String(match[4] || '').trim();
          const href = safeHref(match[5]);
          out += `<a href="${escAttr(href)}" target="_blank" rel="noreferrer">${esc(label || href)}</a>`;
        }
        last = pattern.lastIndex;
      }
      out += esc(text.slice(last));
      return out;
    }

    function splitTableRow(line) {
      return String(line || '')
        .trim()
        .replace(/^\|/, '')
        .replace(/\|$/, '')
        .split('|')
        .map((cell) => cell.trim());
    }

    function isTableSeparator(line) {
      const cells = splitTableRow(line);
      return cells.length > 1 && cells.every((cell) => /^:?-{3,}:?$/.test(cell));
    }

    function renderMarkdownBlocks(lines) {
      const out = [];
      let paragraph = [];

      const flushParagraph = () => {
        if (!paragraph.length) return;
        const text = paragraph.join(' ').trim();
        if (text) out.push(`<p>${renderInlineMarkdown(text)}</p>`);
        paragraph = [];
      };

      for (let i = 0; i < lines.length; ++i) {
        const line = String(lines[i] || '');
        const trimmed = line.trim();
        if (!trimmed) {
          flushParagraph();
          continue;
        }

        const fence = trimmed.match(/^```([A-Za-z0-9_+.-]*)\s*$/);
        if (fence) {
          flushParagraph();
          const lang = fence[1] || '';
          const codeLines = [];
          i += 1;
          while (i < lines.length && !String(lines[i] || '').trim().startsWith('```')) {
            codeLines.push(String(lines[i] || ''));
            i += 1;
          }
          const langLabel = lang ? `<span class="md-code-lang">${esc(lang)}</span>` : '';
          out.push(`<pre>${langLabel}<code class="${lang ? `language-${escAttr(lang)}` : ''}">${esc(codeLines.join('\n'))}</code></pre>`);
          continue;
        }

        const heading = trimmed.match(/^(#{1,6})\s+(.+)$/);
        if (heading) {
          flushParagraph();
          const level = Math.min(6, heading[1].length);
          out.push(`<h${level}>${renderInlineMarkdown(heading[2])}</h${level}>`);
          continue;
        }

        if (trimmed.startsWith('>')) {
          flushParagraph();
          const quoteLines = [];
          while (i < lines.length && String(lines[i] || '').trim().startsWith('>')) {
            quoteLines.push(String(lines[i] || '').replace(/^\s*>\s?/, ''));
            i += 1;
          }
          i -= 1;
          const callout = String(quoteLines[0] || '').trim().match(/^\[!([A-Za-z0-9_-]+)\]\s*(.*)$/);
          if (callout) {
            const title = String(callout[2] || callout[1] || 'Callout').trim();
            out.push(`<div class="obs-callout"><div class="obs-callout-title">${esc(title)}</div>${renderMarkdownBlocks(quoteLines.slice(1))}</div>`);
          } else {
            out.push(`<blockquote>${renderMarkdownBlocks(quoteLines)}</blockquote>`);
          }
          continue;
        }

        const unordered = trimmed.match(/^[-*+]\s+(.+)$/);
        const ordered = trimmed.match(/^\d+[.)]\s+(.+)$/);
        if (unordered || ordered) {
          flushParagraph();
          const orderedList = Boolean(ordered);
          const items = [];
          while (i < lines.length) {
            const itemLine = String(lines[i] || '').trim();
            const matchItem = orderedList
              ? itemLine.match(/^\d+[.)]\s+(.+)$/)
              : itemLine.match(/^[-*+]\s+(.+)$/);
            if (!matchItem) break;
            items.push(`<li>${renderInlineMarkdown(matchItem[1])}</li>`);
            i += 1;
          }
          i -= 1;
          out.push(`<${orderedList ? 'ol' : 'ul'}>${items.join('')}</${orderedList ? 'ol' : 'ul'}>`);
          continue;
        }

        if (trimmed.includes('|') && i + 1 < lines.length && isTableSeparator(lines[i + 1])) {
          flushParagraph();
          const headers = splitTableRow(trimmed);
          i += 2;
          const rows = [];
          while (i < lines.length && String(lines[i] || '').trim().includes('|')) {
            rows.push(splitTableRow(lines[i]));
            i += 1;
          }
          i -= 1;
          out.push('<table><thead><tr>' +
            headers.map((cell) => `<th>${renderInlineMarkdown(cell)}</th>`).join('') +
            '</tr></thead><tbody>' +
            rows.map((row) => '<tr>' + row.map((cell) => `<td>${renderInlineMarkdown(cell)}</td>`).join('') + '</tr>').join('') +
            '</tbody></table>');
          continue;
        }

        paragraph.push(line);
      }

      flushParagraph();
      return out.join('\n');
    }

    function renderMarkdownWithObsidian(raw) {
      return renderMarkdownBlocks(String(raw || '').split(/\r?\n/));
    }

    function hydrateMarkdown(root) {
      const container = typeof root === 'string'
        ? document.querySelector(root)
        : (root || document);
      if (!container) return;
      container.querySelectorAll('[data-kob-markdown]').forEach((node) => {
        if (node.__kobMarkdownHydrated) return;
        const source = node.querySelector('.kob-md-source');
        const raw = source ? (source.value || source.textContent || '') : '';
        node.innerHTML = renderMarkdownWithObsidian(raw || '(empty)');
        node.__kobMarkdownHydrated = true;
      });
    }

    function bindItemLinksWithin(root, fallbackProduct = '') {
      const container = typeof root === 'string'
        ? document.querySelector(root)
        : root;
      if (!container) return;
      container.querySelectorAll('[data-item-id]').forEach((link) => {
        if (link.__kobItemLinkBound) return;
        link.__kobItemLinkBound = true;
        link.addEventListener('click', async (event) => {
          event.preventDefault();
          const id = link.getAttribute('data-item-id');
          const product = link.getAttribute('data-item-product') || fallbackProduct || '';
          if (!id) return;
          await openItemModal(id, product);
        });
      });
    }

    function typeIcon(type) {
      const map = {
        Theme: '🧩',
        Initiative: 'I',
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

    function gateStateClass(status) {
      const value = String(status || 'unknown');
      return value === 'passed' ? 'passed' :
        value === 'failed' ? 'failed' :
        value === 'blocked' ? 'blocked' :
        (value === 'not-run' || value === 'unknown' ? 'missing' : '');
    }

    function gateStateText(status) {
      const value = String(status || 'unknown');
      if (value === 'not-run') return 'not run';
      return value;
    }

    function gateStateSymbol(status) {
      const value = String(status || 'unknown');
      if (value === 'passed') return '✓';
      if (value === 'failed') return '!';
      if (value === 'blocked') return '⛔';
      return '?';
    }

    function renderGateBadge(label, gate) {
      const stateValue = String(gate?.state || 'unknown');
      const stateLabel = gateStateText(stateValue);
      return `<span class="pill gate-badge ${gateStateClass(stateValue)}" data-gate-state="${escAttr(stateValue)}" data-gate-symbol="${escAttr(gateStateSymbol(stateValue))}" title="${escAttr(`${label} gate: ${stateLabel}`)}" aria-label="${escAttr(`${label} gate ${stateLabel}`)}">${esc(label)}</span>`;
    }

    function renderGateStrip(gateStatus) {
      const gate = gateStatus || {};
      return `<div class="gate-strip" role="list" aria-label="Gate status">${renderGateBadge('Ready', gate.ready)}${renderGateBadge('Review', gate.review)}${renderGateBadge('Done', gate.done)}</div>`;
    }

    function selectableItemAttrs(item) {
      return `data-selectable-item="true" data-item-id="${escAttr(item.id || '')}" data-item-product="${escAttr(item.product || '')}" tabindex="-1" aria-selected="false" role="option"`;
    }

    function renderItemCardSummary(item) {
      return `<div><code>${esc(item.id || '')}</code></div><div><a href="#" class="item-link" data-item-id="${escAttr(item.id || '')}" data-item-product="${escAttr(item.product || '')}">${esc(item.title || item.id || '')}</a></div>${renderGateStrip(item.gate_status)}<div class="muted">${esc(renderMeta(item))}</div>`;
    }

    function renderItemCard(item) {
      return `<div class="card" ${selectableItemAttrs(item)}>${renderItemCardSummary(item)}</div>`;
    }
)JS";

inline constexpr std::string_view kBackboardAppJsPart3 = R"JS(

    function renderReviewCard(bundle, lane) {
      const item = bundle?.item || {};
      const reason = bundle?.review_reason || `Queued for ${lane || 'review'} review.`;
      const reasonCode = bundle?.reason_code ? `<code>${esc(bundle.reason_code)}</code> ` : '';
      const decision = bundle?.suggested_human_decision ? `<div class="muted">Suggested decision: ${esc(bundle.suggested_human_decision)}</div>` : '';
      const draft = bundle?.review_draft || {};
      const draftText = draft.exists ? (draft.rationale || '') : '';
      const draftStatus = draft.exists ? `Draft saved for ${esc(draft.actor_alias || state.reviewActorAlias)}.` : 'No saved draft.';
      const actions = (bundle?.actions || []).map((action) => {
        const targetState = action.target_state || '';
        const resultLabel = targetState ? `<span class="muted">Resulting state: ${esc(targetState)}</span>` : '';
        return `<span class="review-action-wrap" style="display:inline-flex;align-items:center;gap:4px;"><button class="btn review-action" data-review-action="submit" data-human-decision="${escAttr(action.human_decision || action.id || '')}" data-target-state="${escAttr(targetState)}" data-requires-confirmation="${action.requires_confirmation ? 'true' : 'false'}">${esc(action.label || action.id || 'Submit decision')}</button>${resultLabel}</span>`;
      }).join(' ');
      return `<div class="card" data-review-card="true" data-review-lane="${escAttr(lane || '')}" data-review-reason-code="${escAttr(bundle?.reason_code || '')}" data-review-suggested-decision="${escAttr(bundle?.suggested_decision || bundle?.suggested_human_decision || '')}" data-review-source-detector="${escAttr(bundle?.diagnostic_status || 'backboard-review-inbox')}" data-review-draft-exists="${draft.exists ? 'true' : 'false'}" ${selectableItemAttrs(item)}>${renderItemCardSummary(item)}<div class="muted review-reason"><strong>Why this needs review:</strong> ${reasonCode}${esc(reason)}${decision}</div><label class="muted" style="display:block;margin-top:8px;">Draft review note<textarea data-review-draft-note="true" rows="3" style="width:100%;box-sizing:border-box;margin-top:4px;" placeholder="Write rationale or instructions before submitting">${esc(draftText)}</textarea></label><div class="muted" data-review-draft-state="true" style="margin-top:4px;">${draftStatus}</div><div class="review-actions" style="display:flex;flex-wrap:wrap;gap:6px;margin-top:8px;"><button class="btn" data-review-action="save-draft">Save Draft</button><button class="btn" data-review-action="discard-draft">Discard Draft</button>${actions}</div><div class="muted" data-review-status style="margin-top:6px;"></div></div>`;
    }

    function reviewPayloadFromCard(card, button, confirmed = false) {
      const note = card.querySelector('[data-review-draft-note]')?.value || '';
      return {
        product: card.getAttribute('data-item-product') || '',
        item_id: card.getAttribute('data-item-id') || '',
        lane: card.getAttribute('data-review-lane') || '',
        reason_code: card.getAttribute('data-review-reason-code') || '',
        suggested_decision: card.getAttribute('data-review-suggested-decision') || '',
        human_decision: button?.getAttribute('data-human-decision') || '',
        rationale: note,
        actor_alias: state.reviewActorAlias,
        target_state: button?.getAttribute('data-target-state') || '',
        source_detector: card.getAttribute('data-review-source-detector') || 'backboard-review-inbox',
        confirmed
      };
    }

    function setReviewStatus(card, text, isError = false) {
      const status = card.querySelector('[data-review-status]');
      if (!status) return;
      status.textContent = text;
      status.style.color = isError ? '#b42318' : '';
    }

    function bindReviewActions(selector) {
      document.querySelectorAll(`${selector} [data-review-action]`).forEach((button) => {
        button.addEventListener('click', async (event) => {
          event.preventDefault();
          event.stopPropagation();
          const card = button.closest('[data-review-card]');
          if (!card) return;
          try {
            if (button.getAttribute('data-review-action') === 'save-draft') {
              await postJson('/api/review/decision/draft', reviewPayloadFromCard(card, null));
              setReviewStatus(card, 'Draft saved.');
              card.setAttribute('data-review-draft-exists', 'true');
              const draftState = card.querySelector('[data-review-draft-state]');
              if (draftState) draftState.textContent = `Draft saved for ${state.reviewActorAlias}.`;
              return;
            }
            if (button.getAttribute('data-review-action') === 'discard-draft') {
              await postJson('/api/review/decision/draft/discard', reviewPayloadFromCard(card, null));
              const note = card.querySelector('[data-review-draft-note]');
              if (note) note.value = '';
              card.setAttribute('data-review-draft-exists', 'false');
              const draftState = card.querySelector('[data-review-draft-state]');
              if (draftState) draftState.textContent = 'No saved draft.';
              setReviewStatus(card, 'Draft discarded.');
              return;
            }
            const needsConfirmation = button.getAttribute('data-requires-confirmation') === 'true';
            const targetState = button.getAttribute('data-target-state') || '';
            const confirmed = needsConfirmation
              ? window.confirm(`This is a high-risk review action. Resulting state: ${targetState || 'no state change'}. Submit only after explicit human confirmation.`)
              : false;
            if (needsConfirmation && !confirmed) {
              setReviewStatus(card, 'Submission cancelled before confirmation.');
              return;
            }
            const result = await postJson('/api/review/decision/submit', reviewPayloadFromCard(card, button, confirmed));
            setReviewStatus(card, `Decision submitted: ${result?.data?.path || 'recorded'}.`);
          } catch (error) {
            setReviewStatus(card, error.message || String(error), true);
          }
        });
      });
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
      const label = `<span class="node-line"><span>${typeIcon(node.type)}</span><code>${esc(node.id)}</code><a href="#" class="item-link" data-item-id="${escAttr(node.id)}" data-item-product="${escAttr(node.product || '')}">${esc(node.title)}</a><span class="muted">(${esc(renderMeta(node))})</span>${renderTreeNavigation(node)}</span>`;
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
      const backdrop = document.getElementById('item-modal-backdrop');
      backdrop.classList.add('open');
      backdrop.setAttribute('aria-hidden', 'false');
    }

    function closeModal() {
      const backdrop = document.getElementById('item-modal-backdrop');
      backdrop.classList.remove('open');
      backdrop.setAttribute('aria-hidden', 'true');
    }

    function describeItemDetailError(error, diagnostic) {
      const itemId = diagnostic?.item_id || 'unknown item';
      const product = diagnostic?.product_scope || 'all';
      const elapsed = diagnostic?.elapsed_ms ? `${Math.ceil(diagnostic.elapsed_ms / 1000)}s` : 'unknown duration';
      if (diagnostic?.timed_out || error?.name === 'AbortError') {
        return `Item detail lookup timed out after ${elapsed} for ${product}/${itemId}`;
      }
      return `Item detail lookup failed for ${product}/${itemId}: ${error?.message || String(error || 'unknown error')}`;
    }

    async function fetchItemDetailPartial(itemId, productScope, requestId) {
      const timeoutMs = 60000;
      const endpoint = `/partials/item/${encodeURIComponent(itemId)}?product=${encodeURIComponent(productScope)}`;
      const controller = new AbortController();
      const startedAt = Date.now();
      const diagnostic = {
        request_id: `item-detail-${requestId}`,
        item_id: itemId,
        product_scope: productScope,
        endpoint: endpointPath(endpoint),
        operation: `GET ${endpoint}`,
        started_at: new Date(startedAt).toISOString(),
        timeout_ms: timeoutMs,
        status: 'running',
      };
      state.lastDetailDiagnostic = diagnostic;
      const timeout = setTimeout(() => {
        diagnostic.timed_out = true;
        controller.abort();
      }, timeoutMs);
      try {
        const resp = await fetch(endpoint, {
          signal: controller.signal,
          headers: { 'Accept': 'text/html' },
        });
        const html = await resp.text();
        diagnostic.http_status = resp.status;
        if (!resp.ok) {
          throw new Error(`HTTP ${resp.status}`);
        }
        diagnostic.status = 'loaded';
        return html;
      } catch (error) {
        diagnostic.status = diagnostic.timed_out || error?.name === 'AbortError'
          ? 'timeout'
          : 'failed';
        diagnostic.error_name = error?.name || '';
        diagnostic.error = describeItemDetailError(error, diagnostic);
        throw error;
      } finally {
        clearTimeout(timeout);
        diagnostic.finished_at = nowIso();
        diagnostic.elapsed_ms = Date.now() - startedAt;
      }
    }

    async function openItemModal(itemId, product = '') {
      const productScope = product || (selectedProductValues().length === 1 ? selectedProductValues()[0] : 'all');
      const detailRequest = ++state.detailSeq;
      openModal(itemId, '<div class="muted">Loading item detail...</div>');
      try {
        const html = await fetchItemDetailPartial(itemId, productScope, detailRequest);
        if (detailRequest !== state.detailSeq) {
          return;
        }
        document.getElementById('item-modal-body').innerHTML = html;
        hydrateMarkdown('#item-modal-body');
        bindItemLinksWithin('#item-modal-body', productScope);
        const titleSource = document.querySelector('#item-modal-body [data-item-title]');
        document.getElementById('item-modal-title').textContent =
          titleSource?.getAttribute('data-item-title') || itemId;
      } catch (error) {
        if (detailRequest !== state.detailSeq) {
          return;
        }
        openModal(itemId, `<div class="muted">Unable to load item detail: ${esc(describeItemDetailError(error, state.lastDetailDiagnostic))}</div>`);
      }
    }

    async function restoreItemModalFromQuery() {
      if (!state.graphItemId || state.activeTab === 'graph') {
        return;
      }
      await openItemModal(state.graphItemId, state.graphItemProduct || '');
    }

    function isTypingContext(target) {
      const element = target?.nodeType === Node.TEXT_NODE ? target.parentElement : target;
      if (!element) return false;
      if (element.isContentEditable) return true;
      const editableParent = element.closest ? element.closest('[contenteditable="true"], [contenteditable=""]') : null;
      if (editableParent) return true;
      const tag = String(element.tagName || '').toUpperCase();
      return tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT';
    }

    function itemSelectionKey(id, product) {
      return `${String(product || '')}::${String(id || '')}`;
    }

    function itemSelectionKeyFromElement(element) {
      if (!element) return '';
      return itemSelectionKey(
        element.getAttribute('data-item-id') || '',
        element.getAttribute('data-item-product') || ''
      );
    }

    function setSelectedItemState(id, product, visibleIndex = -1) {
      state.selectedItemId = String(id || '');
      state.selectedItemProduct = String(product || '');
      state.selectedItemKey = state.selectedItemId ? itemSelectionKey(state.selectedItemId, state.selectedItemProduct) : '';
      state.selectedItemVisibleIndex = Number.isInteger(visibleIndex) ? visibleIndex : -1;
    }

    function activeSelectableCards() {
      const activePage = document.querySelector('.page.active');
      if (!activePage) return [];
      return [...activePage.querySelectorAll('[data-selectable-item]')].filter((card) =>
        card.getAttribute('data-item-id') && card.getClientRects().length > 0
      );
    }

    function findSelectedVisibleCard(visibleCards) {
      if (!state.selectedItemKey) return null;
      const indexedCard = visibleCards[state.selectedItemVisibleIndex];
      if (indexedCard && itemSelectionKeyFromElement(indexedCard) === state.selectedItemKey) {
        return indexedCard;
      }
      return visibleCards.find((card) => itemSelectionKeyFromElement(card) === state.selectedItemKey) || null;
    }
)JS";

inline constexpr std::string_view kBackboardAppJsPart4 = R"JS(

    function syncSelectableItems() {
      const visibleCards = activeSelectableCards();
      let selectedCard = findSelectedVisibleCard(visibleCards);
      if (!visibleCards.length) {
        setSelectedItemState('', '');
      } else {
        selectedCard = selectedCard || visibleCards[0];
        setSelectedItemState(
          selectedCard.getAttribute('data-item-id') || '',
          selectedCard.getAttribute('data-item-product') || '',
          visibleCards.indexOf(selectedCard)
        );
      }

      document.querySelectorAll('[data-selectable-item]').forEach((card) => {
        const selected = card === selectedCard;
        card.classList.toggle('is-selected', Boolean(selected));
        card.setAttribute('aria-selected', selected ? 'true' : 'false');
        card.setAttribute('tabindex', selected ? '0' : '-1');
      });
    }

    function setSelectedCard(card, options = {}) {
      if (!card) {
        setSelectedItemState('', '');
        syncSelectableItems();
        return false;
      }
      const visibleCards = activeSelectableCards();
      setSelectedItemState(
        card.getAttribute('data-item-id') || '',
        card.getAttribute('data-item-product') || '',
        visibleCards.indexOf(card)
      );
      syncSelectableItems();
      if (options.scroll !== false) {
        card.scrollIntoView({ block: 'nearest', inline: 'nearest' });
      }
      if (options.focus) {
        card.focus({ preventScroll: true });
      }
      return true;
    }

    function bindSelectableCards(root) {
      const container = typeof root === 'string' ? document.querySelector(root) : (root || document);
      if (!container) return;
      container.querySelectorAll('[data-selectable-item]').forEach((card) => {
        if (card.__kobSelectableBound) return;
        card.__kobSelectableBound = true;
        card.addEventListener('click', (event) => {
          setSelectedCard(card, { focus: false, scroll: false });
          if (event.target?.closest?.('.item-link, button, input, select, textarea, summary')) {
            return;
          }
        });
        card.addEventListener('focus', () => {
          setSelectedCard(card, { focus: false, scroll: false });
        });
      });
      syncSelectableItems();
    }

    function selectItemByDelta(delta) {
      const cards = activeSelectableCards();
      if (!cards.length) return false;
      const currentCard = findSelectedVisibleCard(cards);
      const currentIndex = currentCard ? cards.indexOf(currentCard) : -1;
      const nextIndex = currentIndex >= 0
        ? (currentIndex + delta + cards.length) % cards.length
        : (delta >= 0 ? 0 : cards.length - 1);
      return setSelectedCard(cards[nextIndex], { focus: true, scroll: true });
    }

    async function openSelectedItem() {
      const cards = activeSelectableCards();
      if (!cards.length) return false;
      const selected = findSelectedVisibleCard(cards) || cards[0];
      setSelectedCard(selected, { focus: false, scroll: false });
      const itemId = selected.getAttribute('data-item-id') || '';
      const product = selected.getAttribute('data-item-product') || '';
      if (!itemId) return false;
      await openItemModal(itemId, product);
      return true;
    }

    function isShortcutHelpOpen() {
      return state.shortcutHelpOpen;
    }

    function openShortcutHelp() {
      state.shortcutHelpOpen = true;
      const backdrop = document.getElementById('shortcut-help-backdrop');
      backdrop.classList.add('open');
      backdrop.setAttribute('aria-hidden', 'false');
      document.getElementById('shortcut-help-button').setAttribute('aria-expanded', 'true');
      document.getElementById('shortcut-help-close').focus({ preventScroll: true });
    }

    function closeShortcutHelp() {
      if (!state.shortcutHelpOpen) return false;
      state.shortcutHelpOpen = false;
      const backdrop = document.getElementById('shortcut-help-backdrop');
      backdrop.classList.remove('open');
      backdrop.setAttribute('aria-hidden', 'true');
      document.getElementById('shortcut-help-button').setAttribute('aria-expanded', 'false');
      document.getElementById('shortcut-help-button').focus({ preventScroll: true });
      return true;
    }

    function toggleShortcutHelp() {
      if (isShortcutHelpOpen()) {
        closeShortcutHelp();
      } else {
        openShortcutHelp();
      }
    }

    function closeOpenDetailPanels() {
      const openPanels = [...document.querySelectorAll('.detail-toggle[open]')];
      if (!openPanels.length) return false;
      openPanels[openPanels.length - 1].open = false;
      return true;
    }

    function isItemModalOpen() {
      return document.getElementById('item-modal-backdrop').classList.contains('open');
    }

    function focusSearchInput() {
      const search = document.getElementById('search');
      search.focus();
      search.select();
    }

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

    function renderProductSelectOptions() {
      const select = document.getElementById('product');
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
      syncProductSelect();
    }
)JS";

inline constexpr std::string_view kBackboardAppJsPart5 = R"JS(

    async function loadProducts(signal) {
      if (!document.getElementById('product').options.length) {
        renderProductSelectOptions();
        renderFilters();
        document.getElementById('product-filters').insertAdjacentHTML(
            'beforeend',
            '<span class="muted">Loading products...</span>');
      }

      try {
        const result = await getJson('/api/products', { signal });
        state.products = result.data || [];
      } catch (error) {
        renderProductSelectOptions();
        renderFilters();
        setStatus(`Product list unavailable: ${describeRefreshError(error)}`, 'error');
        return;
      }

      const currentProducts = selectedProductValues();
      const validSelection = currentProducts.length === 1 &&
          (currentProducts[0] === 'all' || state.products.includes(currentProducts[0]));
      if (!validSelection) {
        state.selectedProducts = new Set(['all']);
      }
      renderProductSelectOptions();
      renderFilters();
    }

    async function loadTree(signal, refresh) {
      const result = await getJson(`/api/tree?${queryString()}`, {
        signal,
        refresh,
        stage: 'tree.items',
      });
      const roots = result?.data?.roots || [];
      if (!state.treeTouched && state.treeOpen.size === 0) {
        for (const root of roots) {
          if (root.id) state.treeOpen.add(treeNodeKey(root));
        }
      }
      document.getElementById('tree').innerHTML = roots.length
        ? `<ul>${roots.map((node) => renderTreeNode(node, 0)).join('')}</ul>`
        : '<div class="muted">No items for the current filters.</div>';
      bindTreeToggles();
    }

    async function loadKanban(signal, refresh) {
      const result = await getJson(`/api/kanban?${queryString()}`, {
        signal,
        refresh,
        stage: 'kanban.lanes',
      });
      const lanesData = result?.data?.lanes || {};
      const html = lanes.map((lane) => {
        const cards = (lanesData[lane] || [])
          .map((item) => renderItemCard(item)).join('');
        return `<div class="lane"><strong>${lane}</strong><div class="lane-items" role="listbox" aria-label="${escAttr(`${lane} items`)}">${cards || '<div class="muted">No items</div>'}</div></div>`;
      }).join('');
      document.getElementById('kanban').innerHTML = html;
      bindItemLinks('#kanban');
      bindSelectableCards('#kanban');
    }

    async function loadContext(signal, refresh) {
      const result = await getJson(`/api/items?${queryString()}`, {
        signal,
        refresh,
        stage: 'context.items',
      });
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

      const listHtml = contextItems.map((item) => renderItemCard(item)).join('');
      document.getElementById('context-list').innerHTML = listHtml
        ? `<div class="context-items" role="listbox" aria-label="Context items">${listHtml}</div>`
        : '<div class="muted">No context items</div>';

      bindItemLinks('#context-list');
      bindSelectableCards('#context-list');
    }

    async function loadReview(signal, refresh) {
      const viewsResult = await getJson('/api/review/saved-views', {
        signal,
        refresh,
        stage: 'review.saved_views',
      });
      const views = viewsResult?.data?.views || [];
      document.getElementById('saved-views').innerHTML = views.map((view) =>
        `<button class="btn" data-saved-view="${escAttr(view.id)}">${esc(view.title)}</button>`
      ).join('');

      const inboxResult = await getJson(`/api/review/inbox?${queryString()}`, {
        signal,
        refresh,
        stage: 'review.inbox',
      });
      const lanesData = inboxResult?.data?.lanes || {};
      const laneNames = reviewQueueOrder.filter((lane) => Object.prototype.hasOwnProperty.call(lanesData, lane));
      document.getElementById('review-inbox').innerHTML = laneNames.map((lane) => {
        const bundles = lanesData[lane] || [];
        const cards = bundles.map((bundle) => renderReviewCard(bundle, lane)).join('');
        return `<div class="review-lane"><strong>${esc(lane)}</strong><div class="muted">${bundles.length} item(s)</div><div class="review-lane-items" role="listbox" aria-label="${escAttr(`${lane} items`)}">${cards || '<div class="muted">No items</div>'}</div></div>`;
      }).join('') || '<div class="muted">No review queues for the current filters.</div>';
      bindItemLinks('#review-inbox');
      bindSelectableCards('#review-inbox');
      bindReviewActions('#review-inbox');

      document.querySelectorAll('#saved-views [data-saved-view]').forEach((button) => {
        button.addEventListener('click', async () => {
          const viewId = button.getAttribute('data-saved-view');
          const result = await getJson(`/api/review/saved-views/${encodeURIComponent(viewId)}?${queryString()}`);
          const items = result?.data?.result?.items || [];
          document.getElementById('review-inbox').innerHTML =
            `<div class="review-lane"><strong>${esc(result?.data?.view?.title || viewId)}</strong><div class="muted">${items.length} item(s)</div><div class="review-lane-items" role="listbox" aria-label="${escAttr(`${result?.data?.view?.title || viewId} items`)}">${items.map(renderItemCard).join('') || '<div class="muted">No items</div>'}</div></div>`;
          bindItemLinks('#review-inbox');
          bindSelectableCards('#review-inbox');
        });
      });
    }
)JS";

inline constexpr std::string_view kBackboardAppJsPart5a = R"JS(

    function renderHandoffIssue(issue) {
      return `<div class="card gap-card"><strong>${esc(issue.code || 'gap')}</strong><div>${esc(issue.message || '')}</div><div class="muted">${esc(issue.severity || '')}</div></div>`;
    }

    function renderHandoffPreviewField(label, value) {
      if (Array.isArray(value)) {
        const chips = value.map((entry) => `<span class="pill">${esc(entry)}</span>`).join('');
        return `<div><div class="detail-label">${esc(label)}</div><div class="detail-links">${chips || '<span class="muted">Missing</span>'}</div></div>`;
      }
      return `<div><div class="detail-label">${esc(label)}</div><div class="detail-value">${value ? esc(value) : '<span class="muted">Missing</span>'}</div></div>`;
    }

    function renderHandoffReadinessRow(row) {
      const preview = row.handoff_preview || {};
      const blockers = (row.blockers || []).map(renderHandoffIssue).join('');
      const gaps = [...(row.gaps || []), ...(row.diagnostics || [])].map(renderHandoffIssue).join('');
      return `<div class="card handoff-row" data-handoff-readiness-row="${escAttr(row.item_id || '')}" data-selectable-item="true" data-item-id="${escAttr(row.item_id || '')}" data-item-product="${escAttr(row.product || '')}" tabindex="-1" aria-selected="false" role="option">` +
        `<div class="detail-title-row"><strong><a href="#" class="item-link" data-item-id="${escAttr(row.item_id || '')}" data-item-product="${escAttr(row.product || '')}">${esc(row.title || row.item_id || '')}</a></strong>${pill(row.status || 'unknown')}</div>` +
        `<div class="muted"><code>${esc(row.item_id || '')}</code> / ${esc(row.product || '')} / ${esc(row.type || '')} / ${esc(row.state || '')}</div>` +
        `<div class="detail-facts">` +
          `<div class="detail-fact"><span class="detail-label">Safe to hand off</span><div class="detail-value">${row.safe_to_handoff ? 'true' : 'false'}</div></div>` +
          `<div class="detail-fact"><span class="detail-label">Blockers</span><div class="detail-value">${row.blocker_count || 0}</div></div>` +
          `<div class="detail-fact"><span class="detail-label">Gaps</span><div class="detail-value">${row.gap_count || 0}</div></div>` +
          `<div class="detail-fact"><span class="detail-label">Dispatch boundary</span><div class="detail-value">KOA / Ark Console</div></div>` +
        `</div>` +
        `<div class="handoff-preview-grid">` +
          renderHandoffPreviewField('Repo', preview.repo) +
          renderHandoffPreviewField('Goal', preview.goal) +
          renderHandoffPreviewField('Non-goals', preview.non_goals) +
          renderHandoffPreviewField('Validation commands', preview.validation_commands || []) +
          renderHandoffPreviewField('Expected result artifact', preview.expected_result_artifact) +
          renderHandoffPreviewField('Reporting format', preview.reporting_format) +
        `</div>` +
        `<div><div class="detail-label">Blockers</div>${blockers || '<div class="muted">No blockers</div>'}</div>` +
        (gaps ? `<div><div class="detail-label">Gaps</div>${gaps}</div>` : '') +
        `<div class="muted">${esc(row.recommended_human_action || '')}</div>` +
      `</div>`;
    }

    async function loadHandoffReadiness(signal, refresh) {
      const result = await getJson(`/api/review/handoff-readiness?${queryString()}`, {
        signal,
        refresh,
        stage: 'handoff.readiness',
      });
      const data = result?.data || {};
      const rows = data.rows || [];
      document.getElementById('handoff-summary').textContent =
        `${rows.length} candidate(s), ${data.safe_candidate_count || 0} safe, ${data.blocked_count || 0} blocked, ${data.gap_count || 0} gap(s)`;
      document.getElementById('handoff-list').innerHTML = rows.length
        ? `<div class="handoff-readiness-list" role="listbox" aria-label="Handoff readiness candidates">${rows.map(renderHandoffReadinessRow).join('')}</div>`
        : `<div class="muted">${esc(data.empty_state || 'No handoff rows.')}</div>`;
      bindItemLinks('#handoff-list');
      bindSelectableCards('#handoff-list');
    }

    function renderRoadmapDiagnostic(diagnostic) {
      return `<div class="card gap-card"><strong>${esc(diagnostic.code || 'gap')}</strong><div>${esc(diagnostic.message || '')}</div><div class="muted">${esc(diagnostic.target || '')}</div></div>`;
    }

    function renderRoadmapGoal(goal) {
      const refs = (goal.linked_refs || []).map(renderTreeRefChip).join('');
      const diagnostics = (goal.diagnostics || []).map(renderRoadmapDiagnostic).join('');
      const gap = goal.gap_state
        ? `<div><div class="detail-label">Gap state</div><div class="detail-value">${esc(goal.gap_state)}</div></div>`
        : '';
      const rationale = goal.rationale
        ? `<div><div class="detail-label">Decision rationale</div><div class="detail-value">${esc(goal.rationale)}</div></div>`
        : '';
      return `<div class="card roadmap-goal" data-roadmap-goal="${escAttr(goal.goal_id || '')}">` +
        `<div class="detail-title-row"><strong>${esc(goal.summary || goal.goal_id || 'Goal')}</strong>${pill(goal.status || 'Unknown')}</div>` +
        `<div class="muted"><code>${esc(goal.goal_id || '')}</code> / ${esc(goal.product || '')} / target ${esc(goal.target_version || 'unknown')}</div>` +
        `<div class="detail-facts">` +
          `<div class="detail-fact"><span class="detail-label">Declared</span><div class="detail-value">${esc(goal.declared_status || 'Unknown')}</div></div>` +
          `<div class="detail-fact"><span class="detail-label">Evidence quality</span><div class="detail-value">${esc(goal.evidence_quality || 'unclear')}</div></div>` +
          `<div class="detail-fact"><span class="detail-label">Closed tickets</span><div class="detail-value">${goal.closed_ticket_count || 0}</div></div>` +
          `<div class="detail-fact"><span class="detail-label">Evidence-backed</span><div class="detail-value">${goal.evidence_backed_count || 0}</div></div>` +
          `<div class="detail-fact"><span class="detail-label">Implemented/unverified</span><div class="detail-value">${goal.implemented_unverified_count || 0}</div></div>` +
        `</div>` +
        gap + rationale +
        `<div><div class="detail-label">Linked refs</div><div class="detail-links">${refs || '<div class="muted">No linked refs; roadmap status is unsupported</div>'}</div></div>` +
        (diagnostics ? `<div><div class="detail-label">Gaps</div>${diagnostics}</div>` : '') +
        `<div class="muted">${esc(goal.status_reason || '')}</div>` +
      `</div>`;
    }

    function renderRoadmapSlice(label, goals) {
      const pretty = label.charAt(0).toUpperCase() + label.slice(1);
      const body = goals.length
        ? goals.map(renderRoadmapGoal).join('')
        : `<div class="muted">No ${esc(label)} roadmap goals for this scope.</div>`;
      return `<section class="roadmap-slice" data-roadmap-slice="${escAttr(label)}"><h4>${esc(pretty)}</h4>${body}</section>`;
    }

    async function loadRoadmap(signal, refresh) {
      const result = await getJson(`/api/review/roadmap?${queryString()}`, {
        signal,
        refresh,
        stage: 'roadmap.version_goals',
      });
      const data = result?.data || {};
      const goals = data.goals || [];
      const diagnostics = data.diagnostics || [];
      const counts = data.status_counts || {};
      const countText = Object.keys(counts).map((key) => `${key}: ${counts[key] || 0}`).join(' | ');
      document.getElementById('roadmap-summary').textContent =
        `${goals.length} goal(s), ${diagnostics.length} gap(s)${countText ? ` | ${countText}` : ''}`;
      const slices = data.slices || {};
      document.getElementById('roadmap-list').innerHTML = [
        renderRoadmapSlice('current', slices.current || []),
        renderRoadmapSlice('next', slices.next || []),
        renderRoadmapSlice('future', slices.future || []),
        diagnostics.length
          ? `<section class="roadmap-diagnostics"><h4>Roadmap gaps</h4>${diagnostics.slice(0, 40).map(renderRoadmapDiagnostic).join('')}</section>`
          : '',
        goals.length ? '' : `<div class="muted">${esc(data.empty_state || 'No roadmap goals.')}</div>`,
      ].join('');
      bindItemLinks('#roadmap-list');
    }

)JS";

inline constexpr std::string_view kBackboardAppJsPart5aa = R"JS(

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

    function incrementGraphReasonCount(counts, reason) {
      if (!reason) {
        return;
      }
      counts.set(reason, (counts.get(reason) || 0) + 1);
    }

    function graphFocusNodeId(nodes) {
      const requested = String(state.graphItemId || '').trim();
      if (!requested) {
        return '';
      }
      const direct = nodes.find((node) => String(node.id || '') === requested);
      if (direct) {
        return String(direct.id || '');
      }
      const byItemId = nodes.find((node) => String(node.item_id || '') === requested);
      return byItemId ? String(byItemId.id || '') : '';
    }

    function graphEdgeIsolationReason(fromReason, toReason) {
      if (!fromReason && !toReason) {
        return '';
      }
      if (fromReason === 'disconnected' || toReason === 'disconnected') {
        return 'disconnected';
      }
      if (fromReason === 'outside-depth' || toReason === 'outside-depth') {
        return 'outside-depth';
      }
      if (fromReason === 'root-missing' || toReason === 'root-missing') {
        return 'root-missing';
      }
      return fromReason || toReason || 'unrelated';
    }

    function buildGraphIsolation(nodes, edges) {
      const mode = normalizeGraphIsolationMode(state.graphIsolationMode);
      const maxDepth = boundedPositiveInt(state.graphMaxDepth, defaultGraphCaps.maxDepth, graphDepthBounds.max);
      const focusNodeId = graphFocusNodeId(nodes);
      const adjacency = new Map(nodes.map((node) => [String(node.id || ''), new Set()]));
      edges.forEach((edge) => {
        const fromId = String(edge.from || '');
        const toId = String(edge.to || '');
        if (!adjacency.has(fromId) || !adjacency.has(toId)) {
          return;
        }
        adjacency.get(fromId).add(toId);
        adjacency.get(toId).add(fromId);
      });

      const distances = new Map();
      if (focusNodeId && adjacency.has(focusNodeId)) {
        const queue = [{ id: focusNodeId, depth: 0 }];
        distances.set(focusNodeId, 0);
        while (queue.length) {
          const current = queue.shift();
          const neighbors = adjacency.get(current.id) || new Set();
          neighbors.forEach((neighborId) => {
            if (distances.has(neighborId)) {
              return;
            }
            const nextDepth = current.depth + 1;
            distances.set(neighborId, nextDepth);
            queue.push({ id: neighborId, depth: nextDepth });
          });
        }
      }

      const rootMissing = !focusNodeId && !!state.graphItemId;
      const nodeVisibility = new Map();
      const nodeReason = new Map();
      const nodeReasonCounts = new Map();
      nodes.forEach((node) => {
        const nodeId = String(node.id || '');
        let reason = 'within-depth';
        let visibility = 'visible';
        if (rootMissing) {
          reason = 'root-missing';
        } else if (nodeId === focusNodeId) {
          reason = 'focus-root';
        } else {
          const distance = distances.get(nodeId);
          if (Number.isFinite(distance) && distance <= maxDepth) {
            reason = 'within-depth';
          } else if (Number.isFinite(distance)) {
            reason = 'outside-depth';
            visibility = mode === 'hide' ? 'hidden' : 'faded';
          } else {
            reason = 'disconnected';
            visibility = mode === 'hide' ? 'hidden' : 'faded';
          }
        }
        nodeVisibility.set(nodeId, visibility);
        nodeReason.set(nodeId, reason);
        if (visibility !== 'visible') {
          incrementGraphReasonCount(nodeReasonCounts, reason);
        }
      });

      const edgeVisibility = new Map();
      const edgeReason = new Map();
      const edgeReasonCounts = new Map();
      edges.forEach((edge, index) => {
        const key = `${String(edge.from || '')}->${String(edge.to || '')}#${index}`;
        const fromVisibility = nodeVisibility.get(String(edge.from || '')) || 'visible';
        const toVisibility = nodeVisibility.get(String(edge.to || '')) || 'visible';
        const relatedToUnscopedNode = fromVisibility !== 'visible' || toVisibility !== 'visible';
        const visibility = mode === 'hide' && relatedToUnscopedNode
          ? 'hidden'
          : (relatedToUnscopedNode ? 'faded' : 'visible');
        const reason = graphEdgeIsolationReason(
          nodeReason.get(String(edge.from || '')) || '',
          nodeReason.get(String(edge.to || '')) || ''
        );
        edgeVisibility.set(key, visibility);
        edgeReason.set(key, visibility === 'visible' ? '' : reason);
        if (visibility !== 'visible') {
          incrementGraphReasonCount(edgeReasonCounts, reason);
        }
      });

      return {
        mode,
        maxDepth,
        focusNodeId,
        rootMissing,
        nodeVisibility,
        nodeReason,
        nodeReasonCounts,
        edgeVisibility,
        edgeReason,
        edgeReasonCounts,
        visibleNodeCount: [...nodeVisibility.values()].filter((value) => value === 'visible').length,
        fadedNodeCount: [...nodeVisibility.values()].filter((value) => value === 'faded').length,
        hiddenNodeCount: [...nodeVisibility.values()].filter((value) => value === 'hidden').length,
        visibleEdgeCount: [...edgeVisibility.values()].filter((value) => value === 'visible').length,
        fadedEdgeCount: [...edgeVisibility.values()].filter((value) => value === 'faded').length,
        hiddenEdgeCount: [...edgeVisibility.values()].filter((value) => value === 'hidden').length,
      };
    }

    function normalizeGraphBounds(bounds) {
      if (!bounds) {
        return null;
      }
      const x = Number(bounds.x);
      const y = Number(bounds.y);
      const width = Number(bounds.width);
      const height = Number(bounds.height);
      if (!Number.isFinite(x) || !Number.isFinite(y) || !Number.isFinite(width) || !Number.isFinite(height) || width <= 0 || height <= 0) {
        return null;
      }
      return { x, y, width, height };
    }

    function mergeGraphBounds(current, next) {
      const a = normalizeGraphBounds(current);
      const b = normalizeGraphBounds(next);
      if (!a) return b;
      if (!b) return a;
      const minX = Math.min(a.x, b.x);
      const minY = Math.min(a.y, b.y);
      const maxX = Math.max(a.x + a.width, b.x + b.width);
      const maxY = Math.max(a.y + a.height, b.y + b.height);
      return {
        x: minX,
        y: minY,
        width: maxX - minX,
        height: maxY - minY,
      };
    }

    function expandGraphBounds(bounds, padding) {
      const value = normalizeGraphBounds(bounds);
      if (!value) {
        return null;
      }
      const inset = Math.max(0, Number(padding) || 0);
      return {
        x: value.x - inset,
        y: value.y - inset,
        width: value.width + inset * 2,
        height: value.height + inset * 2,
      };
    }

    function graphCanvasElements() {
      return {
        canvas: document.getElementById('graph-canvas'),
        svg: document.getElementById('graph-svg'),
        viewportLayer: document.getElementById('graph-viewport-layer'),
      };
    }

    function measureGraphViewport() {
      const { canvas, svg } = graphCanvasElements();
      if (!canvas || !svg) {
        return null;
      }
      const rect = canvas.getBoundingClientRect();
      const width = Math.max(graphViewportBounds.minViewportWidth, Math.round(rect.width || canvas.clientWidth || 0));
      const height = Math.max(graphViewportBounds.minViewportHeight, Math.round(rect.height || canvas.clientHeight || 0));
      state.graphViewport.viewportWidth = width;
      state.graphViewport.viewportHeight = height;
      svg.setAttribute('viewBox', `0 0 ${width} ${height}`);
      return { width, height };
    }

    function clampGraphViewport(viewport = state.graphViewport) {
      const fullBounds = normalizeGraphBounds(viewport.fullBounds);
      const viewportWidth = Math.max(graphViewportBounds.minViewportWidth, Number(viewport.viewportWidth) || 0);
      const viewportHeight = Math.max(graphViewportBounds.minViewportHeight, Number(viewport.viewportHeight) || 0);
      const scale = Math.min(
        graphViewportBounds.maxScale,
        Math.max(graphViewportBounds.minScale, Number(viewport.scale) || 1)
      );
      let x = Number(viewport.x) || 0;
      let y = Number(viewport.y) || 0;
      if (!fullBounds) {
        return { scale, x, y };
      }

      const scaledWidth = fullBounds.width * scale;
      const scaledHeight = fullBounds.height * scale;
      const centeredX = ((viewportWidth - scaledWidth) / 2) - (fullBounds.x * scale);
      const centeredY = ((viewportHeight - scaledHeight) / 2) - (fullBounds.y * scale);

      if (scaledWidth + graphViewportBounds.panPadding * 2 <= viewportWidth) {
        x = centeredX;
      } else {
        const minX = viewportWidth - ((fullBounds.x + fullBounds.width) * scale) - graphViewportBounds.panPadding;
        const maxX = graphViewportBounds.panPadding - (fullBounds.x * scale);
        x = Math.max(minX, Math.min(maxX, x));
      }

      if (scaledHeight + graphViewportBounds.panPadding * 2 <= viewportHeight) {
        y = centeredY;
      } else {
        const minY = viewportHeight - ((fullBounds.y + fullBounds.height) * scale) - graphViewportBounds.panPadding;
        const maxY = graphViewportBounds.panPadding - (fullBounds.y * scale);
        y = Math.max(minY, Math.min(maxY, y));
      }

      return { scale, x, y };
    }

    function updateGraphViewportButtons() {
      const zoomOut = document.getElementById('graph-zoom-out');
      const zoomIn = document.getElementById('graph-zoom-in');
      const fitAll = document.getElementById('graph-fit-all');
      const fitFocus = document.getElementById('graph-fit-focus');
      const resetView = document.getElementById('graph-reset-view');
      const scale = Number(state.graphViewport.scale) || 1;
      if (zoomOut) {
        zoomOut.disabled = scale <= graphViewportBounds.minScale + 0.0001;
      }
      if (zoomIn) {
        zoomIn.disabled = scale >= graphViewportBounds.maxScale - 0.0001;
      }
      if (fitAll) {
        fitAll.disabled = !normalizeGraphBounds(state.graphViewport.fullBounds);
      }
      if (fitFocus) {
        fitFocus.disabled = !normalizeGraphBounds(state.graphViewport.focusBounds);
      }
      if (resetView) {
        resetView.disabled = !normalizeGraphBounds(state.graphViewport.fullBounds);
      }
    }

    function applyGraphViewport() {
      const elements = graphCanvasElements();
      if (!elements.canvas || !elements.viewportLayer) {
        return;
      }
      if (!measureGraphViewport()) {
        return;
      }
      const clamped = clampGraphViewport();
      state.graphViewport.scale = clamped.scale;
      state.graphViewport.x = clamped.x;
      state.graphViewport.y = clamped.y;
      elements.viewportLayer.setAttribute(
        'transform',
        `translate(${state.graphViewport.x} ${state.graphViewport.y}) scale(${state.graphViewport.scale})`
      );
      elements.canvas.setAttribute('data-graph-scale', state.graphViewport.scale.toFixed(2));
      updateGraphViewportButtons();
    }

    function setGraphViewport(nextViewport = {}, options = {}) {
      state.graphViewport.scale = Number(nextViewport.scale ?? state.graphViewport.scale) || 1;
      state.graphViewport.x = Number(nextViewport.x ?? state.graphViewport.x) || 0;
      state.graphViewport.y = Number(nextViewport.y ?? state.graphViewport.y) || 0;
      applyGraphViewport();
      if (options.announce) {
        setStatus(options.announce);
      }
    }

    function fitGraphBounds(bounds, options = {}) {
      const target = normalizeGraphBounds(bounds);
      if (!target || !measureGraphViewport()) {
        return false;
      }
      const viewportWidth = state.graphViewport.viewportWidth;
      const viewportHeight = state.graphViewport.viewportHeight;
      const padded = expandGraphBounds(target, graphViewportBounds.fitPadding);
      const scale = Math.min(
        graphViewportBounds.maxScale,
        Math.max(
          graphViewportBounds.minScale,
          Math.min(viewportWidth / padded.width, viewportHeight / padded.height)
        )
      );
      const nextViewport = {
        scale,
        x: ((viewportWidth - (padded.width * scale)) / 2) - (padded.x * scale),
        y: ((viewportHeight - (padded.height * scale)) / 2) - (padded.y * scale),
      };
      if (options.rememberDefault) {
        state.graphViewport.defaultMode = options.defaultMode || 'all';
      }
      setGraphViewport(nextViewport, { announce: options.announce || '' });
      return true;
    }

)JS";

inline constexpr std::string_view kBackboardAppJsPart5aaa = R"JS(

    function fitAllGraphView(options = {}) {
      return fitGraphBounds(state.graphViewport.fullBounds, {
        ...options,
        defaultMode: options.defaultMode || 'all',
      });
    }

    function fitFocusedGraphView(options = {}) {
      const focusBounds = normalizeGraphBounds(state.graphViewport.focusBounds);
      if (focusBounds && state.graphViewport.focusNodeCount > 0) {
        return fitGraphBounds(focusBounds, {
          ...options,
          defaultMode: options.defaultMode || 'focus',
        });
      }
      if (options.fallback === false) {
        return false;
      }
      return fitAllGraphView({
        ...options,
        defaultMode: options.defaultMode || 'all',
      });
    }

    function resetGraphView(options = {}) {
      const announce = options.announce === false ? '' : 'Graph viewport reset';
      if (state.graphViewport.defaultMode === 'focus') {
        return fitFocusedGraphView({ announce, rememberDefault: false });
      }
      return fitAllGraphView({ announce, rememberDefault: false });
    }

    function graphViewportLocalPoint(clientX, clientY) {
      const { canvas } = graphCanvasElements();
      if (!canvas || !measureGraphViewport()) {
        return null;
      }
      const rect = canvas.getBoundingClientRect();
      return {
        x: clientX - rect.left,
        y: clientY - rect.top,
      };
    }

    function zoomGraphAtPoint(multiplier, clientX, clientY, options = {}) {
      const point = graphViewportLocalPoint(clientX, clientY);
      if (!point) {
        return false;
      }
      const nextScale = Math.min(
        graphViewportBounds.maxScale,
        Math.max(graphViewportBounds.minScale, state.graphViewport.scale * multiplier)
      );
      if (Math.abs(nextScale - state.graphViewport.scale) < 0.0001) {
        return false;
      }
      const worldX = (point.x - state.graphViewport.x) / state.graphViewport.scale;
      const worldY = (point.y - state.graphViewport.y) / state.graphViewport.scale;
      setGraphViewport({
        scale: nextScale,
        x: point.x - (worldX * nextScale),
        y: point.y - (worldY * nextScale),
      }, { announce: options.announce || '' });
      return true;
    }

    function zoomGraphFromCenter(multiplier, options = {}) {
      const { canvas } = graphCanvasElements();
      if (!canvas || !measureGraphViewport()) {
        return false;
      }
      const rect = canvas.getBoundingClientRect();
      return zoomGraphAtPoint(
        multiplier,
        rect.left + (rect.width / 2),
        rect.top + (rect.height / 2),
        options
      );
    }

    function panGraphViewportBy(deltaX, deltaY, options = {}) {
      setGraphViewport({
        x: state.graphViewport.x + deltaX,
        y: state.graphViewport.y + deltaY,
      }, { announce: options.announce || '' });
    }

    function disconnectGraphViewportResizeObserver() {
      if (state.graphViewport.resizeObserver) {
        state.graphViewport.resizeObserver.disconnect();
        state.graphViewport.resizeObserver = null;
      }
    }

)JS";

inline constexpr std::string_view kBackboardAppJsPart5aaab = R"JS(

    function bindGraphViewportControls() {
      const { canvas } = graphCanvasElements();
      const zoomOut = document.getElementById('graph-zoom-out');
      const zoomIn = document.getElementById('graph-zoom-in');
      const fitAll = document.getElementById('graph-fit-all');
      const fitFocus = document.getElementById('graph-fit-focus');
      const resetView = document.getElementById('graph-reset-view');

      if (zoomOut && !zoomOut.__kobGraphViewportBound) {
        zoomOut.__kobGraphViewportBound = true;
        zoomOut.addEventListener('click', () => {
          zoomGraphFromCenter(1 / graphViewportBounds.zoomStep, { announce: 'Graph zoomed out' });
        });
      }
      if (zoomIn && !zoomIn.__kobGraphViewportBound) {
        zoomIn.__kobGraphViewportBound = true;
        zoomIn.addEventListener('click', () => {
          zoomGraphFromCenter(graphViewportBounds.zoomStep, { announce: 'Graph zoomed in' });
        });
      }
      if (fitAll && !fitAll.__kobGraphViewportBound) {
        fitAll.__kobGraphViewportBound = true;
        fitAll.addEventListener('click', () => {
          fitAllGraphView({ announce: 'Graph fit to all nodes' });
        });
      }
      if (fitFocus && !fitFocus.__kobGraphViewportBound) {
        fitFocus.__kobGraphViewportBound = true;
        fitFocus.addEventListener('click', () => {
          fitFocusedGraphView({ announce: 'Graph fit to focused subgraph' });
        });
      }
      if (resetView && !resetView.__kobGraphViewportBound) {
        resetView.__kobGraphViewportBound = true;
        resetView.addEventListener('click', () => {
          resetGraphView();
        });
      }

      if (canvas && !canvas.__kobGraphViewportBound) {
        canvas.__kobGraphViewportBound = true;
        canvas.addEventListener('pointerdown', (event) => {
          if (event.button !== 0) {
            return;
          }
          const nodeTarget = event.target?.closest?.('[data-graph-node-id]') || null;
          state.graphViewport.dragging = {
            pointerId: event.pointerId,
            startClientX: event.clientX,
            startClientY: event.clientY,
            startX: state.graphViewport.x,
            startY: state.graphViewport.y,
            moved: false,
            nodeTarget,
          };
          if (!nodeTarget) {
            canvas.focus({ preventScroll: true });
          }
          canvas.setPointerCapture?.(event.pointerId);
        });
        canvas.addEventListener('pointermove', (event) => {
          const drag = state.graphViewport.dragging;
          if (!drag || drag.pointerId !== event.pointerId) {
            return;
          }
          const deltaX = event.clientX - drag.startClientX;
          const deltaY = event.clientY - drag.startClientY;
          if (!drag.moved && Math.hypot(deltaX, deltaY) > 4) {
            drag.moved = true;
            state.graphViewport.suppressClick = true;
            canvas.classList.add('is-panning');
          }
          if (!drag.moved) {
            return;
          }
          setGraphViewport({
            x: drag.startX + deltaX,
            y: drag.startY + deltaY,
          });
          event.preventDefault();
        });
        const finishDrag = (event) => {
          const drag = state.graphViewport.dragging;
          if (!drag || (event?.pointerId && drag.pointerId !== event.pointerId)) {
            return;
          }
          if (drag.moved) {
            canvas.focus({ preventScroll: true });
          }
          state.graphViewport.dragging = null;
          canvas.classList.remove('is-panning');
          if (event?.pointerId) {
            canvas.releasePointerCapture?.(event.pointerId);
          }
        };
        canvas.addEventListener('pointerup', finishDrag);
        canvas.addEventListener('pointercancel', finishDrag);
        canvas.addEventListener('lostpointercapture', finishDrag);
        canvas.addEventListener('click', (event) => {
          if (!state.graphViewport.suppressClick) {
            return;
          }
          state.graphViewport.suppressClick = false;
          event.preventDefault();
          event.stopPropagation();
        }, true);
        canvas.addEventListener('wheel', (event) => {
          if (!state.graphPayload) {
            return;
          }
          event.preventDefault();
          const multiplier = event.deltaY < 0
            ? graphViewportBounds.zoomStep
            : (1 / graphViewportBounds.zoomStep);
          zoomGraphAtPoint(multiplier, event.clientX, event.clientY);
        }, { passive: false });
        canvas.addEventListener('keydown', (event) => {
          if (!state.graphPayload) {
            return;
          }
          if (event.key === '+' || event.key === '=') {
            if (zoomGraphFromCenter(graphViewportBounds.zoomStep, { announce: 'Graph zoomed in' })) {
              event.preventDefault();
            }
            return;
          }
          if (event.key === '-') {
            if (zoomGraphFromCenter(1 / graphViewportBounds.zoomStep, { announce: 'Graph zoomed out' })) {
              event.preventDefault();
            }
            return;
          }
          if (event.key === '0') {
            if (resetGraphView()) {
              event.preventDefault();
            }
            return;
          }
          if (event.key === 'ArrowLeft') {
            panGraphViewportBy(graphViewportBounds.keyboardPanStep, 0, { announce: 'Graph panned left' });
            event.preventDefault();
            return;
          }
          if (event.key === 'ArrowRight') {
            panGraphViewportBy(-graphViewportBounds.keyboardPanStep, 0, { announce: 'Graph panned right' });
            event.preventDefault();
            return;
          }
          if (event.key === 'ArrowUp') {
            panGraphViewportBy(0, graphViewportBounds.keyboardPanStep, { announce: 'Graph panned up' });
            event.preventDefault();
            return;
          }
          if (event.key === 'ArrowDown') {
            panGraphViewportBy(0, -graphViewportBounds.keyboardPanStep, { announce: 'Graph panned down' });
            event.preventDefault();
          }
        });
      }

      disconnectGraphViewportResizeObserver();
      if (canvas && typeof ResizeObserver !== 'undefined') {
        state.graphViewport.resizeObserver = new ResizeObserver(() => {
          applyGraphViewport();
        });
        state.graphViewport.resizeObserver.observe(canvas);
      }
      updateGraphViewportButtons();
    }

    function initializeGraphViewport(layout = {}) {
      state.graphViewport.fullBounds = normalizeGraphBounds(layout.fullBounds);
      state.graphViewport.focusBounds = normalizeGraphBounds(layout.focusBounds);
      state.graphViewport.focusNodeCount = Number(layout.focusNodeCount) || 0;
      state.graphViewport.dragging = null;
      state.graphViewport.suppressClick = false;
      bindGraphViewportControls();
      const defaultMode = state.graphViewport.focusNodeCount > 0 && state.graphViewport.focusBounds
        ? 'focus'
        : 'all';
      state.graphViewport.defaultMode = defaultMode;
      if (defaultMode === 'focus') {
        fitFocusedGraphView({ rememberDefault: true });
        return;
      }
      fitAllGraphView({ rememberDefault: true });
    }

    function renderGraphSvg(nodes, edges) {
      const isolation = buildGraphIsolation(nodes, edges);
      if (!nodes.length) {
        return {
          markup: '<div class="muted">No graph nodes for the current filters.</div>',
          isolation,
          layout: {
            fullBounds: null,
            focusBounds: null,
            focusNodeCount: 0,
          },
        };
      }

      const incomingDependency = new Set(edges
        .filter((edge) => edge.dependency)
        .map((edge) => edge.to));
      const sortedNodes = [...nodes].filter((node) =>
        (isolation.nodeVisibility.get(String(node.id || '')) || 'visible') !== 'hidden'
      ).sort((a, b) => {
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
      let fullBounds = null;
      let focusBounds = null;
      let focusNodeCount = 0;
      positioned.forEach((pos, nodeId) => {
        const bounds = { x: pos.x, y: pos.y, width: pos.w, height: pos.h };
        fullBounds = mergeGraphBounds(fullBounds, bounds);
        const reason = isolation.nodeReason.get(String(nodeId || '')) || 'within-depth';
        const visibility = isolation.nodeVisibility.get(String(nodeId || '')) || 'visible';
        if ((reason === 'focus-root' || reason === 'within-depth') && visibility !== 'hidden') {
          focusBounds = mergeGraphBounds(focusBounds, bounds);
          focusNodeCount += 1;
        }
      });

      const edgeMarkup = edges.slice(0, 160).map((edge, index) => {
        const edgeKey = `${String(edge.from || '')}->${String(edge.to || '')}#${index}`;
        const visibility = isolation.edgeVisibility.get(edgeKey) || 'visible';
        if (visibility === 'hidden') return '';
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
        const fadedClass = visibility === 'faded' ? ' is-faded' : '';
        const reason = isolation.edgeReason.get(edgeKey) || '';
        return `<path class="graph-edge ${escAttr(graphEdgeClass(edge.kind))}${fadedClass}" data-edge-visibility="${escAttr(visibility)}" data-edge-reason="${escAttr(reason)}" d="M ${x1} ${y1} C ${c1x} ${y1}, ${c2x} ${y2}, ${x2} ${y2}" marker-end="url(#graph-arrow)" />` +
          `<text class="graph-edge-label${fadedClass}" x="${midX}" y="${labelY}" text-anchor="middle">${esc(edge.kind || '')}</text>`;
      }).join('');

      const nodeMarkup = sortedNodes.map((node) => {
        const pos = positioned.get(node.id);
        const meta = [node.product, node.kind, node.state].filter(Boolean).join(' / ');
        const nodeId = String(node.id || '');
        const visibility = isolation.nodeVisibility.get(nodeId) || 'visible';
        const reason = isolation.nodeReason.get(nodeId) || 'within-depth';
        const isFocusRoot = nodeId === isolation.focusNodeId;
        const rerootId = String(node.item_id || node.id || '');
        const rerootProduct = String(node.product || state.graphItemProduct || '');
        const rerootable = Boolean(rerootId);
        const classes = [graphNodeClass(node, incomingDependency.has(node.id))];
        if (visibility === 'faded') classes.push('is-faded');
        if (reason === 'focus-root' || reason === 'within-depth') classes.push('is-included-neighborhood');
        if (isFocusRoot) classes.push('is-focus-root');
        if (rerootable) classes.push('is-rerootable');
        return `<g class="${escAttr(classes.join(' '))}" transform="translate(${pos.x}, ${pos.y})" data-graph-node-id="${escAttr(rerootId)}" data-graph-node-product="${escAttr(rerootProduct)}" data-node-visibility="${escAttr(visibility)}" data-node-reason="${escAttr(reason)}" data-focus-root="${isFocusRoot ? 'true' : 'false'}"${rerootable ? ' tabindex="0" role="button"' : ''}>` +
          `<rect width="${pos.w}" height="${pos.h}"></rect>` +
          `<text class="graph-label" x="10" y="20">${esc(shortGraphLabel(node.item_id || node.id, 24))}</text>` +
          `<text class="graph-meta" x="10" y="38">${esc(shortGraphLabel(node.label || '', 30))}</text>` +
          `<title>${esc(`${node.id || ''} ${meta} / ${graphIsolationReasonLabel(reason)}${rerootable ? ' / click to re-root' : ''}`)}</title>` +
        `</g>`;
      }).join('');

      return {
        markup: `<div id="graph-canvas" class="graph-canvas" tabindex="0" role="region" aria-label="Dependency graph canvas" aria-describedby="graph-scope-help graph-mode-help">` +
        `<svg id="graph-svg" class="graph-svg" role="img" aria-label="Dependency graph visualization">` +
        `<defs><marker id="graph-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M 0 0 L 10 5 L 0 10 z" fill="#6c778d"></path></marker></defs>` +
        `<g id="graph-viewport-layer" class="graph-viewport-layer" transform="translate(0 0) scale(1)">` +
        `<g id="graph-content-layer" class="graph-content-layer" data-graph-width="${width}" data-graph-height="${height}">` +
        edgeMarkup + nodeMarkup +
        `</g></g></svg></div>`,
        isolation,
        layout: {
          fullBounds,
          focusBounds,
          focusNodeCount,
        },
      };
    }

    function renderGraphIsolationCountPills(counts) {
      const entries = [...counts.entries()];
      if (!entries.length) {
        return '<span class="muted">none</span>';
      }
      return entries.map(([reason, count]) =>
        `<span class="pill">${count} ${esc(graphIsolationReasonLabel(reason))}</span>`
      ).join('');
    }

    function bindGraphNodeRerooting() {
      const container = document.getElementById('graph-list');
      if (!container) {
        return;
      }
      container.querySelectorAll('[data-graph-node-id]').forEach((node) => {
        if (node.__kobGraphNodeBound) {
          return;
        }
        node.__kobGraphNodeBound = true;
        const activate = async () => {
          const nextId = node.getAttribute('data-graph-node-id') || '';
          const nextProduct = node.getAttribute('data-graph-node-product') || '';
          if (!nextId) {
            return;
          }
          setGraphRoot(nextId, nextProduct, { reason: 'graph node re-rooted' });
        };
        node.addEventListener('click', async (event) => {
          event.preventDefault();
          await activate();
        });
        node.addEventListener('keydown', async (event) => {
          if (event.key !== 'Enter' && event.key !== ' ') {
            return;
          }
          event.preventDefault();
          await activate();
        });
      });
    }

)JS";

inline constexpr std::string_view kBackboardAppJsPart5ab = R"JS(

    function cycleAuditRows(value) {
      return Array.isArray(value) ? value : [];
    }

    function cycleAuditText(value, fallback = 'not reported') {
      return value === undefined || value === null || value === ''
        ? fallback
        : String(value);
    }

    function cycleAuditMemberKey(member) {
      return String(member?.canonical_node_key || `${member?.product || ''}:${member?.item_id || ''}`);
    }

    function renderCycleAuditMember(member) {
      const itemId = cycleAuditText(member?.item_id, 'unknown item');
      const product = cycleAuditText(member?.product, 'unknown product');
      const title = cycleAuditText(member?.title, itemId);
      const state = cycleAuditText(member?.state, 'unknown state');
      const kind = cycleAuditText(member?.kind, 'unknown kind');
      const memberJumpTarget = member && member.jump_target;
      const jumpTarget = memberJumpTarget && typeof memberJumpTarget === 'object'
        ? memberJumpTarget
        : {};
      const rerootProduct = String(jumpTarget.reroot_product || product).trim();
      const rerootItemId = String(jumpTarget.reroot_item_id || '').trim();
      const targetMode = String(jumpTarget.target_mode || 'cycles').trim();
      const jumpButton = rerootItemId
        ? `<button type="button" class="btn cycle-audit-jump blocker-chain-jump" data-blocker-chain-jump-id="${escAttr(rerootItemId)}" data-blocker-chain-jump-product="${escAttr(rerootProduct)}" data-cycle-audit-target-mode="${escAttr(targetMode)}" aria-label="Re-root bounded ${escAttr(targetMode)} graph at ${escAttr(rerootItemId)} in ${escAttr(rerootProduct)}">Jump to ${esc(rerootItemId)}</button>`
        : '';
      return `<li class="cycle-audit-member">` +
        `<div class="detail-title-row"><strong>${esc(title)}</strong>${pill(state)}</div>` +
        `<div class="muted"><code class="cycle-audit-member-id">${esc(cycleAuditMemberKey(member))}</code> / ${esc(product)} / ${esc(kind)}</div>` +
        jumpButton +
      `</li>`;
    }

    function renderCycleAuditEdge(edge) {
      const from = cycleAuditText(edge?.from, 'unknown source');
      const to = cycleAuditText(edge?.to, 'unknown target');
      const direction = cycleAuditText(edge?.direction, 'blocker_to_blocked');
      return `<li class="cycle-audit-edge"><code>${esc(from)}</code> -> <code>${esc(to)}</code><span class="muted">${esc(direction)}</span></li>`;
    }

    function renderCycleAudit(cycleAudit) {
      if (!cycleAudit || typeof cycleAudit !== 'object' || Array.isArray(cycleAudit)) {
        return '';
      }
      const summary = cycleAudit.summary && typeof cycleAudit.summary === 'object'
        ? cycleAudit.summary
        : {};
      const groups = cycleAuditRows(cycleAudit.groups).sort((left, right) =>
        String(left?.canonical_key || '').localeCompare(String(right?.canonical_key || ''))
      );
      const groupCount = Number.isFinite(Number(summary.group_count))
        ? Number(summary.group_count)
        : groups.length;
      const summaryFacts = [
        ['Visible dependency nodes', summary.visible_dependency_node_count],
        ['Visible dependency edges', summary.visible_dependency_edge_count],
        ['Depth cap', summary.max_depth],
        ['Children cap', summary.max_children_per_node],
        ['Node cap', summary.max_total_nodes],
        ['Edge cap', summary.max_total_edges],
      ];
      const truncationWarning = summary.graph_truncated
        ? '<div class="cycle-audit-warning" role="status">This bounded audit may be incomplete because the visible graph was truncated.</div>'
        : '';
      const groupCards = groups.map((group, index) => {
        const members = cycleAuditRows(group?.members).sort((left, right) =>
          cycleAuditMemberKey(left).localeCompare(cycleAuditMemberKey(right))
        );
        const edges = cycleAuditRows(group?.offending_edges).sort((left, right) => {
          const leftKey = `${left?.from || ''}->${left?.to || ''}`;
          const rightKey = `${right?.from || ''}->${right?.to || ''}`;
          return leftKey.localeCompare(rightKey);
        });
        const products = cycleAuditRows(group?.involved_products).map((product) => String(product || ''))
          .filter(Boolean).sort((left, right) => left.localeCompare(right));
        const groupId = `cycle-audit-group-${index + 1}`;
        return `<article class="card cycle-audit-group" aria-labelledby="${escAttr(groupId)}">` +
          `<div class="detail-title-row"><h5 id="${escAttr(groupId)}">Group ${index + 1}</h5><span class="pill">${esc(cycleAuditText(group?.member_count, members.length))} member(s)</span></div>` +
          `<div class="muted">Products: ${esc(products.join(', ') || 'not reported')} / ${group?.crosses_product_boundary ? 'cross-product' : 'single-product'}</div>` +
          `<div class="cycle-audit-grid">` +
            `<section><h6>Members</h6><ol class="cycle-audit-members">${members.map(renderCycleAuditMember).join('')}</ol></section>` +
            `<section><h6>Offending edges</h6><ul class="cycle-audit-edges">${edges.length ? edges.map(renderCycleAuditEdge).join('') : '<li class="muted">No normalized offending edges reported.</li>'}</ul></section>` +
          `</div>` +
        `</article>`;
      }).join('');
      return `<section class="cycle-audit" aria-labelledby="cycle-audit-title">` +
        `<div class="cycle-audit-header"><h4 id="cycle-audit-title">Cycle groups (${esc(String(groupCount))})</h4>` +
          `<div class="muted">${esc(cycleAuditText(cycleAudit.edge_direction_note, 'Dependency edges are normalized from blocker to blocked.'))}</div>` +
          `<div class="muted">Each group is a strongly connected dependency group in the visible bounded dependency graph and may contain multiple simple loops.</div>` +
        `</div>` +
        truncationWarning +
        `<div class="cycle-audit-summary">${summaryFacts.map(([label, value]) =>
          `<div class="detail-fact"><span class="detail-label">${esc(label)}</span><div class="detail-value">${esc(cycleAuditText(value))}</div></div>`
        ).join('')}</div>` +
        (groups.length ? `<div class="cycle-audit-groups">${groupCards}</div>` : '<div class="muted">No dependency cycles found.</div>') +
      `</section>`;
    }

)JS";

inline constexpr std::string_view kBackboardAppJsPart5ac = R"JS(

    function blockerChainRows(value) {
      return Array.isArray(value) ? value : [];
    }

    function blockerChainValue(value, fallback = 'not reported') {
      return value === undefined || value === null || value === ''
        ? fallback
        : String(value);
    }

    function blockerChainPath(entry) {
      const path = Array.isArray(entry?.path_item_ids)
        ? entry.path_item_ids
        : (Array.isArray(entry?.path) ? entry.path : []);
      if (path.length) {
        return path.map((item) => String(item || '')).filter(Boolean).join(' -> ');
      }
      return blockerChainValue(entry?.path, 'No path context reported.');
    }

    function blockerChainJumpTarget(entry, jumpTargets) {
      const entryId = String(entry?.item_id || entry?.id || '').trim();
      const entryProduct = String(entry?.product || '').trim();
      const target = blockerChainRows(jumpTargets).find((candidate) => {
        const candidateId = String(candidate?.reroot_item_id || candidate?.item_id || candidate?.id || '').trim();
        const candidateProduct = String(candidate?.reroot_product || candidate?.product || '').trim();
        return candidateId && candidateId === entryId && (!entryProduct || candidateProduct === entryProduct);
      }) || entry || {};
      return {
        itemId: String(target.reroot_item_id || target.item_id || target.id || entryId).trim(),
        product: String(target.reroot_product || target.product || entry?.product || '').trim(),
      };
    }

    function renderBlockerChainEntry(entry, kind, jumpTargets) {
      const itemId = blockerChainValue(entry?.item_id || entry?.id, 'unknown item');
      const title = blockerChainValue(entry?.title, itemId);
      const state = blockerChainValue(entry?.state, 'unknown state');
      const direct = entry && Object.prototype.hasOwnProperty.call(entry, 'direct')
        ? (entry.direct ? 'direct' : 'indirect')
        : 'not reported';
      const jump = blockerChainJumpTarget(entry, jumpTargets);
      const rootFacts = kind === 'root'
        ? `<div class="detail-fact"><span class="detail-label">Rank</span><div class="detail-value">${esc(blockerChainValue(entry?.rank))}</div></div>` +
          `<div class="detail-fact"><span class="detail-label">Visible bounded impact</span><div class="detail-value">${esc(blockerChainValue(entry?.visible_bounded_downstream_impact ?? entry?.visible_bounded_impact ?? entry?.visible_impact))}</div></div>`
        : '';
      const jumpButton = jump.itemId
        ? `<button type="button" class="btn blocker-chain-jump" data-blocker-chain-jump-id="${escAttr(jump.itemId)}" data-blocker-chain-jump-product="${escAttr(jump.product)}" aria-label="Re-root bounded graph at ${escAttr(jump.itemId)}">Jump to ${esc(jump.itemId)}</button>`
        : '';
      return `<article class="card blocker-chain-item">` +
        `<div class="detail-title-row"><strong>${esc(title)}</strong>${pill(state)}</div>` +
        `<div class="muted"><code class="blocker-chain-id">${esc(itemId)}</code>${entry?.product ? ` / ${esc(entry.product)}` : ''}</div>` +
        `<div class="blocker-chain-facts">${rootFacts}` +
          `<div class="detail-fact"><span class="detail-label">Distance</span><div class="detail-value">${esc(blockerChainValue(entry?.distance))}</div></div>` +
          `<div class="detail-fact"><span class="detail-label">Connection</span><div class="detail-value">${esc(direct)}</div></div>` +
          `<div class="detail-fact"><span class="detail-label">Path count</span><div class="detail-value">${esc(blockerChainValue(entry?.path_count))}</div></div>` +
        `</div>` +
        `<div><span class="detail-label">Path context</span><div class="detail-value blocker-chain-path">${esc(blockerChainPath(entry))}</div></div>` +
        jumpButton +
      `</article>`;
    }

    function renderBlockerChainSection(title, rows, kind, jumpTargets, emptyState) {
      const entries = blockerChainRows(rows);
      return `<section class="blocker-chain-section" aria-labelledby="blocker-chain-${escAttr(kind)}">` +
        `<h5 id="blocker-chain-${escAttr(kind)}">${esc(title)}</h5>` +
        `<div class="blocker-chain-list">${entries.length
          ? entries.map((entry) => renderBlockerChainEntry(entry, kind, jumpTargets)).join('')
          : `<div class="muted">${esc(emptyState)}</div>`}</div>` +
      `</section>`;
    }

    function renderBlockerChainMetric(label, value) {
      return `<div class="detail-fact"><span class="detail-label">${esc(label)}</span><div class="detail-value">${esc(blockerChainValue(value))}</div></div>`;
    }

    function renderBlockerChain(blockerChain) {
      if (!blockerChain || typeof blockerChain !== 'object' || Array.isArray(blockerChain)) {
        return '';
      }
      const ranking = blockerChain.ranking_basis && typeof blockerChain.ranking_basis === 'object'
        ? blockerChain.ranking_basis
        : {};
      const summary = blockerChain.summary && typeof blockerChain.summary === 'object'
        ? blockerChain.summary
        : {};
      const ordering = Array.isArray(ranking.ordering) ? ranking.ordering.join(', ') : blockerChainValue(ranking.ordering, '');
      const rankingSummary = blockerChainValue(
        ranking.summary,
        'Bounded review order uses visible impact, shorter path, and stable ID; it is not business priority.'
      );
      const rankingDetail = ordering ? ` Ordering: ${ordering}.` : '';
      return `<section class="blocker-chain" aria-labelledby="blocker-chain-title">` +
        `<div class="blocker-chain-header"><h4 id="blocker-chain-title">Blocker chain</h4>` +
          `<div class="muted">${esc(blockerChainValue(blockerChain.edge_direction_note, 'Dependency edge direction was not reported.'))}</div>` +
          `<div class="muted">${esc(rankingSummary + rankingDetail)} This ordering is bounded review order, not business priority.</div>` +
        `</div>` +
        `<div class="blocker-chain-grid">` +
          renderBlockerChainSection('Root blockers', blockerChain.root_blockers, 'root', blockerChain.jump_targets, 'No root blockers are visible in this bounded graph.') +
          renderBlockerChainSection('Upstream blockers', blockerChain.upstream_blockers, 'upstream', blockerChain.jump_targets, 'No upstream blockers are visible in this bounded graph.') +
          renderBlockerChainSection('Downstream impact', blockerChain.downstream_blocked_items, 'downstream', blockerChain.jump_targets, 'No downstream impact is visible in this bounded graph.') +
        `</div>` +
        `<section class="blocker-chain-section" aria-labelledby="blocker-chain-branches"><h5 id="blocker-chain-branches">Branch evidence</h5>` +
          `<div class="muted">Collapsed and truncated branch evidence remains bounded and diagnosable; counts are reported exactly as returned by the dependency payload.</div>` +
          `<div class="blocker-chain-facts">` +
            renderBlockerChainMetric('Parallel branches', blockerChain.parallel_branch_count) +
            renderBlockerChainMetric('Truncated branches', blockerChain.truncated_branch_count) +
            renderBlockerChainMetric('Visible dependency edges', summary.visible_dependency_edge_count) +
            renderBlockerChainMetric('Hidden nodes', summary.hidden_node_count) +
            renderBlockerChainMetric('Hidden edges', summary.hidden_edge_count) +
            renderBlockerChainMetric('Invalid refs', summary.invalid_ref_count) +
            renderBlockerChainMetric('Bounded query', summary.bounded_query) +
            renderBlockerChainMetric('Depth cap', summary.max_depth) +
            renderBlockerChainMetric('Children cap', summary.max_children_per_node) +
            renderBlockerChainMetric('Node cap', summary.max_total_nodes) +
            renderBlockerChainMetric('Edge cap', summary.max_total_edges) +
          `</div>` +
        `</section>` +
      `</section>`;
    }

    function bindBlockerChainJumpActions() {
      const container = document.getElementById('graph-list');
      if (!container) {
        return;
      }
      container.querySelectorAll('[data-blocker-chain-jump-id]').forEach((button) => {
        if (button.__kobBlockerChainJumpBound) {
          return;
        }
        button.__kobBlockerChainJumpBound = true;
        button.addEventListener('click', () => {
          const itemId = button.getAttribute('data-blocker-chain-jump-id') || '';
          const product = button.getAttribute('data-blocker-chain-jump-product') || '';
          if (itemId) {
            if (button.classList.contains('cycle-audit-jump')) {
              setGraphRoot(itemId, product, { reason: 'cycle-audit jump' });
              return;
            }
            setGraphRoot(itemId, product, { reason: 'blocker-chain jump' });
          }
        });
      });
    }

    function renderGraphView(data = {}) {
      state.graphPayload = data;
      const currentPreset = syncGraphModeControls(data);
      syncGraphIsolationControls();
      const nodes = data.nodes || [];
      const edges = data.edges || [];
      const missing = data.missing_nodes || [];
      const hiddenNodes = Number(data.hidden_node_count || 0);
      const hiddenEdges = Number(data.hidden_edge_count || 0);
      const graphRender = renderGraphSvg(nodes, edges);
      const isolation = graphRender.isolation;
      const displayedNodeCount = nodes.length - isolation.hiddenNodeCount;
      const displayedEdgeCount = edges.length - isolation.hiddenEdgeCount;
      const caps = [
        `depth ${data.max_depth ?? 'n/a'}`,
        `children ${data.max_children_per_node ?? 'n/a'}`,
        `nodes ${data.max_total_nodes ?? data.node_limit ?? 'n/a'}`,
        `edges ${data.max_total_edges ?? data.edge_limit ?? 'n/a'}`,
      ].join(' / ');
      const truncated = data.truncated ? `, truncated (${hiddenNodes} hidden node(s), ${hiddenEdges} hidden edge(s))` : '';
      const modeLabel = currentPreset?.label || 'Dependencies';
      const isolationCounts = isolation.mode === 'hide'
        ? `${isolation.hiddenNodeCount} hidden node(s), ${isolation.hiddenEdgeCount} hidden edge(s)`
        : `${isolation.fadedNodeCount} faded node(s), ${isolation.fadedEdgeCount} faded edge(s)`;
      const focusLabel = isolation.focusNodeId || state.graphItemId || 'n/a';
      const cycleAudit = data.mode === 'cycles' ? renderCycleAudit(data.cycle_audit) : '';
      updateFocusGraphPageChrome();
      document.getElementById('graph-summary').textContent =
        `${modeLabel}: ${nodes.length} node(s), ${edges.length} edge(s), ${missing.length} missing node(s)${truncated} | focus ${focusLabel} | neighborhood depth ${isolation.maxDepth} | isolation ${graphIsolationModeLabel(isolation.mode)} | ${isolationCounts} | caps ${caps}`;

      const diagnostics = [
        `<div class="card"><strong>Isolation scope</strong><div class="muted">Focus root ${esc(focusLabel)} / ${esc(graphIsolationModeLabel(isolation.mode))} / depth ${esc(String(isolation.maxDepth))}</div><div class="graph-diagnostic-pills"><span class="pill">canvas nodes ${displayedNodeCount}</span><span class="pill">canvas edges ${displayedEdgeCount}</span><span class="pill">included nodes ${isolation.visibleNodeCount}</span></div></div>`,
        `<div class="card"><strong>${esc(isolation.mode === 'hide' ? 'Hidden' : 'Faded')} unrelated graph elements</strong><div class="muted">Unrelated nodes are never removed silently; counts and reasons stay visible here even when the canvas hides them.</div><div class="graph-diagnostic-pills">${renderGraphIsolationCountPills(isolation.nodeReasonCounts)}</div><div class="graph-diagnostic-pills">${renderGraphIsolationCountPills(isolation.edgeReasonCounts)}</div></div>`,
        ...(isolation.rootMissing
          ? [`<div class="card"><strong>Focused root missing</strong><div class="muted">${esc(state.graphItemId || 'requested root')} was not returned in the bounded graph response, so all returned nodes stay visible with a root-missing diagnostic.</div></div>`]
          : []),
        ...((data.diagnostics || []).slice(0, 20).map((diagnostic) =>
          `<div class="card"><strong>${esc(diagnostic.code || 'Graph diagnostic')}</strong><div class="muted">${esc(diagnostic.target || 'graph')}</div><div>${esc(diagnostic.message || '')}</div></div>`
        )),
        ...missing.slice(0, 20).map((node) =>
          `<div class="card"><strong>Missing</strong> <code>${esc(node.id || node.ref || '')}</code><div class="muted">${esc(node.kind || '')} from ${esc(node.source || '')}</div></div>`
        ),
        ...((data.invalid_refs || []).slice(0, 20).map((ref) =>
          `<div class="card"><strong>Invalid ref</strong> <code>${esc(ref.ref || '')}</code><div class="muted">${esc(ref.kind || '')} from ${esc(ref.source || '')}</div></div>`
        )),
        ...(data.mode === 'cycles' ? [] : (data.dependency_cycles || []).slice(0, 10).map((cycle) =>
          `<div class="card"><strong>Dependency cycle</strong><div class="muted">${esc((cycle || []).join(' -> '))}</div></div>`
        )),
      ].join('');

      document.getElementById('graph-list').innerHTML = [
        data.mode === 'cycles' ? '' : renderBlockerChain(data.blocker_chain),
        cycleAudit,
        graphRender.markup,
        diagnostics ? `<div class="graph-diagnostics">${diagnostics}</div>` : '',
        `<h4>Edge details</h4>`,
        ...(edges.slice(0, 120).map((edge, index) => {
          const edgeKey = `${String(edge.from || '')}->${String(edge.to || '')}#${index}`;
          const visibility = isolation.edgeVisibility.get(edgeKey) || 'visible';
          const reason = isolation.edgeReason.get(edgeKey) || '';
          return `<div class="card"><code>${esc(edge.from || '')}</code> -> <code>${esc(edge.to || '')}</code><div class="muted">${esc(edge.kind || '')} / ${esc(edge.semantic || '')} / ${esc(visibility)}${reason ? ` / ${esc(graphIsolationReasonLabel(reason))}` : ''}</div></div>`;
        })),
        `<h4>Node details</h4>`,
        ...(nodes.slice(0, 120).map((node) => {
          const nodeId = String(node.id || '');
          const visibility = isolation.nodeVisibility.get(nodeId) || 'visible';
          const reason = isolation.nodeReason.get(nodeId) || 'within-depth';
          const rerootId = String(node.item_id || node.id || '');
          return `<div class="card"><code>${esc(node.id || '')}</code><div>${esc(node.label || '')}</div><div class="muted">${esc(node.kind || '')} ${esc(node.state || '')} / ${esc(visibility)} / ${esc(graphIsolationReasonLabel(reason))}${rerootId ? ` / re-root ${esc(rerootId)}` : ''}</div></div>`;
        })),
      ].join('');
      bindBlockerChainJumpActions();
      bindGraphNodeRerooting();
      initializeGraphViewport(graphRender.layout || {});
    }

    async function loadGraph(signal, refresh) {
      if (!state.graphItemId) {
        renderFocusGraphScaffold();
        return;
      }

      const result = await getJson(`/api/review/graph?${graphQueryString()}`, {
        signal,
        refresh,
        stage: 'graph.dependencies',
      });
      renderGraphView(result?.data || {});
    }

)JS";

inline constexpr std::string_view kBackboardAppJsPart5b = R"JS(

    function renderDecisionRadarDiagnostic(diagnostic) {
      return `<div class="card gap-card"><strong>${esc(diagnostic.code || 'gap')}</strong><div>${esc(diagnostic.message || '')}</div><div class="muted">${esc(diagnostic.target || '')}</div></div>`;
    }

    function renderDecisionRadarRow(row) {
      const categories = (row.categories || []).map(pill).join('');
      const affected = (row.affected_refs || []).map(renderTreeRefChip).join('');
      const evidence = (row.evidence_refs || []).map(renderTreeRefChip).join('');
      const superseded = (row.superseded_by || []).map(renderTreeRefChip).join('');
      const diagnostics = (row.diagnostics || []).map(renderDecisionRadarDiagnostic).join('');
      return `<div class="card decision-radar-row" data-decision-radar-row="${escAttr(row.adr_id || '')}">` +
        `<div class="detail-title-row"><strong>${esc(row.title || row.adr_id || 'ADR')}</strong>${categories}</div>` +
        `<div class="muted"><code>${esc(row.adr_id || '')}</code> / ${esc(row.product || '')}</div>` +
        `<div class="detail-facts">` +
          `<div class="detail-fact"><span class="detail-label">Decision status</span><div class="detail-value">${esc(row.decision_status || 'metadata_gap')}</div></div>` +
          `<div class="detail-fact"><span class="detail-label">Radar status</span><div class="detail-value">${esc(row.radar_status || 'unknown')}</div></div>` +
          `<div class="detail-fact"><span class="detail-label">Advisory</span><div class="detail-value">${row.advisory_only === false ? 'false' : 'true'}</div></div>` +
        `</div>` +
        `<div><div class="detail-label">Affected feature or Product Map node</div><div class="detail-links">${affected || '<div class="muted">No impacted feature refs recorded</div>'}</div></div>` +
        `<div><div class="detail-label">Revisit condition</div><div class="detail-value">${esc(row.revisit_condition || 'none recorded')}</div></div>` +
        `<div><div class="detail-label">Current signal / evidence</div><div class="detail-value">${esc(row.current_signal || '')}</div><div class="detail-links">${evidence || '<div class="muted">No evidence refs recorded</div>'}</div></div>` +
        `<div><div class="detail-label">Recommended human review action</div><div class="detail-value">${esc(row.recommended_human_review_action || '')}</div></div>` +
        (superseded ? `<div><div class="detail-label">Superseded by</div><div class="detail-links">${superseded}</div></div>` : '') +
        (diagnostics ? `<div><div class="detail-label">Gaps</div>${diagnostics}</div>` : '') +
      `</div>`;
    }

    async function loadDecisionRadar(signal, refresh) {
      const result = await getJson(`/api/review/decision-radar?${queryString()}`, {
        signal,
        refresh,
        stage: 'decision_radar.adrs',
      });
      const data = result?.data || {};
      const rows = data.rows || [];
      const diagnostics = data.diagnostics || [];
      const counts = data.category_counts || {};
      const countText = Object.keys(counts).map((key) => `${key}: ${counts[key] || 0}`).join(' | ');
      document.getElementById('decision-radar-summary').textContent =
        `${rows.length} ADR row(s), ${diagnostics.length} gap(s)${countText ? ` | ${countText}` : ''}`;
      document.getElementById('decision-radar-list').innerHTML = [
        ...rows.map(renderDecisionRadarRow),
        diagnostics.length
          ? `<section class="decision-radar-diagnostics"><h4>Decision radar gaps</h4>${diagnostics.slice(0, 40).map(renderDecisionRadarDiagnostic).join('')}</section>`
          : '',
        rows.length ? '' : `<div class="muted">${esc(data.empty_state || 'No ADR radar rows.')}</div>`,
      ].join('');
      bindItemLinks('#decision-radar-list');
    }

    async function loadRuns(signal, refresh) {
      const result = await getJson(`/api/review/runs?${queryString()}`, {
        signal,
        refresh,
        stage: 'runs.records',
      });
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
        `<div class="card"><strong>KOBQL</strong><div><code>${esc(data.generated_kobql || '')}</code></div><div class="muted">Read-only preview, ${items.length} visible item(s)</div></div><div class="command-items" role="listbox" aria-label="Command preview items">${items.map(renderItemCard).join('')}</div>`;
      bindItemLinks('#command-result');
      bindSelectableCards('#command-result');
    }

    function setActiveTab(tab) {
      state.activeTab = tab;
      const isTree = tab === 'tree';
      const isHandoff = tab === 'handoff';
      const isRoadmap = tab === 'roadmap';
      const isDecisionRadar = tab === 'decision-radar';
      const isKanban = tab === 'kanban';
      const isContext = tab === 'context';
      const isReview = tab === 'review';
      const isGraph = tab === 'graph';
      const isRuns = tab === 'runs';
      const isCommand = tab === 'command';
      document.getElementById('tab-tree').classList.toggle('active', isTree);
      document.getElementById('tab-handoff').classList.toggle('active', isHandoff);
      document.getElementById('tab-roadmap').classList.toggle('active', isRoadmap);
      document.getElementById('tab-decision-radar').classList.toggle('active', isDecisionRadar);
      document.getElementById('tab-kanban').classList.toggle('active', isKanban);
      document.getElementById('tab-context').classList.toggle('active', isContext);
      document.getElementById('tab-review').classList.toggle('active', isReview);
      document.getElementById('tab-graph').classList.toggle('active', isGraph);
      document.getElementById('tab-runs').classList.toggle('active', isRuns);
      document.getElementById('tab-command').classList.toggle('active', isCommand);
      document.getElementById('page-tree').classList.toggle('active', isTree);
      document.getElementById('page-handoff').classList.toggle('active', isHandoff);
      document.getElementById('page-roadmap').classList.toggle('active', isRoadmap);
      document.getElementById('page-decision-radar').classList.toggle('active', isDecisionRadar);
      document.getElementById('page-kanban').classList.toggle('active', isKanban);
      document.getElementById('page-context').classList.toggle('active', isContext);
      document.getElementById('page-review').classList.toggle('active', isReview);
      document.getElementById('page-graph').classList.toggle('active', isGraph);
      document.getElementById('page-runs').classList.toggle('active', isRuns);
      document.getElementById('page-command').classList.toggle('active', isCommand);
      updateAllPageRefreshStates();
      updateUrlState();
      updateFocusGraphPageChrome();
      syncSelectableItems();
    }
)JS";

inline constexpr std::string_view kBackboardAppJsPart6 = R"JS(

    function loaderForTab(tab) {
      return {
        review: loadReview,
        handoff: loadHandoffReadiness,
        tree: loadTree,
        roadmap: loadRoadmap,
        'decision-radar': loadDecisionRadar,
        kanban: loadKanban,
        context: loadContext,
        graph: loadGraph,
        runs: loadRuns,
      }[tab] || null;
    }

    function scheduleRefresh(delayMs = 120) {
      updateUrlState();
      if (state.refreshTimer) {
        clearTimeout(state.refreshTimer);
      }
      const runRefresh = () => {
        state.refreshTimer = null;
        ensureActiveTabLoaded({ force: true, reason: 'scheduled active tab refresh' });
      };
      state.refreshTimer = window.KobUi?.debounce
          ? window.KobUi.debounce('refresh-active-tab', runRefresh, delayMs)
          : setTimeout(runRefresh, delayMs);
    }

    async function ensureActiveTabLoaded(options = {}) {
      const tab = state.activeTab;
      if (!isRefreshableTab(tab)) {
        setBusy(false);
        return;
      }
      if (!options.force && state.loadedTabs.has(tab) && !state.staleTabs.has(tab)) {
        setPageRefreshState(tab, false);
        return;
      }
      await refreshActiveTab({
        tab,
        force: !!options.force,
        reason: options.reason || (state.loadedTabs.has(tab) ? 'reload stale active tab' : 'lazy tab load'),
      });
    }

    async function refreshAll(options = {}) {
      await refreshActiveTab({
        ...options,
        tab: options.tab || state.activeTab,
        reason: options.reason || 'active tab refresh',
      });
    }

    async function refreshActiveTab(options = {}) {
      const tab = options.tab || state.activeTab;
      const loader = loaderForTab(tab);
      if (!loader) {
        setBusy(false);
        return;
      }
      if (selectedProductValues().length === 0) {
        setStatus('No product found', 'error');
        setBusy(false);
        return;
      }

      if (state.refreshTimer) {
        clearTimeout(state.refreshTimer);
        state.refreshTimer = null;
      }
      if (state.refreshAbort) {
        if (state.activeRefresh) {
          state.activeRefresh.abort_reason =
              `superseded by ${tabLabels[tab] || tab} refresh`;
          state.activeRefresh.status = 'aborted';
        }
        state.refreshAbort.abort();
      }

      const refreshId = ++state.refreshSeq;
      const controller = new AbortController();
      state.refreshAbort = controller;
      state.refreshCancelRequested = false;
      const signal = controller.signal;
      const refresh = createRefreshContext(refreshId, tab, options.reason || 'active tab refresh', {
        cache_status: options.cache_status || cacheStatusFor(tab),
        stale_reason: options.stale_reason || (state.staleTabs.has(tab)
          ? state.tabCacheStatus[tab]?.reason || 'visible data retained while refreshing'
          : ''),
      });
      state.activeRefresh = refresh;

      setStatus(`Loading ${refresh.view_label} for ${refresh.product_scope_label}...`);
      setBusy(true);
      setPageRefreshState(tab, true);
      updateBusyFromRefresh(refresh);
      if (state.refreshTickTimer) {
        clearInterval(state.refreshTickTimer);
      }
      state.refreshTickTimer = setInterval(() => {
        if (refreshId === state.refreshSeq) {
          updateBusyFromRefresh(refresh);
        }
      }, 1000);

      try {
        await loader(signal, refresh);
        if (refreshId !== state.refreshSeq) {
          return;
        }
        refresh.completed = 1;
        refresh.status = 'loaded';
        refresh.finished_at = nowIso();
        refresh.elapsed_ms = Date.now() - refresh.started_at_ms;
        refresh.cache_status = 'fresh';
        state.loadedTabs.add(tab);
        state.staleTabs.delete(tab);
        state.tabCacheStatus[tab] = {
          status: 'fresh',
          reason: refresh.reason,
          updated_at: nowIso(),
          request_id: refresh.request_id,
        };
        setPageRefreshState(tab, false);
        state.lastRefreshDiagnostic =
            makeRefreshDiagnostic('loaded', `Loaded ${refresh.view_label}`, refresh.started_at_ms, [], refresh);
        setBusy(false);
        setStatus(`Loaded ${refresh.view_label} for ${refresh.product_scope_label} in ${formatElapsed(refresh.started_at_ms)}`);
      } catch (error) {
        if (refreshId !== state.refreshSeq) {
          return;
        }
        refresh.finished_at = nowIso();
        refresh.elapsed_ms = Date.now() - refresh.started_at_ms;
        if (signal.aborted) {
          const reason = state.refreshCancelRequested
            ? 'canceled by user'
            : refresh.abort_reason || 'active tab refresh aborted';
          refresh.status = state.refreshCancelRequested ? 'canceled' : 'aborted';
          refresh.abort_reason = reason;
          refresh.cancel_reason = state.refreshCancelRequested ? reason : '';
          state.lastRefreshDiagnostic =
              makeRefreshDiagnostic(refresh.status, reason, refresh.started_at_ms, [{ status: 'rejected', reason: error }], refresh);
          updateBusy('Refresh canceled', `${reason}; elapsed ${formatElapsed(refresh.started_at_ms)}; view=${refresh.view_label}; scope=${refresh.product_scope_label}`);
          setBusy(false, true);
          setStatus(`${refresh.view_label} refresh ${refresh.status} after ${formatElapsed(refresh.started_at_ms)}`);
          state.refreshCancelRequested = false;
          return;
        }

        const detail = describeRefreshError(error);
        refresh.status = 'failed';
        refresh.error = detail;
        if (state.loadedTabs.has(tab)) {
          state.staleTabs.add(tab);
          state.tabCacheStatus[tab] = {
            status: 'stale',
            reason: `refresh failed: ${detail}`,
            updated_at: nowIso(),
            request_id: refresh.request_id,
          };
        }
        setPageRefreshState(tab, false);
        state.lastRefreshDiagnostic =
            makeRefreshDiagnostic('failed', detail, refresh.started_at_ms, [{ status: 'rejected', reason: error }], refresh);
        updateBusy('Refresh failed', `${detail}; elapsed ${formatElapsed(refresh.started_at_ms)}; view=${refresh.view_label}; scope=${refresh.product_scope_label}; endpoint=${refresh.active_endpoint || 'unknown'}`);
        setBusy(false, true);
        setStatus(`Refresh failed: ${detail}`, 'error');
      } finally {
        if (refreshId === state.refreshSeq) {
          if (state.refreshTickTimer) {
            clearInterval(state.refreshTickTimer);
            state.refreshTickTimer = null;
          }
          state.refreshAbort = null;
          state.activeRefresh = null;
          updateAllPageRefreshStates();
        }
      }
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
      markLoadedViewsStale('product selection changed');
      scheduleRefresh(0);
    });

    document.getElementById('search').addEventListener('input', async (e) => {
      state.q = e.target.value.trim();
      state.treeTouched = false;
      state.treeOpen.clear();
      markLoadedViewsStale('search changed');
      scheduleRefresh(250);
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

    document.addEventListener('keydown', async (event) => {
      if (event.defaultPrevented) {
        return;
      }
      if (event.ctrlKey || event.metaKey || event.altKey) {
        return;
      }
      if (event.key === 'Escape') {
        if (closeShortcutHelp()) {
          event.preventDefault();
          return;
        }
        if (isItemModalOpen()) {
          event.preventDefault();
          closeModal();
          return;
        }
        if (closeOpenDetailPanels()) {
          event.preventDefault();
        }
        return;
      }
      if (isTypingContext(event.target)) {
        return;
      }
      if (event.key === '?') {
        event.preventDefault();
        toggleShortcutHelp();
        return;
      }
      const interactiveTarget = event.target?.closest?.('button, a[href], summary');
      if (interactiveTarget) {
        return;
      }
      if (isShortcutHelpOpen() || isItemModalOpen()) {
        return;
      }
      if (event.key === '/') {
        event.preventDefault();
        focusSearchInput();
        return;
      }
      if (event.key === 'j' || event.key === 'ArrowDown') {
        if (selectItemByDelta(1)) {
          event.preventDefault();
        }
        return;
      }
      if (event.key === 'k' || event.key === 'ArrowUp') {
        if (selectItemByDelta(-1)) {
          event.preventDefault();
        }
        return;
      }
      if (event.key === 'Enter') {
        if (await openSelectedItem()) {
          event.preventDefault();
        }
        return;
      }
      if (event.key === 'r') {
        event.preventDefault();
        document.getElementById('refresh').click();
      }
    });

    document.querySelectorAll('.tab-btn[data-tab]').forEach((btn) => {
      btn.addEventListener('click', async () => {
        setActiveTab(btn.getAttribute('data-tab') || 'tree');
        await ensureActiveTabLoaded({ reason: 'tab selected' });
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
      markLoadedViewsStale('product filter changed');
      scheduleRefresh(120);
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
      markLoadedViewsStale('state filter changed');
      scheduleRefresh(120);
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
      markLoadedViewsStale('type filter changed');
      scheduleRefresh(120);
    });

    document.getElementById('limit').addEventListener('change', async (event) => {
      const parsed = Number.parseInt(event.target.value, 10);
      state.limit = Number.isFinite(parsed) ? Math.max(1, Math.min(parsed, 1000)) : 200;
      event.target.value = String(state.limit);
      markLoadedViewsStale('limit changed');
      scheduleRefresh(0);
    });

    document.getElementById('graph-mode').addEventListener('change', async (event) => {
      const nextMode = normalizeGraphMode(event.target.value);
      state.graphMode = nextMode;
      const preset = syncGraphModeControls();
      updateUrlState();
      markGraphTabStale('graph mode changed');
      if (state.activeTab === 'graph') {
        scheduleRefresh(0);
        return;
      }
      updateAllPageRefreshStates();
      setStatus(`Graph mode set to ${preset?.label || nextMode}`);
    });

    document.getElementById('graph-max-depth').addEventListener('change', async (event) => {
      const nextDepth = boundedPositiveInt(event.target.value, state.graphMaxDepth, graphDepthBounds.max);
      state.graphMaxDepth = Math.max(graphDepthBounds.min, nextDepth);
      event.target.value = String(state.graphMaxDepth);
      updateFocusGraphPageChrome();
      updateUrlState();
      markGraphTabStale('graph depth changed');
      if (state.activeTab === 'graph') {
        scheduleRefresh(0);
        return;
      }
      updateAllPageRefreshStates();
      setStatus(`Graph depth set to ${state.graphMaxDepth}`);
    });

    document.getElementById('graph-isolation-mode').addEventListener('change', async (event) => {
      state.graphIsolationMode = normalizeGraphIsolationMode(event.target.value);
      updateFocusGraphPageChrome();
      updateUrlState();
      if (state.activeTab === 'graph' && state.graphPayload && state.graphItemId) {
        renderGraphView(state.graphPayload);
        setStatus(`Graph isolation set to ${graphIsolationModeLabel(state.graphIsolationMode)}`);
        return;
      }
      if (state.activeTab === 'graph') {
        scheduleRefresh(0);
        return;
      }
      setStatus(`Graph isolation set to ${graphIsolationModeLabel(state.graphIsolationMode)}`);
    });

    document.getElementById('graph-reset-scope').addEventListener('click', async () => {
      resetGraphScope();
    });
)JS";

inline constexpr std::string_view kBackboardAppJsPart7 = R"JS(

    document.getElementById('refresh').addEventListener('click', async () => {
      const products = selectedProductValues();
      const refreshScope = products.length === 1 ? products[0] : 'all';
      if (state.refreshAbort) {
        if (state.activeRefresh) {
          state.activeRefresh.abort_reason = 'superseded by manual cache refresh';
          state.activeRefresh.status = 'aborted';
        }
        state.refreshAbort.abort();
      }
      const refreshId = ++state.refreshSeq;
      const controller = new AbortController();
      state.refreshAbort = controller;
      state.refreshCancelRequested = false;
      markLoadedViewsStale('manual cache refresh requested');
      const refresh = createRefreshContext(refreshId, state.activeTab, 'manual cache invalidation', {
        cache_status: 'invalidating',
        stale_reason: 'manual cache refresh requested',
      });
      state.activeRefresh = refresh;
      setStatus(`Refreshing ${refresh.product_scope_label}...`);
      setBusy(true);
      setPageRefreshState(state.activeTab, true);
      updateBusyFromRefresh(refresh);
      try {
        await getJson(`/api/refresh?product=${encodeURIComponent(refreshScope)}`, {
          signal: controller.signal,
          refresh,
          stage: 'cache.invalidate',
        });
        if (refreshId !== state.refreshSeq) {
          return;
        }
        refresh.status = 'cache-invalidated';
        refresh.cache_status = 'invalidated';
        state.lastRefreshDiagnostic =
            makeRefreshDiagnostic('cache-invalidated', 'Backlog cache invalidated', refresh.started_at_ms, [], refresh);
        state.refreshAbort = null;
        state.activeRefresh = null;
        setStatus(`Refresh requested in ${formatElapsed(refresh.started_at_ms)}`);
        await refreshActiveTab({
          force: true,
          reason: 'manual refresh after cache invalidation',
          stale_reason: 'manual cache refresh requested',
        });
      } catch (error) {
        const detail = describeRefreshError(error);
        refresh.status = controller.signal.aborted ? 'canceled' : 'failed';
        refresh.abort_reason = controller.signal.aborted ? detail : '';
        refresh.error = detail;
        state.lastRefreshDiagnostic =
            makeRefreshDiagnostic(refresh.status, detail, refresh.started_at_ms, [{ status: 'rejected', reason: error }], refresh);
        updateBusy('Refresh failed', `${detail}; elapsed ${formatElapsed(refresh.started_at_ms)}; endpoint=${refresh.active_endpoint || '/api/refresh'}`);
        setBusy(false, true);
        setStatus(`Refresh failed: ${detail}`, 'error');
      } finally {
        if (refreshId === state.refreshSeq) {
          state.refreshAbort = null;
          state.activeRefresh = null;
          updateAllPageRefreshStates();
        }
      }
    });

    document.getElementById('busy-cancel').addEventListener('click', () => {
      if (!state.refreshAbort) {
        return;
      }
      state.refreshCancelRequested = true;
      if (state.activeRefresh) {
        state.activeRefresh.cancel_reason = 'canceled by user';
        state.activeRefresh.abort_reason = 'canceled by user';
      }
      state.refreshAbort.abort();
      updateBusy('Canceling refresh', 'Waiting for in-flight requests to stop');
      setStatus('Canceling refresh...');
    });

    document.getElementById('busy-retry').addEventListener('click', () => {
      scheduleRefresh(0);
    });

    document.getElementById('busy-copy').addEventListener('click', copyRefreshDiagnostic);

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
    document.getElementById('shortcut-help-button').addEventListener('click', toggleShortcutHelp);
    document.getElementById('shortcut-help-close').addEventListener('click', closeShortcutHelp);
    document.getElementById('shortcut-help-backdrop').addEventListener('click', (event) => {
      if (event.target.id === 'shortcut-help-backdrop') {
        closeShortcutHelp();
      }
    });

    (async () => {
      setActiveTab(state.activeTab);
      syncGraphModeControls();
      state.workspaces = loadSavedWorkspaces();
      renderWorkspaceList();
      await loadWorkspaceInfo();
      document.getElementById('workspace-input').value = state.workspace || '';
      document.getElementById('search').value = state.q;
      document.getElementById('limit').value = String(state.limit);
      await loadProducts();
      await ensureActiveTabLoaded({ reason: 'initial active tab load' });
      await restoreItemModalFromQuery();
    })();
)JS";

inline const std::string& BackboardAppJs() {
  static const std::string js = [] {
    std::string text;
    text.reserve(
        kBackboardAppJsPart1.size() + kBackboardAppJsPart1a.size() +
        kBackboardAppJsPart1b.size() + kBackboardAppJsPart1bb.size() +
        kBackboardAppJsPart1c.size() +
        kBackboardAppJsPart2.size() +
        kBackboardAppJsPart3.size() + kBackboardAppJsPart4.size() +
        kBackboardAppJsPart5.size() + kBackboardAppJsPart5a.size() +
        kBackboardAppJsPart5aa.size() + kBackboardAppJsPart5aaa.size() +
        kBackboardAppJsPart5aaab.size() +
        kBackboardAppJsPart5ab.size() +
        kBackboardAppJsPart5ac.size() +
        kBackboardAppJsPart5b.size() +
        kBackboardAppJsPart6.size() +
        kBackboardAppJsPart7.size());
    text.append(kBackboardAppJsPart1);
    text.append(kBackboardAppJsPart1a);
    text.append(kBackboardAppJsPart1b);
    text.append(kBackboardAppJsPart1bb);
    text.append(kBackboardAppJsPart1c);
    text.append(kBackboardAppJsPart2);
    text.append(kBackboardAppJsPart3);
    text.append(kBackboardAppJsPart4);
    text.append(kBackboardAppJsPart5);
    text.append(kBackboardAppJsPart5a);
    text.append(kBackboardAppJsPart5aa);
    text.append(kBackboardAppJsPart5aaa);
    text.append(kBackboardAppJsPart5aaab);
    text.append(kBackboardAppJsPart5ab);
    text.append(kBackboardAppJsPart5ac);
    text.append(kBackboardAppJsPart5b);
    text.append(kBackboardAppJsPart6);
    text.append(kBackboardAppJsPart7);
    return text;
  }();
  return js;
}

}  // namespace kano::backlog::webview::assets
