#pragma once

#include <string>
#include <string_view>

#include "backboard_app_js.hpp"
#include "backboard_css.hpp"

namespace kano::backlog::webview::assets {

inline constexpr std::string_view kIndexHtmlPrefix = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Backboard - Kano Backlog</title>
  <style>
)HTML";

inline constexpr std::string_view kIndexHtmlBetweenAssets = R"HTML(
  </style>
</head>
<body>
  <div class="app-shell">
    <aside class="panel sidebar">
      <h3 style="margin-top:0;">Backboard</h3>
      <div class="muted" style="margin-bottom:8px;">KOB Webview runtime</div>
      <h4 style="margin:10px 0 6px 0;">Workspaces</h4>
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
    <input id="search" placeholder="Search id/title/topic/content" aria-keyshortcuts="/" />
    <label for="limit">Limit:</label>
    <input id="limit" class="limit-input" type="number" min="1" max="1000" value="200" />
    <button id="refresh" aria-keyshortcuts="r">Refresh</button>
    <button id="shortcut-help-button" class="btn" type="button" aria-keyshortcuts="?" aria-controls="shortcut-help-backdrop" aria-expanded="false">Shortcuts ?</button>
    <span id="status-wrap" class="status-wrap" aria-live="polite">
      <span class="spinner" aria-hidden="true"></span>
      <span id="status" class="muted"></span>
    </span>
  </div>

  <div id="busy-banner" class="busy-banner" role="status" aria-live="polite">
    <span class="spinner" aria-hidden="true"></span>
    <div class="busy-body">
      <div id="busy-title" class="busy-title">Loading backlog data</div>
      <div id="busy-detail" class="muted">Starting refresh...</div>
      <div class="busy-progress" aria-hidden="true"><div class="busy-progress-fill"></div></div>
      <div class="busy-actions">
        <button id="busy-cancel" class="btn" type="button">Cancel</button>
        <button id="busy-retry" class="btn" type="button" hidden>Retry</button>
        <button id="busy-copy" class="btn" type="button">Copy diagnostics</button>
      </div>
    </div>
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
      <button id="tab-review" class="tab-btn active" data-tab="review">Review Inbox</button>
      <button id="tab-handoff" class="tab-btn" data-tab="handoff">Handoff Readiness</button>
      <button id="tab-tree" class="tab-btn" data-tab="tree">Product Map</button>
      <button id="tab-roadmap" class="tab-btn" data-tab="roadmap">Roadmap</button>
      <button id="tab-decision-radar" class="tab-btn" data-tab="decision-radar">Decision Radar</button>
      <button id="tab-kanban" class="tab-btn" data-tab="kanban">Flow</button>
      <button id="tab-context" class="tab-btn" data-tab="context">Context</button>
      <button id="tab-graph" class="tab-btn" data-tab="graph">Dependencies</button>
      <button id="tab-runs" class="tab-btn" data-tab="runs">Agent Runs</button>
      <button id="tab-command" class="tab-btn" data-tab="command">Command</button>
    </div>
  </div>

  <div id="page-tree" class="panel tree page">
      <div class="row" style="margin: 0 0 8px 0;">
        <h3 style="margin: 0;">Product Map</h3>
        <button id="expand-all" class="btn">Expand All</button>
        <button id="collapse-all" class="btn">Collapse All</button>
      </div>
      <div id="tree"></div>
  </div>

  <div id="page-roadmap" class="panel page">
    <h3>Version Goal Ledger</h3>
    <div id="roadmap-summary" class="muted" style="margin-bottom:8px;"></div>
    <div id="roadmap-list"></div>
  </div>

  <div id="page-decision-radar" class="panel page">
    <h3>Decision Debt / ADR Revisit Radar</h3>
    <div id="decision-radar-summary" class="muted" style="margin-bottom:8px;"></div>
    <div id="decision-radar-list"></div>
  </div>

  <div id="page-kanban" class="panel page">
      <h3>Flow</h3>
      <div id="kanban" class="kanban"></div>
  </div>

  <div id="page-context" class="panel page">
    <h3>Context (ADR / Topic / Workset)</h3>
    <div id="context-summary" class="muted" style="margin-bottom:8px;"></div>
    <div id="context-list"></div>
  </div>

  <div id="page-review" class="panel page active">
    <h3>Backboard Review Inbox</h3>
    <div id="saved-views" class="row" style="align-items:flex-start; flex-wrap:wrap;"></div>
    <div id="review-inbox" class="review-grid"></div>
  </div>

  <div id="page-handoff" class="panel page">
    <h3>Agent Handoff Readiness</h3>
    <div id="handoff-summary" class="muted" style="margin-bottom:8px;"></div>
    <div id="handoff-list"></div>
  </div>

  <div id="page-graph" class="panel page" data-navigation-model="focus-graph-page">
    <div class="graph-page-head">
      <div class="graph-page-title">
        <h3>Dependency Graph</h3>
        <div id="focus-graph-root-label" class="muted">No item root selected</div>
      </div>
      <a id="focus-graph-back-link" class="btn" href="/?tab=review">Back to Review Inbox</a>
    </div>
    <div class="graph-toolbar">
       <div class="graph-toolbar-row">
         <label class="graph-mode-field" for="graph-mode">
           <span class="filter-group-title">Review mode</span>
           <select id="graph-mode" class="graph-mode-select" aria-label="Dependency graph mode"></select>
         </label>
        <label class="graph-toolbar-field" for="graph-max-depth">
          <span class="filter-group-title">Neighborhood depth</span>
          <input id="graph-max-depth" class="graph-depth-input" type="number" min="1" max="32" step="1" inputmode="numeric" aria-label="Focused graph neighborhood depth" value="2" />
        </label>
        <label class="graph-toolbar-field" for="graph-isolation-mode">
          <span class="filter-group-title">Isolation display</span>
          <select id="graph-isolation-mode" class="graph-mode-select" aria-label="Focused graph isolation display mode"></select>
        </label>
         <div class="graph-toolbar-actions">
           <button id="graph-reset-scope" class="btn" type="button">Reset scope</button>
         </div>
       </div>
       <div class="graph-toolbar-row graph-toolbar-row-secondary">
         <div class="graph-toolbar-actions graph-viewport-actions" role="toolbar" aria-label="Graph viewport controls">
           <button id="graph-zoom-out" class="btn graph-viewport-btn" type="button" aria-label="Zoom out">Zoom out</button>
           <button id="graph-zoom-in" class="btn graph-viewport-btn" type="button" aria-label="Zoom in">Zoom in</button>
           <button id="graph-fit-all" class="btn graph-viewport-btn" type="button" aria-label="Fit all graph nodes">Fit all</button>
           <button id="graph-fit-focus" class="btn graph-viewport-btn" type="button" aria-label="Fit focused subgraph">Fit focused subgraph</button>
           <button id="graph-reset-view" class="btn graph-viewport-btn" type="button" aria-label="Reset graph view">Reset view</button>
         </div>
         <div class="muted graph-viewport-help">Focus the graph canvas to pan with arrow keys and zoom with +, -, or 0. Drag to pan and use the mouse wheel to zoom around the pointer.</div>
       </div>
       <div id="graph-scope-help" class="muted graph-scope-help">Click a node to re-root this bounded graph and keep diagnostics visible for unrelated nodes.</div>
       <div id="graph-mode-help" class="muted graph-mode-help"></div>
     </div>
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

  <div id="item-modal-backdrop" class="modal-backdrop" aria-hidden="true">
    <div class="modal" role="dialog" aria-modal="true" aria-labelledby="item-modal-title">
      <div class="modal-head">
        <strong id="item-modal-title">Item Detail</strong>
        <button id="item-modal-close" class="btn" aria-keyshortcuts="Escape">Close</button>
      </div>
      <div id="item-modal-body" class="modal-body"></div>
    </div>
  </div>

  <div id="shortcut-help-backdrop" class="modal-backdrop" aria-hidden="true">
    <div class="modal shortcut-help" role="dialog" aria-modal="true" aria-labelledby="shortcut-help-title">
      <div class="modal-head">
        <strong id="shortcut-help-title">Backboard keyboard shortcuts</strong>
        <button id="shortcut-help-close" class="btn" type="button" aria-keyshortcuts="Escape">Close</button>
      </div>
      <div class="modal-body">
        <div class="shortcut-callout"><span class="pill">Visible help</span><span class="muted">Shortcuts stay inactive while you type in inputs, textareas, selects, or editable content.</span></div>
        <div class="shortcut-grid">
          <span class="kbd">/</span><div>Focus the backlog search field.</div>
          <span class="kbd">j / k</span><div>Move selection to the next or previous visible item card.</div>
          <span class="kbd">↑ / ↓</span><div>Move selection through the same visible card list with arrow keys.</div>
          <span class="kbd">Enter</span><div>Open the currently selected item detail.</div>
          <span class="kbd">Esc</span><div>Close shortcut help first, then item detail. If no dialog is open, close the raw markdown details panel when available.</div>
          <span class="kbd">r</span><div>Refresh backlog data for the current scope.</div>
          <span class="kbd">?</span><div>Open or close this shortcut overlay.</div>
          <span class="kbd">Alt+1..9</span><div>Keep the existing workspace switch shortcuts.</div>
        </div>
      </div>
    </div>
  </div>

  <script src="/assets/kob-ui.js"></script>
  <script>
)HTML";

inline constexpr std::string_view kIndexHtmlSuffix = R"HTML(
  </script>
</body>
</html>
)HTML";

inline const std::string& IndexHtml() {
  static const std::string html = [] {
    std::string text;
    text.reserve(
        kIndexHtmlPrefix.size() + BackboardCss().size() +
        kIndexHtmlBetweenAssets.size() + BackboardAppJs().size() +
        kIndexHtmlSuffix.size());
    text.append(kIndexHtmlPrefix);
    text.append(BackboardCss());
    text.append(kIndexHtmlBetweenAssets);
    text.append(BackboardAppJs());
    text.append(kIndexHtmlSuffix);
    return text;
  }();
  return html;
}

}  // namespace kano::backlog::webview::assets
