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

const char* kKobUiJs = R"JS(
(function () {
  'use strict';

  const timers = new Map();

  function targetElement(target) {
    if (!target) return null;
    if (typeof target === 'string') return document.querySelector(target);
    return target;
  }

  function showError(target, message) {
    const element = targetElement(target);
    const text = String(message || 'Request failed');
    const html = '<div class="card" role="alert"><strong>Unable to load content</strong><div class="muted">' +
      text.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;') +
      '</div></div>';
    if (element) {
      element.innerHTML = html;
    }
    return html;
  }

  function swapTarget(target, html) {
    const element = targetElement(target);
    if (!element) {
      throw new Error('Partial target not found');
    }
    element.innerHTML = html;
    element.dispatchEvent(new CustomEvent('kob-ui:swapped', { bubbles: true, detail: { target: element } }));
    return element;
  }

  async function fetchPartial(url, options) {
    const settings = options || {};
    const timeoutMs = Number(settings.timeoutMs || 15000);
    const controller = new AbortController();
    const externalSignal = settings.signal;
    const timeout = setTimeout(function () {
      controller.abort();
    }, timeoutMs);

    if (externalSignal) {
      if (externalSignal.aborted) {
        controller.abort();
      } else {
        externalSignal.addEventListener('abort', function () {
          controller.abort();
        }, { once: true });
      }
    }

    try {
      if (settings.onStatus) settings.onStatus('loading');
      const response = await fetch(url, {
        method: settings.method || 'GET',
        signal: controller.signal,
        headers: { 'Accept': 'text/html' },
      });
      const html = await response.text();
      if (!response.ok) {
        throw new Error('HTTP ' + response.status);
      }
      if (settings.target) {
        swapTarget(settings.target, html);
      }
      if (settings.onStatus) settings.onStatus('loaded');
      return html;
    } catch (error) {
      if (settings.target) {
        showError(settings.target, error && error.message ? error.message : String(error));
      }
      if (settings.onStatus) settings.onStatus('failed');
      throw error;
    } finally {
      clearTimeout(timeout);
    }
  }

  function debounce(key, run, delayMs) {
    const name = String(key || 'default');
    if (timers.has(name)) {
      clearTimeout(timers.get(name));
    }
    const timer = setTimeout(function () {
      timers.delete(name);
      run();
    }, Number(delayMs || 0));
    timers.set(name, timer);
    return timer;
  }

  function cancelDebounce(key) {
    const name = String(key || 'default');
    if (!timers.has(name)) return;
    clearTimeout(timers.get(name));
    timers.delete(name);
  }

  function setQueryState(update, options) {
    const settings = options || {};
    const url = new URL(window.location.href);
    Object.entries(update || {}).forEach(function (entry) {
      const key = entry[0];
      const value = entry[1];
      if (value === null || value === undefined || value === '') {
        url.searchParams.delete(key);
      } else {
        url.searchParams.set(key, String(value));
      }
    });
    if (settings.replace !== false) {
      window.history.replaceState({}, '', url);
    } else {
      window.history.pushState({}, '', url);
    }
  }

  function readQueryState() {
    const result = {};
    new URLSearchParams(window.location.search).forEach(function (value, key) {
      result[key] = value;
    });
    return result;
  }

  function bindDelegates(root) {
    const container = root || document;
    if (container.__kobUiDelegatesBound) return;
    container.__kobUiDelegatesBound = true;
    container.addEventListener('click', function (event) {
      const trigger = event.target && event.target.closest
        ? event.target.closest('[data-kob-partial]')
        : null;
      if (!trigger) return;
      event.preventDefault();
      const url = trigger.getAttribute('data-kob-partial');
      const target = trigger.getAttribute('data-kob-target');
      if (!url || !target) return;
      fetchPartial(url, { target: target }).catch(function () {});
    });
  }

  window.KobUi = {
    bindDelegates: bindDelegates,
    cancelDebounce: cancelDebounce,
    debounce: debounce,
    fetchPartial: fetchPartial,
    readQueryState: readQueryState,
    setQueryState: setQueryState,
    showError: showError,
    swapTarget: swapTarget,
  };

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', function () { bindDelegates(document); });
  } else {
    bindDelegates(document);
  }
})();
)JS";

const char* kIndexHtml = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Backboard - Kano Backlog</title>
  <style>
    :root { --kob-accent: #1f4fa3; --kob-accent-soft: #f2f6ff; --kob-accent-border: #9fb5de; --kob-border: #cfd9ea; --kob-border-strong: #9eb3d7; --kob-surface: #fcfdff; --kob-surface-strong: #ffffff; --kob-shadow: rgba(30, 55, 95, 0.12); }
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
    .card { position: relative; border: 1px solid #cfd9ea; border-radius: 8px; padding: 8px; margin-bottom: 8px; background: #fcfdff; }
    .card[data-selectable-item] { cursor: pointer; transition: border-color 0.16s ease, box-shadow 0.16s ease, transform 0.16s ease; }
    .card[data-selectable-item]:hover { border-color: var(--kob-accent-border); box-shadow: 0 4px 10px var(--kob-shadow); }
    .card[data-selectable-item]:focus-visible { outline: 3px solid var(--kob-accent-border); outline-offset: 2px; }
    .card.is-selected { border-color: var(--kob-accent); border-left-width: 6px; padding-left: 10px; background: var(--kob-surface-strong); box-shadow: 0 0 0 1px var(--kob-accent), 0 8px 18px var(--kob-shadow); transform: translateX(2px); }
    .review-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 10px; }
    .review-lane { border: 1px solid #d8e1f0; border-radius: 8px; padding: 8px; background: #fff; min-height: 120px; }
    .lane-items, .review-lane-items, .context-items, .command-items { margin-top: 8px; }
    .review-reason { margin-top: 6px; padding-top: 6px; border-top: 1px solid #edf1f7; }
    .evidence-row { display: grid; grid-template-columns: 130px 90px minmax(0, 1fr); gap: 8px; padding: 4px 0; border-bottom: 1px solid #edf1f7; }
    .pill { display: inline-block; border: 1px solid #cfd9ea; border-radius: 999px; padding: 2px 7px; font-size: 12px; background: #f8fbff; }
    .pill.passed { border-color: #86c48b; background: #f1fbf2; color: #245c2a; }
    .pill.failed { border-color: #db8a8a; background: #fff4f4; color: #8a2525; }
    .pill.blocked { border-color: #d5b15d; background: #fff9e8; color: #7a5610; }
    .pill.missing { border-color: #c5ccd9; background: #f4f6fa; color: #4f5a6e; }
    .gate-strip { display: flex; flex-wrap: wrap; gap: 6px; margin: 6px 0; }
    .gate-badge { display: inline-flex; align-items: center; gap: 6px; font-size: 11px; font-weight: 700; letter-spacing: 0.01em; }
    .gate-badge::before { content: attr(data-gate-symbol); font-size: 11px; line-height: 1; }
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
    .status-wrap { display: inline-flex; align-items: center; gap: 7px; min-width: 190px; }
    .spinner { width: 14px; height: 14px; border: 2px solid #cbd6e8; border-top-color: #1f4fa3; border-radius: 50%; animation: spin 0.8s linear infinite; }
    .status-wrap .spinner { display: none; }
    .status-wrap.busy .spinner { display: inline-block; }
    .busy-banner { display: none; align-items: center; gap: 12px; border: 1px solid #b9c9e8; border-left: 5px solid #1f4fa3; background: #f5f8ff; border-radius: 8px; padding: 10px 12px; margin-bottom: 12px; box-shadow: 0 1px 2px rgba(30, 55, 95, 0.08); }
    .busy-banner.visible { display: flex; }
    .busy-body { flex: 1; min-width: 0; }
    .busy-title { font-weight: 700; color: #1d3158; margin-bottom: 4px; }
    .busy-progress { position: relative; height: 5px; overflow: hidden; border-radius: 999px; background: #dfe8f7; margin-top: 8px; }
    .busy-progress-fill { position: absolute; inset: 0 auto 0 0; width: 42%; border-radius: 999px; background: linear-gradient(90deg, #1f4fa3, #4d7ed6); animation: busy-slide 1.1s ease-in-out infinite; }
    .busy-actions { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 8px; }
    .status-error { color: #9c1c1c; font-weight: 600; }
    @keyframes spin { to { transform: rotate(360deg); } }
    @keyframes busy-slide { 0% { transform: translateX(-105%); } 50% { transform: translateX(65%); } 100% { transform: translateX(245%); } }
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
    .shortcut-help .modal-body { display: grid; gap: 12px; }
    .shortcut-grid { display: grid; grid-template-columns: minmax(72px, auto) minmax(0, 1fr); gap: 10px 12px; align-items: start; }
    .kbd { display: inline-flex; align-items: center; justify-content: center; min-width: 42px; padding: 4px 8px; border-radius: 8px; border: 1px solid var(--kob-border-strong); background: var(--kob-accent-soft); font-size: 12px; font-weight: 700; }
    .shortcut-callout { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
    .modal pre { white-space: pre-wrap; word-break: break-word; background: #f6f8fc; border: 1px solid #dfe6f3; border-radius: 8px; padding: 10px; }
    .detail-shell { display: grid; gap: 12px; }
    .detail-header { display: grid; gap: 10px; }
    .detail-title-row { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }
    .detail-facts { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 10px; }
    .detail-fact { border: 1px solid #dfe6f3; border-radius: 8px; padding: 8px; background: #fcfdff; min-width: 0; }
    .detail-sections { display: grid; gap: 12px; }
    .detail-stack { display: grid; gap: 8px; }
    .detail-label { display: block; margin-bottom: 4px; color: #586074; font-size: 11px; text-transform: uppercase; font-weight: 700; letter-spacing: 0.03em; }
    .detail-value { min-width: 0; word-break: break-word; }
    .detail-links { display: flex; gap: 6px; flex-wrap: wrap; }
    .detail-kv-list { display: grid; gap: 8px; }
    .detail-kv { display: grid; grid-template-columns: 120px minmax(0, 1fr); gap: 8px; align-items: start; }
    .detail-toggle summary { cursor: pointer; }
    .detail-toggle-summary { display: inline-flex; align-items: center; gap: 8px; }
    .detail-toggle-summary::before { content: '▸'; color: #5a6d8f; }
    .detail-toggle[open] .detail-toggle-summary::before { content: '▾'; }
    .item-link.pill { text-decoration: none; }
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
    .md-view pre code { display: block; background: transparent; padding: 0; border-radius: 0; overflow-x: auto; }
    .md-code-lang { display: inline-block; margin-bottom: 6px; color: #66738a; font-size: 11px; text-transform: uppercase; letter-spacing: 0; }
    .md-view a { color: #1f4fa3; text-decoration: none; }
    .md-view a:hover { text-decoration: underline; }
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
      <button id="tab-tree" class="tab-btn" data-tab="tree">Product Map</button>
      <button id="tab-kanban" class="tab-btn" data-tab="kanban">Flow</button>
      <button id="tab-context" class="tab-btn" data-tab="context">Context</button>
      <button id="tab-graph" class="tab-btn" data-tab="graph">Dependencies</button>
      <button id="tab-runs" class="tab-btn" data-tab="runs">Agent Runs</button>
      <button id="tab-command" class="tab-btn" data-tab="command">Command</button>
    </div>
  </div>

)HTML"
R"HTML(

  <div id="page-tree" class="panel tree page">
      <div class="row" style="margin: 0 0 8px 0;">
        <h3 style="margin: 0;">Product Map</h3>
        <button id="expand-all" class="btn">Expand All</button>
        <button id="collapse-all" class="btn">Collapse All</button>
      </div>
      <div id="tree"></div>
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

)HTML"
R"HTML(  <script src="/assets/kob-ui.js"></script>
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
      activeTab: 'review',
      allItems: [],
      refreshSeq: 0,
      refreshAbort: null,
      refreshTimer: null,
      refreshTickTimer: null,
        refreshCancelRequested: false,
        lastRefreshDiagnostic: null,
        selectedItemKey: '',
        selectedItemId: '',
        selectedItemProduct: '',
        selectedItemVisibleIndex: -1,
        shortcutHelpOpen: false,
        reviewActorAlias: 'human-reviewer'
    };
    const lanes = ['Backlog', 'Doing', 'Blocked', 'Review', 'Done'];
    const reviewQueueOrder = ['Needs Review', 'Done Candidate', 'False Done Suspect', 'Evidence Gap', 'Blocked/Dirty', 'Stale/Drift', 'Ready Frontier'];
    const itemStates = ['Proposed', 'Ready', 'InProgress', 'Blocked', 'Review', 'Done', 'Dropped'];
    const itemTypes = ['Theme', 'Epic', 'Feature', 'UserStory', 'Task', 'Bug', 'Issue', 'ADR', 'Topic', 'Workset'];
    const workspaceStorageKey = 'kano_webview_workspaces_v2';

    function tokenSetFromQuery(value, fallback) {
      if (!value) return new Set(fallback);
      if (value === '__none__') return new Set();
      return new Set(String(value).split(',').map((x) => x.trim()).filter(Boolean));
    }

    (function applyInitialQueryState() {
      const query = window.KobUi?.readQueryState?.() || {};
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
      if (query.tab && ['review', 'tree', 'kanban', 'context', 'graph', 'runs', 'command'].includes(query.tab)) {
        state.activeTab = query.tab;
      }
    })();

    async function getJson(url, options = {}) {
      const resp = await fetch(url, { signal: options.signal });
      let body = null;
      try {
        body = await resp.json();
      } catch (_e) {
        body = {};
      }
      if (!resp.ok || body?.ok === false) {
        const detail = body?.data?.error || body?.error || `HTTP ${resp.status}`;
        throw new Error(detail);
      }
      return body;
    }

    async function postJson(url, payload, options = {}) {
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
        throw new Error(detail);
      }
      return body;
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

)HTML"
R"HTML(
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

    function describeRefreshError(error) {
      if (error?.name === 'AbortError') {
        return 'Refresh replaced by a newer request';
      }
      return error?.message || String(error || 'Unknown refresh error');
    }

    function makeRefreshDiagnostic(status, detail, startedAt, failures = []) {
      return {
        status,
        detail,
        generated_at: nowIso(),
        elapsed: formatElapsed(startedAt || Date.now()),
        workspace: state.workspace || '',
        product_scope: selectedProductValues(),
        query: state.q,
        states: [...state.selectedStates],
        types: [...state.selectedTypes],
        limit: state.limit,
        active_tab: state.activeTab,
        failures: failures.map((failure) => ({
          status: failure.status || 'rejected',
          reason: describeRefreshError(failure.reason || failure),
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

    function updateUrlState() {
      const products = selectedProductValues();
      const update = {
        tab: state.activeTab,
        product: null,
        products: null,
        q: state.q || null,
        state: selectedTokens(state.selectedStates, itemStates) || null,
        type: selectedTokens(state.selectedTypes, itemTypes) || null,
        limit: String(state.limit || 200),
      };
      if (products.length === 1) {
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

    function safeHref(href) {
      const value = String(href || '').trim();
      if (!value) return '#';
      if (/^(https?:|mailto:|#|\/)/i.test(value)) return value;
      if (/^[A-Za-z][A-Za-z0-9+.-]*:/i.test(value)) return '#';
      return value;
    }

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

)HTML"
R"HTML(
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

    function renderReviewCard(bundle, lane) {
      const item = bundle?.item || {};
      const reason = bundle?.review_reason || `Queued for ${lane || 'review'} review.`;
      const reasonCode = bundle?.reason_code ? `<code>${esc(bundle.reason_code)}</code> ` : '';
      const decision = bundle?.suggested_human_decision ? `<div class="muted">Suggested decision: ${esc(bundle.suggested_human_decision)}</div>` : '';
      const actions = (bundle?.actions || []).map((action) =>
        `<button class="btn review-action" data-review-action="submit" data-human-decision="${escAttr(action.human_decision || action.id || '')}" data-target-state="${escAttr(action.target_state || '')}" data-requires-confirmation="${action.requires_confirmation ? 'true' : 'false'}">${esc(action.label || action.id || 'Submit decision')}</button>`
      ).join(' ');
      return `<div class="card" data-review-card="true" data-review-lane="${escAttr(lane || '')}" data-review-reason-code="${escAttr(bundle?.reason_code || '')}" data-review-suggested-decision="${escAttr(bundle?.suggested_decision || bundle?.suggested_human_decision || '')}" data-review-source-detector="${escAttr(bundle?.diagnostic_status || 'backboard-review-inbox')}" ${selectableItemAttrs(item)}>${renderItemCardSummary(item)}<div class="muted review-reason"><strong>Why this needs review:</strong> ${reasonCode}${esc(reason)}${decision}</div><label class="muted" style="display:block;margin-top:8px;">Draft review note<textarea data-review-draft-note="true" rows="3" style="width:100%;box-sizing:border-box;margin-top:4px;" placeholder="Write rationale or instructions before submitting"></textarea></label><div class="review-actions" style="display:flex;flex-wrap:wrap;gap:6px;margin-top:8px;"><button class="btn" data-review-action="save-draft">Save Draft</button>${actions}</div><div class="muted" data-review-status style="margin-top:6px;"></div></div>`;
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
              return;
            }
            const needsConfirmation = button.getAttribute('data-requires-confirmation') === 'true';
            const confirmed = needsConfirmation
              ? window.confirm('This is a high-risk review action. Submit only after explicit human confirmation.')
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

)HTML"
R"HTML(
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
      const backdrop = document.getElementById('item-modal-backdrop');
      backdrop.classList.add('open');
      backdrop.setAttribute('aria-hidden', 'false');
    }

    function closeModal() {
      const backdrop = document.getElementById('item-modal-backdrop');
      backdrop.classList.remove('open');
      backdrop.setAttribute('aria-hidden', 'true');
    }

    async function openItemModal(itemId, product = '') {
      const productScope = product || (selectedProductValues().length === 1 ? selectedProductValues()[0] : 'all');
      openModal(itemId, '<div class="muted">Loading item detail...</div>');
      try {
        await window.KobUi.fetchPartial(
          `/partials/item/${encodeURIComponent(itemId)}?product=${encodeURIComponent(productScope)}`,
          { target: '#item-modal-body' },
        );
        hydrateMarkdown('#item-modal-body');
        bindItemLinksWithin('#item-modal-body', productScope);
        const titleSource = document.querySelector('#item-modal-body [data-item-title]');
        document.getElementById('item-modal-title').textContent =
          titleSource?.getAttribute('data-item-title') || itemId;
      } catch (error) {
        openModal(itemId, `<div class="muted">Unable to load item detail: ${esc(describeRefreshError(error))}</div>`);
      }
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

    async function loadTree(signal) {
      const result = await getJson(`/api/tree?${queryString()}`, { signal });
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

    async function loadKanban(signal) {
      const result = await getJson(`/api/kanban?${queryString()}`, { signal });
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

    async function loadContext(signal) {
      const result = await getJson(`/api/items?${queryString()}`, { signal });
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

    async function loadReview(signal) {
      const viewsResult = await getJson('/api/review/saved-views', { signal });
      const views = viewsResult?.data?.views || [];
      document.getElementById('saved-views').innerHTML = views.map((view) =>
        `<button class="btn" data-saved-view="${escAttr(view.id)}">${esc(view.title)}</button>`
      ).join('');

      const inboxResult = await getJson(`/api/review/inbox?${queryString()}`, { signal });
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

    async function loadGraph(signal) {
      const result = await getJson(`/api/review/graph?${queryString()}`, { signal });
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

    async function loadRuns(signal) {
      const result = await getJson(`/api/review/runs?${queryString()}`, { signal });
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
      updateUrlState();
      syncSelectableItems();
    }

)HTML"
R"HTML(
    function scheduleRefresh(delayMs = 120) {
      updateUrlState();
      if (state.refreshTimer) {
        clearTimeout(state.refreshTimer);
      }
      const runRefresh = () => {
        state.refreshTimer = null;
        refreshAll();
      };
      state.refreshTimer = window.KobUi?.debounce
          ? window.KobUi.debounce('refresh-all', runRefresh, delayMs)
          : setTimeout(runRefresh, delayMs);
    }

    async function refreshAll() {
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
        state.refreshAbort.abort();
      }

      const refreshId = ++state.refreshSeq;
      const controller = new AbortController();
      state.refreshAbort = controller;
      state.refreshCancelRequested = false;
      const signal = controller.signal;
      const startedAt = Date.now();
      const scopeLabel = productScopeLabel();
      const steps = [
        ['tree', () => loadTree(signal)],
        ['kanban', () => loadKanban(signal)],
        ['context', () => loadContext(signal)],
        ['review', () => loadReview(signal)],
        ['graph', () => loadGraph(signal)],
        ['runs', () => loadRuns(signal)],
      ];
      let completed = 0;
      const total = steps.length;

      setStatus(`Loading ${scopeLabel}...`);
      setBusy(true);
      updateBusy(`Loading ${scopeLabel}`, `Starting refresh, 0/${total} complete`);
      if (state.refreshTickTimer) {
        clearInterval(state.refreshTickTimer);
      }
      state.refreshTickTimer = setInterval(() => {
        if (refreshId === state.refreshSeq) {
          updateBusy(
              `Loading ${scopeLabel}`,
              `${completed}/${total} complete, elapsed ${formatElapsed(startedAt)}`);
        }
      }, 1000);

      const results = await Promise.allSettled(steps.map(async ([label, run]) => {
        if (refreshId === state.refreshSeq) {
          updateBusy(
              `Loading ${scopeLabel}`,
              `Loading ${label}, ${completed}/${total} complete, elapsed ${formatElapsed(startedAt)}`);
        }
        await run();
        completed += 1;
        if (refreshId === state.refreshSeq) {
          updateBusy(
              `Loading ${scopeLabel}`,
              `Loaded ${label}, ${completed}/${total} complete, elapsed ${formatElapsed(startedAt)}`);
        }
      }));

      if (refreshId !== state.refreshSeq) {
        return;
      }
      state.refreshAbort = null;
      setBusy(false);

      if (signal.aborted && state.refreshCancelRequested) {
        state.lastRefreshDiagnostic =
            makeRefreshDiagnostic('canceled', 'Refresh canceled by user', startedAt, []);
        updateBusy('Refresh canceled', `Canceled after ${formatElapsed(startedAt)}`);
        setBusy(false, true);
        setStatus(`Refresh canceled after ${formatElapsed(startedAt)}`);
        state.refreshCancelRequested = false;
        return;
      }

      const failures = results.filter((result) =>
          result.status === 'rejected' && result.reason?.name !== 'AbortError');
      if (failures.length) {
        const first = describeRefreshError(failures[0].reason);
        state.lastRefreshDiagnostic =
            makeRefreshDiagnostic('failed', first, startedAt, failures);
        updateBusy('Refresh failed', `${first}; elapsed ${formatElapsed(startedAt)}`);
        setBusy(false, true);
        setStatus(`Refresh failed: ${first}`, 'error');
        return;
      }

      state.lastRefreshDiagnostic =
          makeRefreshDiagnostic('loaded', `Loaded ${scopeLabel}`, startedAt, []);
      setStatus(`Loaded ${scopeLabel} in ${formatElapsed(startedAt)}`);
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
      scheduleRefresh(0);
    });

    document.getElementById('search').addEventListener('input', async (e) => {
      state.q = e.target.value.trim();
      state.treeTouched = false;
      state.treeOpen.clear();
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
      scheduleRefresh(120);
    });

    document.getElementById('limit').addEventListener('change', async (event) => {
      const parsed = Number.parseInt(event.target.value, 10);
      state.limit = Number.isFinite(parsed) ? Math.max(1, Math.min(parsed, 1000)) : 200;
      event.target.value = String(state.limit);
      scheduleRefresh(0);
    });

    document.getElementById('refresh').addEventListener('click', async () => {
      const products = selectedProductValues();
      const refreshScope = products.length === 1 ? products[0] : 'all';
      const startedAt = Date.now();
      setStatus(`Refreshing ${productScopeLabel()}...`);
      setBusy(true);
      updateBusy(`Refreshing ${productScopeLabel()}`, 'Invalidating cached backlog data');
      try {
        await getJson(`/api/refresh?product=${encodeURIComponent(refreshScope)}`);
        setStatus(`Refresh requested in ${formatElapsed(startedAt)}`);
        await refreshAll();
      } catch (error) {
        const detail = describeRefreshError(error);
        state.lastRefreshDiagnostic =
            makeRefreshDiagnostic('failed', detail, startedAt, [{ status: 'rejected', reason: error }]);
        updateBusy('Refresh failed', `${detail}; elapsed ${formatElapsed(startedAt)}`);
        setBusy(false);
        setBusy(false, true);
        setStatus(`Refresh failed: ${detail}`, 'error');
      }
    });

    document.getElementById('busy-cancel').addEventListener('click', () => {
      if (!state.refreshAbort) {
        return;
      }
      state.refreshCancelRequested = true;
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
      state.workspaces = loadSavedWorkspaces();
      renderWorkspaceList();
      await loadWorkspaceInfo();
      document.getElementById('workspace-input').value = state.workspace || '';
      document.getElementById('search').value = state.q;
      document.getElementById('limit').value = String(state.limit);
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
      "/assets/kob-ui.js",
      [](const drogon::HttpRequestPtr&,
         std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto response = drogon::HttpResponse::newHttpResponse();
        response->setStatusCode(drogon::k200OK);
        response->setContentTypeString("application/javascript; charset=utf-8");
        response->setBody(kKobUiJs);
        callback(response);
      },
      {drogon::Get});

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
