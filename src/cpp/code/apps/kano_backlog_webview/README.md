# kano_backlog_webview (C++ Drogon)

Local-only backlog visualization service for Backboard.

Backboard is the product-facing local review UI. KOB Webview is the technical
name for this native service, its binary, routes, and Docker wrapper.

Design contracts:

- [`docs/design/backboard-information-architecture.md`](../../../../../docs/design/backboard-information-architecture.md)
- [`docs/design/backboard-review-inbox-model.md`](../../../../../docs/design/backboard-review-inbox-model.md)
- [`docs/design/backboard-partial-ui-boundary.md`](../../../../../docs/design/backboard-partial-ui-boundary.md)
- [`docs/design/webview-technology-boundary.md`](../../../../../docs/design/webview-technology-boundary.md)

## Scope (MVP)

- Read canonical markdown backlog files under `_kano/backlog/products/*/items/`
- Default to an all-products view, with explicit single-product and multi-product filters.
- Expose item metadata needed for review scans: product, type, state, source kind, UID, and topic membership when a topic manifest references the item.
- Read-mostly APIs:
  - `GET /healthz`
  - `GET /api/products`
  - `GET /api/items?product=all|<name>[&products=a,b][&q=...][&state=Ready,Doing][&type=task,feature][&limit=200][&offset=0]`
  - `GET /api/items/<id>?product=all|<name>[&products=a,b]`
  - `GET /api/tree?product=all|<name>[&products=a,b][&q=...][&state=...][&type=...][&limit=...]`
  - `GET /api/kanban?product=all|<name>[&products=a,b][&q=...][&state=...][&type=...][&limit=...]`
  - `GET /api/refresh[?product=all|<name>][&products=a,b]`
  - `GET /api/review/done-detector?product=all|<name>[&products=a,b][&q=...][&state=...][&type=...]`
  - `GET /api/review/evidence-quality?product=all|<name>[&products=a,b][&q=...][&state=...][&type=...]`
  - `GET /api/review/handoff-readiness?product=all|<name>[&products=a,b][&q=...][&state=...][&type=...]`
  - `GET /api/review/context-recovery?area=...&product=all|<name>[&products=a,b][&q=...][&state=...][&type=...]`
  - `GET /api/review/graph?product=all|<name>[&products=a,b][&item=<id>][&root_product=<name>][&mode=dependency|structure|cycles|related|product_memory][&graph_isolation=fade|hide][&max_depth=2][&max_children_per_node=25][&max_total_nodes=80|&node_limit=80][&max_total_edges=120|&edge_limit=120]`
  - `GET /api/review/feature-evolution?product=<name>&feature_id=<id>`
  - `GET /api/review/roadmap?product=all|<name>[&products=a,b]`
  - `GET /api/review/decision-radar?product=all|<name>[&products=a,b]`
  - `POST /api/review/decision/draft`
  - `POST /api/review/decision/draft/discard`
  - `POST /api/review/decision/submit`
- Server-rendered partials:
  - `GET /partials/tree?...`
  - `GET /partials/kanban?...`
  - `GET /partials/review?...`
  - `GET /partials/handoff-readiness?...`
  - `GET /partials/roadmap?...`
  - `GET /partials/decision-radar?...`
  - `GET /partials/context?...`
  - `GET /partials/filters?...`
  - `GET /partials/item/<id>?product=all|<name>`
- First-party UI runtime:
  - `GET /assets/kob-ui.js`
  - `GET /graph?tab=graph[&product=<name>][&item=<id>][&root_product=<name>][&mode=dependency][&graph_isolation=fade|hide][&max_depth=2][&max_children_per_node=25][&max_total_nodes=80][&max_total_edges=120]`
- UI: Backboard Review Inbox, Agent Handoff Readiness, product map, flow,
  context, dependencies, agent runs, and command preview at `/`

The full-page Dependencies canvas is item-rooted and bounded by query caps. The
default graph page shell keeps the mode selector/help visible, but when no
`item` root is present it renders a scaffold-only prompt instead of fetching or
rendering a global all-node graph by default.

When an item root exists, the normal Backboard UI sends a graph-only bounded all-product scan
(`product=all`, `limit=1000`, `offset=0`) only to resolve qualified cross-product dependencies.
It does not forward list filters such as search, state, type, topic, selected products, or list
offsets. The response remains item-rooted and bounded by depth, child, node, and edge caps, and
does not render a global graph.

The graph toolbar also keeps the root-focused isolation contract local and
bounded: reviewers can change `max_depth`, switch between fade or hide for
unrelated nodes, click a graph node to re-root the bounded graph query, and use
Reset scope to restore the incoming root (or clear back to the scaffold when no
incoming root exists). Hidden or faded nodes and edges always stay diagnosable
through the graph summary and diagnostics cards; Backboard does not silently
drop unrelated blockers from review context.

The same bounded item-rooted graph data now has client-side viewport controls:
zoom out, zoom in, fit all, fit focused subgraph, and reset view. The SVG graph
canvas supports pointer drag panning, mouse-wheel zoom centered on the pointer,
button zoom controls, and focused keyboard shortcuts (`+`/`=`, `-`, `0`, and
arrow-key pan) without changing the bounded query itself. Reset view only
changes the client-side pan/zoom state; it does not change root scope, depth,
isolation mode, URL query state, or fetched graph data.

Dependency mode is dependency-only by default: its bounded item-rooted response
may include a native `blocker_chain` object with `root_item`,
`edge_direction_note`, `upstream_blockers`, `downstream_blocked_items`,
`root_blockers`, `jump_targets`, `ranking_basis`, branch counts, and bounded
summary counts/caps. Backboard renders Root blockers, Upstream blockers,
Downstream impact, and Branch evidence before the SVG canvas when that object is
present. Root ordering is explainable bounded review order: visible bounded
impact, shorter path, then stable ID. It is not business priority.

In explicit dependency mode (`mode=dependency`), the graph contains only
`blocks` and `blocked_by` edges and emits `blocker_chain` when the requested
root is unambiguous. An omitted mode preserves legacy broad context for the
internal Focus Graph summary. If duplicate products share a bare root ID and
`root_product` is omitted, the response emits `graph_root_ambiguous` and no
`blocker_chain`; callers provide `root_product=<name>` to disambiguate that
item-rooted graph request.

Branch truncation is bounded and diagnosable: parallel and truncated branch
counts, hidden node and edge counts, invalid references, visible dependency edge
counts, and the returned query caps remain visible rather than being inferred.
Jump actions only re-root the existing bounded graph query; they do not request
a global graph. Hierarchy, relates, topic, and product-memory views require
explicit modes (`structure`, `related`, or `product_memory`) rather than being
mixed into dependency mode. Backboard has no global graph or saved query support.
It has no saved queries, global graph, or framework scope.

`kob-ui.js` is intentionally small and first-party. It owns partial fetch/swap,
delegated partial links, filter debounce support, URL query-state helpers, and
bounded error rendering without npm or a frontend build step.

## Embedded asset layout

The Backboard root shell remains embedded in the native binary to avoid runtime
static-file lookup and Docker packaging drift.

- `assets/index_html.hpp` composes the `/` HTML shell and stitches the embedded
  CSS and page app JavaScript into one response body.
- `assets/backboard_css.hpp` holds the first-party CSS used by the root shell.
- `assets/backboard_app_js.hpp` holds the inline page application JavaScript for
  the root shell.
- `assets/kob_ui_js.hpp` holds the first-party `/assets/kob-ui.js` runtime.

## Security Defaults

- Binds to `127.0.0.1` only
- Product path constrained to configured products root
- Mutation endpoints are limited to local KOB review-decision drafts/submissions;
  confirmed target-state actions call existing KOB transition policy and never
  start agents or dispatch work.

## Build (Linux)

```bash
cmake --preset linux-ninja-gcc
cmake --build --preset build-linux
./build/linux-ninja-gcc/apps/kano_backlog_webview/kano_backlog_webview
```

or

```bash
./scripts/build/build_linux_gcc.sh
```

## Build (Windows, Ninja + MSVC)

Use the C++ convention skill guidance to pin MSVC toolset before CMake if needed.

```bat
cmake --preset windows-ninja-msvc
cmake --build --preset build-windows
build\windows-ninja-msvc\apps\kano_backlog_webview\Debug\kano_backlog_webview.exe
```

or

```bat
scripts\build\build_win_ninja_msvc.bat
```

## Runtime Configuration

- Backlog products root:
  - default: `_kano/backlog/products`
  - env: `KANO_BACKLOG_PRODUCTS_ROOT`
  - arg: `--backlog-root <path>`
- Port:
  - default: `8787`
  - env: `KANO_WEBVIEW_PORT`
  - arg: `--port <number>`
- Bind host:
  - default: `127.0.0.1`
  - env: `KANO_WEBVIEW_HOST`
  - arg: `--host <address>`

## Docker Compose

From the repository root:

```bash
pixi run webview-docker
```

The Docker path binds the webview to `0.0.0.0` inside the container, publishes
it on host port `8799`, mounts `../_kano/backlog` read-only at
`/workspace/_kano/backlog`, and uses Docker's `unless-stopped` restart policy.
