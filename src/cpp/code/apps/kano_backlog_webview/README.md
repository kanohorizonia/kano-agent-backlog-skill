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
  - `GET /api/review/context-recovery?area=...&product=all|<name>[&products=a,b][&q=...][&state=...][&type=...]`
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
  - `GET /partials/roadmap?...`
  - `GET /partials/decision-radar?...`
  - `GET /partials/context?...`
  - `GET /partials/filters?...`
  - `GET /partials/item/<id>?product=all|<name>`
- First-party UI runtime:
  - `GET /assets/kob-ui.js`
- UI: Backboard Review Inbox, product map, flow, context, dependencies, agent
  runs, and command preview at `/`

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
