#pragma once

#include <string_view>

namespace kano::backlog::webview::assets {

inline constexpr std::string_view kBackboardGraphInspectorJs = R"JS(

    const graphInspectorRelationshipLimit = 12;
    const graphInspectorDetailRefLimit = 50;

    function graphInspectorNodeForKey(nodes, key) {
      const cleanKey = String(key || '').trim();
      if (!cleanKey) return null;
      return (Array.isArray(nodes) ? nodes : []).find((node) =>
        graphCanonicalNodeKey(node) === cleanKey
      ) || null;
    }

    function graphInspectorSelectedNode(nodes = state.graphPayload?.nodes || []) {
      return graphInspectorNodeForKey(nodes, state.graphSelectedNodeKey);
    }

    function isResolvedGraphInspectorNode(node) {
      const kind = String(node?.kind || '').trim().toLowerCase();
      const nodeId = String(node?.id || '').trim().toLowerCase();
      return Boolean(node && !node.missing && kind !== 'missing' && kind !== 'topic' &&
        !nodeId.startsWith('topic:') && String(node.item_id || '').trim() &&
        String(node.product || '').trim());
    }

    function abortGraphInspectorRequest() {
      const request = state.graphInspectorRequest;
      if (!request) return;
      request.controller?.abort?.();
      if (state.graphInspectorStatuses.get(request.key)?.state === 'loading') {
        state.graphInspectorStatuses.delete(request.key);
      }
      state.graphInspectorRequest = null;
    }

    function clearGraphInspectionScope(reason = '') {
      state.graphInspectorGeneration += 1;
      abortGraphInspectorRequest();
      state.graphSelectedNodeKey = '';
      state.graphInspectionFocusKey = '';
      state.graphPinnedNodeKeys.clear();
      state.graphHiddenNodeKeys.clear();
      state.graphInspectorDetails.clear();
      state.graphInspectorStatuses.clear();
      state.graphInspectorNotice = reason
        ? `Node inspector state cleared: ${reason}.`
        : '';
    }

    function pruneGraphInspectionState(nodes) {
      const validKeys = new Set((Array.isArray(nodes) ? nodes : [])
        .map((node) => graphCanonicalNodeKey(node))
        .filter(Boolean));
      const pruneSet = (values) => {
        [...values].forEach((key) => {
          if (!validKeys.has(key)) values.delete(key);
        });
      };
      const pruneMap = (values) => {
        [...values.keys()].forEach((key) => {
          if (!validKeys.has(key)) values.delete(key);
        });
      };
      pruneSet(state.graphPinnedNodeKeys);
      pruneSet(state.graphHiddenNodeKeys);
      pruneMap(state.graphInspectorDetails);
      pruneMap(state.graphInspectorStatuses);
      if (state.graphInspectorRequest &&
          !validKeys.has(state.graphInspectorRequest.key)) {
        abortGraphInspectorRequest();
      }
      if (state.graphSelectedNodeKey && !validKeys.has(state.graphSelectedNodeKey)) {
        state.graphSelectedNodeKey = '';
      }
      if (state.graphInspectionFocusKey && !validKeys.has(state.graphInspectionFocusKey)) {
        state.graphInspectionFocusKey = '';
      }
    }

    function normalizeGraphInspectorRefs(value) {
      const normalized = [];
      const seen = new Set();
      (Array.isArray(value) ? value : []).forEach((entry) => {
        const ref = String(entry || '').trim();
        if (!ref || seen.has(ref)) return;
        seen.add(ref);
        normalized.push(ref);
      });
      return {
        values: normalized.slice(0, graphInspectorDetailRefLimit),
        total: normalized.length,
        capped: normalized.length > graphInspectorDetailRefLimit,
      };
    }

    function projectGraphInspectorDetail(item) {
      const links = item?.links || {};
      return {
        product: String(item?.product || '').trim(),
        id: String(item?.id || '').trim(),
        title: String(item?.title || '').trim(),
        type: String(item?.type || '').trim(),
        state: String(item?.state || '').trim(),
        parent: String(item?.parent || '').trim(),
        links: {
          blocked_by: normalizeGraphInspectorRefs(links.blocked_by),
          blocks: normalizeGraphInspectorRefs(links.blocks),
          relates: normalizeGraphInspectorRefs(links.relates),
        },
      };
    }

    async function loadGraphInspectorDetail(node, key) {
      if (!isResolvedGraphInspectorNode(node)) return;
      const itemId = String(node.item_id || '').trim();
      const product = String(node.product || '').trim();
      const requestSeq = ++state.graphInspectorRequestSeq;
      const generation = state.graphInspectorGeneration;
      const controller = new AbortController();
      if (state.graphInspectorRequest?.key !== key) {
        abortGraphInspectorRequest();
      }
      state.graphInspectorRequest = { controller, requestSeq, generation, key };
      try {
        const result = await getJson(
          `/api/items/${encodeURIComponent(itemId)}?product=${encodeURIComponent(product)}`,
          { signal: controller.signal, stage: 'graph.inspector.detail' }
        );
        const active = state.graphInspectorRequest;
        if (generation !== state.graphInspectorGeneration ||
            active?.requestSeq !== requestSeq || active?.key !== key) {
          return;
        }
        const item = result?.data?.item;
        if (!item || String(item.id || '').trim() !== itemId ||
            String(item.product || '').trim() !== product) {
          throw new Error('Exact bounded item metadata did not match the selected graph node');
        }
        state.graphInspectorDetails.set(key, projectGraphInspectorDetail(item));
        state.graphInspectorStatuses.set(key, {
          state: 'loaded',
          message: 'Exact bounded item metadata loaded.',
        });
        if (state.graphSelectedNodeKey === key) {
          rerenderGraphInspectorAfterDetail();
        }
      } catch (error) {
        const active = state.graphInspectorRequest;
        if (generation !== state.graphInspectorGeneration ||
            active?.requestSeq !== requestSeq || active?.key !== key ||
            error?.name === 'AbortError') {
          return;
        }
        state.graphInspectorStatuses.set(key, {
          state: 'error',
          message: `Exact item metadata unavailable: ${error?.message || String(error)}. Graph payload metadata remains visible.`,
        });
        if (state.graphSelectedNodeKey === key) {
          rerenderGraphInspectorAfterDetail();
        }
      } finally {
        const active = state.graphInspectorRequest;
        if (generation === state.graphInspectorGeneration &&
            active?.requestSeq === requestSeq && active?.key === key) {
          state.graphInspectorRequest = null;
        }
      }
    }

    function focusGraphInspectorAction(action) {
      const cleanAction = String(action || '').trim();
      const panel = document.getElementById('graph-node-inspector');
      if (!cleanAction || !panel) return false;
      const target = [...panel.querySelectorAll('[data-graph-inspector-action]')]
        .find((button) =>
          button.getAttribute('data-graph-inspector-action') === cleanAction
        );
      target?.focus?.({ preventScroll: true });
      return Boolean(target);
    }

    function rerenderGraphInspectorAfterDetail() {
      const panel = document.getElementById('graph-node-inspector');
      const active = document.activeElement;
      const panelHadFocus = Boolean(panel && active && panel.contains(active));
      const activeAction = panelHadFocus
        ? active.getAttribute?.('data-graph-inspector-action') || ''
        : '';
      renderGraphView(undefined, {
        preserveViewport: true,
        focusInspector: panelHadFocus && !activeAction,
        focusInspectorAction: activeAction,
      });
    }

    async function selectGraphNode(key, options = {}) {
      const cleanKey = String(key || '').trim();
      const node = graphInspectorNodeForKey(state.graphPayload?.nodes || [], cleanKey);
      if (!node) {
        setStatus('The selected graph node is no longer present in the bounded graph.', 'error');
        return;
      }
      if (state.graphInspectorRequest?.key !== cleanKey) {
        abortGraphInspectorRequest();
      }
      state.graphSelectedNodeKey = cleanKey;
      const shouldLoad = isResolvedGraphInspectorNode(node) &&
        !state.graphInspectorDetails.has(cleanKey) &&
        state.graphInspectorStatuses.get(cleanKey)?.state !== 'loading';
      if (shouldLoad) {
        state.graphInspectorStatuses.set(cleanKey, {
          state: 'loading',
          message: 'Loading exact bounded item metadata…',
        });
      }
      renderGraphView(undefined, {
        preserveViewport: true,
        focusInspector: options.focusInspector !== false,
      });
      if (shouldLoad) {
        await loadGraphInspectorDetail(node, cleanKey);
      }
    }

    function closeGraphNodeInspector() {
      const focusNodeKey = state.graphSelectedNodeKey;
      abortGraphInspectorRequest();
      state.graphSelectedNodeKey = '';
      renderGraphView(undefined, { preserveViewport: true, focusNodeKey });
      setStatus('Graph node inspector closed');
    }

    function toggleGraphNodeIsolation(key) {
      const cleanKey = String(key || '').trim();
      if (!cleanKey) return;
      const isolated = state.graphInspectionFocusKey === cleanKey;
      state.graphInspectionFocusKey = isolated ? '' : cleanKey;
      renderGraphView(undefined, { preserveViewport: true, focusInspector: true });
      setStatus(isolated
        ? 'Restored root-based graph isolation'
        : 'Isolated the bounded graph around the selected node');
    }

    function toggleGraphNodeHidden(key) {
      const cleanKey = String(key || '').trim();
      if (!cleanKey) return;
      if (state.graphPinnedNodeKeys.has(cleanKey)) {
        setStatus('Unpin this node before hiding it.', 'error');
        return;
      }
      const hidden = state.graphHiddenNodeKeys.has(cleanKey);
      if (hidden) {
        state.graphHiddenNodeKeys.delete(cleanKey);
      } else {
        state.graphHiddenNodeKeys.add(cleanKey);
        if (state.graphInspectionFocusKey === cleanKey) {
          state.graphInspectionFocusKey = '';
        }
      }
      renderGraphView(undefined, { preserveViewport: true, focusInspector: true });
      setStatus(hidden
        ? 'Restored the node to the graph canvas'
        : 'Hid the node from the canvas; relationship evidence remains in diagnostics');
    }
)JS" R"JS(
    function toggleGraphNodePinned(key) {
      const cleanKey = String(key || '').trim();
      if (!cleanKey) return;
      const pinned = state.graphPinnedNodeKeys.has(cleanKey);
      if (pinned) {
        state.graphPinnedNodeKeys.delete(cleanKey);
      } else {
        state.graphPinnedNodeKeys.add(cleanKey);
        state.graphHiddenNodeKeys.delete(cleanKey);
      }
      renderGraphView(undefined, { preserveViewport: true, focusInspector: true });
      setStatus(pinned
        ? 'Unpinned the graph node'
        : 'Pinned the graph node in ephemeral view state');
    }

    function graphInspectorEndpointNode(nodes, endpoint) {
      const cleanEndpoint = String(endpoint || '').trim();
      return (Array.isArray(nodes) ? nodes : []).find((node) =>
        String(node.id || '').trim() === cleanEndpoint
      ) || null;
    }

    function graphInspectorDisplayRef(node, fallback) {
      if (!node) return String(fallback || '').trim();
      const itemId = String(node.item_id || node.id || fallback || '').trim();
      const product = String(node.product || '').trim();
      return product && product !== String(graphInspectorSelectedNode()?.product || '').trim()
        ? `${product}:${itemId}`
        : itemId;
    }

    function graphInspectorRefsFromEdges(node, nodes, edges) {
      const endpoint = String(node?.id || '').trim();
      const parent = [];
      const blockers = [];
      const blocked = [];
      const related = [];
      const appendUnique = (target, ref) => {
        const value = String(ref || '').trim();
        if (value && !target.includes(value)) target.push(value);
      };
      (Array.isArray(edges) ? edges : []).forEach((edge) => {
        const from = String(edge?.from || '').trim();
        const to = String(edge?.to || '').trim();
        const kind = String(edge?.kind || '').trim();
        const semantic = String(edge?.semantic || '').trim();
        if (kind === 'parent' && to === endpoint) {
          appendUnique(parent, graphInspectorDisplayRef(
            graphInspectorEndpointNode(nodes, from), from
          ));
        }
        if (semantic === 'dependency' && to === endpoint) {
          appendUnique(blockers, graphInspectorDisplayRef(
            graphInspectorEndpointNode(nodes, from), from
          ));
        }
        if (semantic === 'dependency' && from === endpoint) {
          appendUnique(blocked, graphInspectorDisplayRef(
            graphInspectorEndpointNode(nodes, to), to
          ));
        }
        if (kind === 'relates' && (from === endpoint || to === endpoint)) {
          const other = from === endpoint ? to : from;
          appendUnique(related, graphInspectorDisplayRef(
            graphInspectorEndpointNode(nodes, other), other
          ));
        }
      });
      return {
        parent: parent[0] || '',
        blocked_by: normalizeGraphInspectorRefs(blockers),
        blocks: normalizeGraphInspectorRefs(blocked),
        relates: normalizeGraphInspectorRefs(related),
      };
    }

    function graphInspectorNodeForRef(ref, product, nodes) {
      const cleanRef = String(ref || '').trim();
      const cleanProduct = String(product || '').trim();
      if (!cleanRef) return null;
      const colon = cleanRef.indexOf(':');
      const refProduct = colon >= 0 ? cleanRef.slice(0, colon) : cleanProduct;
      const itemId = colon >= 0 ? cleanRef.slice(colon + 1) : cleanRef;
      const exact = (Array.isArray(nodes) ? nodes : []).filter((node) =>
        String(node.item_id || '').trim() === itemId &&
        (!refProduct || String(node.product || '').trim() === refProduct)
      );
      if (exact.length === 1) return exact[0];
      if (colon < 0) {
        const unique = (Array.isArray(nodes) ? nodes : []).filter((node) =>
          String(node.item_id || '').trim() === itemId
        );
        if (unique.length === 1) return unique[0];
      }
      return null;
    }

    function graphInspectorRefStatus(ref, product, nodes) {
      const target = graphInspectorNodeForRef(ref, product, nodes);
      if (!target) {
        return { label: 'outside bounded view', className: '' };
      }
      const key = graphCanonicalNodeKey(target);
      if (target.missing || String(target.kind || '').toLowerCase() === 'missing') {
        return { label: 'unresolved', className: 'missing' };
      }
      if (state.graphHiddenNodeKeys.has(key)) {
        return { label: 'hidden with diagnostics', className: 'blocked' };
      }
      if (state.graphPinnedNodeKeys.has(key)) {
        return { label: 'pinned', className: 'passed' };
      }
      return { label: 'in bounded view', className: 'passed' };
    }

    function renderGraphInspectorRefList(label, refs, product, nodes, emptyText) {
      const values = Array.isArray(refs?.values) ? refs.values : [];
      const total = Number(refs?.total ?? values.length);
      const visible = values.slice(0, graphInspectorRelationshipLimit);
      const rows = visible.map((ref) => {
        const status = graphInspectorRefStatus(ref, product, nodes);
        return `<li><code>${esc(ref)}</code><span class="pill ${escAttr(status.className)}">${esc(status.label)}</span></li>`;
      }).join('');
      const hiddenCount = Math.max(0, total - visible.length);
      return `<section class="graph-inspector-relationship"><div class="detail-label">${esc(label)} <span class="graph-inspector-count">${total}</span></div>` +
        (rows
          ? `<ul class="graph-inspector-ref-list">${rows}</ul>`
          : `<div class="muted">${esc(emptyText)}</div>`) +
        (hiddenCount
          ? `<div class="muted">${hiddenCount} additional recorded reference(s) remain outside this compact panel.</div>`
          : '') +
        (refs?.capped
          ? `<div class="muted">The exact detail projection is capped at ${graphInspectorDetailRefLimit} references for this relationship.</div>`
          : '') +
        `</section>`;
    }

    function graphInspectorSourceMatches(source, node) {
      const value = String(source || '').trim();
      if (!value) return false;
      return new Set([
        graphCanonicalNodeKey(node),
        String(node?.id || '').trim(),
        String(node?.item_id || '').trim(),
      ].filter(Boolean)).has(value);
    }

    function graphInspectorRecordMatchesNode(entry, node) {
      if (graphInspectorSourceMatches(entry?.source, node)) return true;
      const canonicalTarget = String(entry?.id || '').trim();
      if (!canonicalTarget) return false;
      const values = new Set([
        graphCanonicalNodeKey(node),
        String(node?.id || '').trim(),
      ].filter(Boolean));
      return values.has(canonicalTarget);
    }

    function graphInspectorMissingRefs(node, baseData) {
      const rows = [];
      const seen = new Set();
      const append = (kind, ref, status) => {
        const cleanRef = String(ref || '').trim();
        const key = `${kind}|${cleanRef}|${status}`;
        if (!cleanRef || seen.has(key)) return;
        seen.add(key);
        rows.push({ kind: String(kind || 'reference'), ref: cleanRef, status });
      };
      const payloads = [
        baseData,
        ...state.graphExpansionPayloads.values(),
      ].filter((payload) => payload && typeof payload === 'object');
      payloads.forEach((payload) => {
        (Array.isArray(payload.missing_nodes) ? payload.missing_nodes : [])
          .filter((entry) => graphInspectorRecordMatchesNode(entry, node))
          .forEach((entry) => append(
            entry?.kind, entry?.ref || entry?.id, 'unresolved'
          ));
        (Array.isArray(payload.invalid_refs) ? payload.invalid_refs : [])
          .filter((entry) => graphInspectorRecordMatchesNode(entry, node))
          .forEach((entry) => append(entry?.kind, entry?.ref, 'invalid'));
      });
      return {
        values: rows.slice(0, graphInspectorRelationshipLimit),
        total: rows.length,
      };
    }

    function renderGraphInspectorMissingRefs(node, baseData) {
      const result = graphInspectorMissingRefs(node, baseData);
      const rows = result.values;
      if (!result.total) {
        return '<section class="graph-inspector-relationship"><div class="detail-label">Missing or invalid refs 0</div><div class="muted">No unresolved references for this node are present in the bounded graph response.</div></section>';
      }
      const hiddenCount = Math.max(0, result.total - rows.length);
      return `<section class="graph-inspector-relationship"><div class="detail-label">Missing or invalid refs ${result.total}</div><ul class="graph-inspector-ref-list">${rows.map((entry) =>
        `<li><code>${esc(entry.ref)}</code><span class="pill missing">${esc(entry.kind)} / ${esc(entry.status)}</span></li>`
      ).join('')}</ul>${hiddenCount ? `<div class="muted">${hiddenCount} additional unresolved reference(s) remain outside this compact panel.</div>` : ''}</section>`;
    }
)JS" R"JS(
    function renderGraphNodeInspector(baseData, nodes, edges) {
      const node = graphInspectorSelectedNode(nodes);
      if (!node) return '';
      const key = graphCanonicalNodeKey(node);
      const detail = state.graphInspectorDetails.get(key) || null;
      const status = state.graphInspectorStatuses.get(key) || {};
      const derived = graphInspectorRefsFromEdges(node, nodes, edges);
      const product = String(detail?.product || node.product || '').trim();
      const itemId = String(detail?.id || node.item_id || node.id || '').trim();
      const title = String(detail?.title || node.label || itemId).trim();
      const type = String(detail?.type || node.kind || '').trim();
      const itemState = String(detail?.state || node.state || '').trim();
      const parent = String(detail?.parent || derived.parent || '').trim();
      const blockedBy = detail?.links?.blocked_by || derived.blocked_by;
      const blocks = detail?.links?.blocks || derived.blocks;
      const relates = detail?.links?.relates || derived.relates;
      const resolved = isResolvedGraphInspectorNode(node);
      const pinned = state.graphPinnedNodeKeys.has(key);
      const hidden = state.graphHiddenNodeKeys.has(key);
      const isolated = state.graphInspectionFocusKey === key;
      const expansionReady = resolved && Boolean(state.graphBaseQueryString && state.graphBasePayload);
      const inboundKey = graphExpansionKey(product, itemId, 'inbound');
      const outboundKey = graphExpansionKey(product, itemId, 'outbound');
      const inboundState = state.graphExpansionStatuses.get(inboundKey) || {};
      const outboundState = state.graphExpansionStatuses.get(outboundKey) || {};
      const inboundStatus = inboundState.message || '';
      const outboundStatus = outboundState.message || '';
      const inboundLoading = inboundState.state === 'loading';
      const outboundLoading = outboundState.state === 'loading';
      const relationshipStatus = status.message
        ? `<div class="muted${status.state === 'error' ? ' status-error' : ''}" role="status">${esc(status.message)}</div>`
        : '';
      const unresolvedNotice = resolved
        ? ''
        : '<div class="graph-inspector-warning">This is not a resolved product-qualified item. Detail, root, and expansion actions are unavailable.</div>';
      const hideDisabled = pinned ? ' disabled aria-disabled="true" title="Unpin this node before hiding it"' : '';
      const resolvedDisabled = resolved ? '' : ' disabled aria-disabled="true"';
      const inboundDisabled = expansionReady && !inboundLoading ? '' : ' disabled aria-disabled="true"';
      const outboundDisabled = expansionReady && !outboundLoading ? '' : ' disabled aria-disabled="true"';
      const parentRefs = normalizeGraphInspectorRefs(parent ? [parent] : []);
      return `<aside id="graph-node-inspector" class="graph-node-inspector" tabindex="-1" role="region" aria-labelledby="graph-node-inspector-title" data-graph-selected-key="${escAttr(key)}">` +
        `<div class="graph-inspector-head"><div><div class="detail-label">Selected graph node</div><h4 id="graph-node-inspector-title">${esc(title || itemId)}</h4></div>` +
        `<button type="button" class="btn graph-inspector-close" data-graph-inspector-action="close" aria-label="Close graph node inspector">Close</button></div>` +
        `<div class="graph-inspector-identity"><code>${esc(itemId)}</code><div class="muted">${esc([product, type, itemState].filter(Boolean).join(' / '))}</div>` +
        `<div class="graph-diagnostic-pills"><span class="pill ${resolved ? 'passed' : 'missing'}">${resolved ? 'resolved item' : 'bounded graph record'}</span>${pinned ? '<span class="pill passed">pinned</span>' : ''}${hidden ? '<span class="pill blocked">hidden with diagnostics</span>' : ''}${isolated ? '<span class="pill">local isolate focus</span>' : ''}</div></div>` +
        relationshipStatus + unresolvedNotice +
        `<div class="graph-inspector-actions" role="group" aria-label="Actions for ${escAttr(itemId)}">` +
          `<button type="button" class="btn" data-graph-inspector-action="open-detail"${resolvedDisabled}>Open detail</button>` +
          `<button type="button" class="btn" data-graph-inspector-action="set-root"${resolvedDisabled}>Set root</button>` +
          `<button type="button" class="btn" data-graph-inspector-action="isolate" aria-pressed="${isolated ? 'true' : 'false'}">${isolated ? 'Restore root isolate' : 'Isolate node'}</button>` +
          `<button type="button" class="btn" data-graph-inspector-action="expand-inbound" aria-busy="${inboundLoading ? 'true' : 'false'}"${inboundDisabled}>Expand inbound</button>` +
          `<button type="button" class="btn" data-graph-inspector-action="expand-outbound" aria-busy="${outboundLoading ? 'true' : 'false'}"${outboundDisabled}>Expand outbound</button>` +
          `<button type="button" class="btn" data-graph-inspector-action="hide" aria-pressed="${hidden ? 'true' : 'false'}"${hideDisabled}>${hidden ? 'Restore node' : 'Hide node'}</button>` +
          `<button type="button" class="btn" data-graph-inspector-action="pin" aria-pressed="${pinned ? 'true' : 'false'}">${pinned ? 'Unpin node' : 'Pin node'}</button>` +
        `</div>` +
        ((inboundStatus || outboundStatus)
          ? `<div class="graph-inspector-expansion-status" role="status" aria-live="polite">${inboundStatus ? `<div>${esc(inboundStatus)}</div>` : ''}${outboundStatus ? `<div>${esc(outboundStatus)}</div>` : ''}</div>`
          : '') +
        `<div class="graph-inspector-relationships">` +
          renderGraphInspectorRefList('Parent', parentRefs, product, nodes, 'No parent is recorded in the available metadata.') +
          renderGraphInspectorRefList('Blockers', blockedBy, product, nodes, 'No blockers are recorded in the available metadata.') +
          renderGraphInspectorRefList('Blocked items', blocks, product, nodes, 'No blocked items are recorded in the available metadata.') +
          renderGraphInspectorRefList('Related refs', relates, product, nodes, 'No related refs are recorded in the available metadata.') +
          renderGraphInspectorMissingRefs(node, baseData) +
        `</div>` +
        `<div class="muted graph-inspector-boundary">Compact metadata only. Pin, hide, and local isolate state stay in memory and are cleared with the base graph.</div>` +
      `</aside>`;
    }

    function bindGraphNodeInspectorActions() {
      const panel = document.getElementById('graph-node-inspector');
      if (!panel || panel.__kobGraphInspectorBound) return;
      panel.__kobGraphInspectorBound = true;
      panel.addEventListener('keydown', (event) => {
        if (event.key === 'Escape') {
          event.preventDefault();
          closeGraphNodeInspector();
        }
      });
      panel.querySelectorAll('[data-graph-inspector-action]').forEach((button) => {
        button.addEventListener('click', async () => {
          const action = button.getAttribute('data-graph-inspector-action') || '';
          const node = graphInspectorSelectedNode();
          const key = graphCanonicalNodeKey(node);
          if (action === 'close') {
            closeGraphNodeInspector();
            return;
          }
          if (!node || !key) return;
          const itemId = String(node.item_id || '').trim();
          const product = String(node.product || '').trim();
          if (action === 'open-detail' && isResolvedGraphInspectorNode(node)) {
            await openItemModal(itemId, product);
          } else if (action === 'set-root' && isResolvedGraphInspectorNode(node)) {
            setGraphRoot(itemId, product, { reason: 'explicit graph inspector root action' });
          } else if (action === 'isolate') {
            toggleGraphNodeIsolation(key);
          } else if (action === 'expand-inbound') {
            button.disabled = true;
            button.setAttribute('aria-busy', 'true');
            await expandGraphNode(itemId, product, 'inbound');
            focusGraphInspectorAction(action);
          } else if (action === 'expand-outbound') {
            button.disabled = true;
            button.setAttribute('aria-busy', 'true');
            await expandGraphNode(itemId, product, 'outbound');
            focusGraphInspectorAction(action);
          } else if (action === 'hide') {
            toggleGraphNodeHidden(key);
          } else if (action === 'pin') {
            toggleGraphNodePinned(key);
          }
        });
      });
    }

    function renderGraphEphemeralDiagnostics(nodes, edges) {
      const hidden = (Array.isArray(nodes) ? nodes : []).filter((node) =>
        state.graphHiddenNodeKeys.has(graphCanonicalNodeKey(node))
      );
      const pinned = (Array.isArray(nodes) ? nodes : []).filter((node) =>
        state.graphPinnedNodeKeys.has(graphCanonicalNodeKey(node))
      );
      const hiddenRows = hidden.map((node) => {
        const endpoint = String(node.id || '').trim();
        const dependencyCount = (Array.isArray(edges) ? edges : []).filter((edge) =>
          (String(edge.from || '') === endpoint || String(edge.to || '') === endpoint) &&
          String(edge.semantic || '') === 'dependency'
        ).length;
        return `<div class="graph-view-state-row"><div><code>${esc(node.item_id || node.id || '')}</code><div class="muted">${dependencyCount} dependency edge(s) retained in details and diagnostics.</div></div>` +
          `<button type="button" class="btn" data-graph-hidden-restore-key="${escAttr(graphCanonicalNodeKey(node))}">Restore</button></div>`;
      }).join('');
      const pinnedRows = pinned.map((node) =>
        `<button type="button" class="btn graph-view-state-select" data-graph-state-select-key="${escAttr(graphCanonicalNodeKey(node))}">Inspect pinned ${esc(node.item_id || node.id || '')}</button>`
      ).join('');
      const localFocus = state.graphInspectionFocusKey
        ? `<div class="graph-view-state-row"><div><strong>Local isolate focus</strong><div class="muted"><code>${esc(state.graphInspectionFocusKey)}</code></div></div><button type="button" class="btn" data-graph-clear-local-focus="true">Restore root isolate</button></div>`
        : '';
      return `<section class="graph-view-state-diagnostics" aria-labelledby="graph-view-state-title"><div class="graph-expansion-header"><h4 id="graph-view-state-title">Ephemeral graph view state</h4>` +
        `<div class="muted">Manual hide never deletes relationship evidence. These selections are memory-only and are excluded from URLs, saved queries, and storage.</div></div>` +
        `<div class="graph-diagnostic-pills"><span class="pill ${hidden.length ? 'blocked' : ''}">manually hidden ${hidden.length}</span><span class="pill ${pinned.length ? 'passed' : ''}">pinned ${pinned.length}</span><span class="pill">local isolate ${state.graphInspectionFocusKey ? 1 : 0}</span></div>` +
        (hiddenRows ? `<div class="graph-view-state-list">${hiddenRows}</div>` : '') +
        (pinnedRows ? `<div class="graph-view-state-actions">${pinnedRows}</div>` : '') +
        localFocus +
      `</section>`;
    }

    function bindGraphEphemeralDiagnostics() {
      document.querySelectorAll('[data-graph-hidden-restore-key]').forEach((button) => {
        button.addEventListener('click', () => {
          const key = button.getAttribute('data-graph-hidden-restore-key') || '';
          state.graphHiddenNodeKeys.delete(key);
          renderGraphView(undefined, { preserveViewport: true });
          setStatus('Restored a manually hidden graph node');
        });
      });
      document.querySelectorAll('[data-graph-state-select-key]').forEach((button) => {
        button.addEventListener('click', async () => {
          await selectGraphNode(
            button.getAttribute('data-graph-state-select-key') || '',
            { focusInspector: true }
          );
        });
      });
      document.querySelectorAll('[data-graph-clear-local-focus]').forEach((button) => {
        button.addEventListener('click', () => {
          state.graphInspectionFocusKey = '';
          renderGraphView(undefined, { preserveViewport: true });
          setStatus('Restored root-based graph isolation');
        });
      });
    }

)JS";

}  // namespace kano::backlog::webview::assets
