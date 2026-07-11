#pragma once

#include <string_view>

namespace kano::backlog::webview::assets {

inline constexpr std::string_view kBackboardCss = R"CSS(
    :root { --kob-accent: #1f4fa3; --kob-accent-soft: #f2f6ff; --kob-accent-border: #9fb5de; --kob-border: #cfd9ea; --kob-border-strong: #9eb3d7; --kob-surface: #fcfdff; --kob-surface-strong: #ffffff; --kob-shadow: rgba(30, 55, 95, 0.12); }
    body { font-family: "Segoe UI", "Yu Gothic UI", Meiryo, "Microsoft JhengHei UI", "Microsoft YaHei UI", "Malgun Gothic", "PingFang SC", "Hiragino Sans", sans-serif; margin: 0; padding: 16px; background: #f7f8fa; color: #1a1f2e; }
    .app-shell { display: grid; grid-template-columns: 280px minmax(0, 1fr); gap: 12px; align-items: start; }
    .app-shell > main { min-width: 0; }
    .sidebar { position: sticky; top: 16px; }
    .workspace-list { display: flex; flex-direction: column; gap: 6px; margin-top: 8px; max-height: 45vh; overflow: auto; }
    .workspace-row { display: grid; grid-template-columns: minmax(0,1fr) auto auto; gap: 6px; align-items: center; }
    .workspace-item { text-align: left; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
    .workspace-item.active { background: #1f4fa3; color: #fff; border-color: #1f4fa3; }
    .workspace-meta { font-size: 11px; color: #70809f; margin-top: 2px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
    .icon-btn { border: 1px solid #cfd9ea; background: #fff; border-radius: 6px; padding: 4px 6px; cursor: pointer; }
    .icon-btn:hover { background: #f2f6ff; }
    .row { display: flex; gap: 12px; align-items: center; flex-wrap: wrap; margin-bottom: 12px; }
    .panel { background: #fff; border: 1px solid #dde3f0; border-radius: 10px; padding: 12px; margin-bottom: 12px; }
    .tabs { display: flex; gap: 8px; flex-wrap: wrap; }
    .tab-btn { border: 1px solid #cfd9ea; background: #fff; border-radius: 8px; padding: 6px 12px; cursor: pointer; }
    .tab-btn.active { background: #1f4fa3; color: #fff; border-color: #1f4fa3; }
    .page { display: none; position: relative; }
    .page.active { display: block; }
    .page.is-refreshing::before,
    .page.is-stale::before { content: attr(data-refresh-note); display: inline-block; margin: 0 0 8px 0; border: 1px solid #b9c9e8; border-radius: 6px; padding: 4px 8px; font-size: 12px; font-weight: 600; color: #1d3158; background: #f5f8ff; }
    .page.is-stale::before { border-color: #d5b15d; color: #6a4c0f; background: #fff9e8; }
    .kanban { display: grid; grid-template-columns: repeat(5, minmax(180px, 1fr)); gap: 10px; }
    .lane { background: #fff; border: 1px solid #dde3f0; border-radius: 10px; padding: 8px; min-height: 140px; }
    .card { position: relative; border: 1px solid #cfd9ea; border-radius: 8px; padding: 8px; margin-bottom: 8px; background: #fcfdff; }
    .card[data-selectable-item] { cursor: pointer; transition: border-color 0.16s ease, box-shadow 0.16s ease, transform 0.16s ease; }
    .card[data-selectable-item]:hover { border-color: var(--kob-accent-border); box-shadow: 0 4px 10px var(--kob-shadow); }
    .card[data-selectable-item]:focus-visible { outline: 3px solid var(--kob-accent-border); outline-offset: 2px; }
    .card.is-selected { border-color: var(--kob-accent); border-left-width: 6px; padding-left: 10px; background: var(--kob-surface-strong); box-shadow: 0 0 0 1px var(--kob-accent), 0 8px 18px var(--kob-shadow); transform: translateX(2px); }
    .review-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 10px; }
    .review-lane { border: 1px solid #d8e1f0; border-radius: 8px; padding: 8px; background: #fff; min-height: 120px; }
    .lane-items, .review-lane-items, .context-items, .command-items, .handoff-readiness-list { margin-top: 8px; }
    .handoff-readiness-list { display: grid; gap: 10px; }
    .handoff-preview-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 10px; margin: 10px 0; }
    .handoff-row { display: grid; gap: 8px; }
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
    .tree .node-line { display: inline-flex; gap: 6px; align-items: center; flex-wrap: wrap; }
    .tree .tree-nav { display: inline-flex; gap: 4px; align-items: center; flex-wrap: wrap; margin-left: 8px; }
    .tree .leaf-spacer { display: inline-block; width: 12px; }
    .btn { border: 1px solid #cfd9ea; background: #fff; border-radius: 6px; padding: 4px 10px; cursor: pointer; color: #1a1f2e; text-decoration: none; display: inline-flex; align-items: center; justify-content: center; }
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
    .graph-canvas { position: relative; width: 100%; height: clamp(360px, 58vh, 720px); overflow: hidden; border: 1px solid #dbe4f2; border-radius: 10px; background: linear-gradient(180deg, #fbfcff 0%, #f5f8ff 100%); margin-bottom: 12px; outline: none; touch-action: none; cursor: grab; }
    .graph-canvas.is-panning { cursor: grabbing; }
    .graph-canvas:focus-visible { box-shadow: 0 0 0 3px rgba(159, 181, 222, 0.85); }
    .graph-svg { width: 100%; height: 100%; min-width: 0; display: block; }
    .graph-edge { stroke: #7a879d; stroke-width: 1.5; fill: none; }
    .graph-edge.blocks,.graph-edge.blocked_by { stroke: #b44646; stroke-width: 2; }
    .graph-edge.parent { stroke: #4f6fa9; }
    .graph-edge.topic-membership { stroke: #498264; stroke-dasharray: 5 4; }
    .graph-edge.relates { stroke: #7d6aa6; stroke-dasharray: 3 4; }
    .graph-edge.is-faded, .graph-edge-label.is-faded { opacity: 0.3; }
    .graph-node rect { fill: #fff; stroke: #b9c7de; rx: 8; }
    .graph-node.topic rect { fill: #eef8f2; stroke: #7eb58d; }
    .graph-node.missing rect { fill: #fff4f4; stroke: #d48b8b; stroke-dasharray: 5 4; }
    .graph-node.dependency rect { stroke: #c65f5f; }
    .graph-node.is-included-neighborhood:not(.is-focus-root) rect { stroke: var(--kob-border-strong); }
    .graph-node.is-focus-root rect { fill: var(--kob-accent-soft); stroke: var(--kob-accent); stroke-width: 2.5; }
    .graph-node.is-faded { opacity: 0.34; }
    .graph-node.is-rerootable { cursor: pointer; }
    .graph-node.is-rerootable:hover rect { stroke: var(--kob-accent); stroke-width: 2; filter: drop-shadow(0 4px 10px var(--kob-shadow)); }
    .graph-node.is-rerootable:focus-visible rect { stroke: var(--kob-accent); stroke-width: 2.5; filter: drop-shadow(0 4px 10px var(--kob-shadow)); }
    .graph-label { font-size: 12px; fill: #1a1f2e; }
    .graph-meta { font-size: 10px; fill: #65738b; }
    .graph-edge-label { font-size: 10px; fill: #47536a; }
    .graph-diagnostics { display: grid; gap: 6px; margin-bottom: 12px; }
    .graph-diagnostic-pills { display: flex; gap: 6px; flex-wrap: wrap; margin-top: 8px; }
    .graph-page-head { display: flex; gap: 12px; align-items: flex-start; justify-content: space-between; flex-wrap: wrap; margin-bottom: 12px; }
    .graph-page-title { display: grid; gap: 4px; }
    .graph-page-title h3 { margin: 0; }
    .graph-empty-state { margin-top: 4px; }
    .graph-toolbar { display: grid; gap: 8px; margin-bottom: 12px; }
    .graph-toolbar-row { display: flex; gap: 12px; align-items: flex-end; flex-wrap: wrap; }
    .graph-toolbar-row-secondary { align-items: center; justify-content: space-between; }
    .graph-mode-field { display: flex; flex-direction: column; gap: 8px; min-width: min(320px, 100%); }
    .graph-toolbar-field { display: flex; flex-direction: column; gap: 8px; min-width: 160px; }
    .graph-mode-select, .graph-depth-input { min-width: 140px; max-width: 100%; border: 1px solid #cfd9ea; background: #fff; border-radius: 8px; padding: 6px 10px; color: #1a1f2e; }
    .graph-depth-input { width: 96px; }
    .graph-toolbar-actions { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }
    .graph-toolbar-actions .btn[disabled] { opacity: 0.65; cursor: default; }
    .graph-viewport-actions { row-gap: 8px; }
    .graph-viewport-btn { min-height: 32px; }
    .graph-viewport-help { flex: 1; min-width: min(320px, 100%); line-height: 1.45; }
    .graph-mode-help { line-height: 1.45; }
    .graph-scope-help { line-height: 1.45; }
    .blocker-chain { display: grid; gap: 8px; margin-bottom: 12px; min-width: 0; }
    .blocker-chain-header { display: grid; gap: 4px; min-width: 0; }
    .blocker-chain-header h4, .blocker-chain-section h5 { margin: 0; }
    .blocker-chain-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(220px, 100%), 1fr)); gap: 8px; min-width: 0; align-items: start; }
    .blocker-chain-section { display: grid; align-content: start; align-self: start; gap: 8px; min-width: 0; }
    .blocker-chain-list { display: grid; align-content: start; gap: 8px; }
    .blocker-chain-item { display: grid; gap: 8px; margin: 0; min-width: 0; }
    .blocker-chain-facts { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(120px, 100%), 1fr)); gap: 8px; }
    .blocker-chain-header, .blocker-chain-section, .blocker-chain-item { overflow-wrap: break-word; word-break: normal; }
    .blocker-chain-facts { min-width: 0; }
    .blocker-chain-path, .blocker-chain code, .blocker-chain-id { overflow-wrap: anywhere; word-break: break-word; }
    .blocker-chain-jump { justify-self: start; }
    .blocker-chain-jump:focus-visible { outline: 3px solid var(--kob-accent-border); outline-offset: 2px; }
    @media (max-width: 720px) {
      body { padding: 12px; }
      .app-shell { grid-template-columns: minmax(0, 1fr); }
      .sidebar { position: static; top: auto; }
    }
)CSS";

inline constexpr std::string_view BackboardCss() noexcept {
  return kBackboardCss;
}

}  // namespace kano::backlog::webview::assets
