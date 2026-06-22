# kano_backlog_webview (C++ Drogon)

Local-only backlog visualization service.

The partial UI boundary is documented in
[`docs/design/backboard-partial-ui-boundary.md`](../../../../../docs/design/backboard-partial-ui-boundary.md).

## Scope (MVP)

- Read canonical markdown backlog files under `_kano/backlog/products/*/items/`
- Default to an all-products view, with explicit single-product and multi-product filters.
- Expose item metadata needed for review scans: product, type, state, source kind, UID, and topic membership when a topic manifest references the item.
- Read-only APIs:
  - `GET /healthz`
  - `GET /api/products`
  - `GET /api/items?product=all|<name>[&products=a,b][&q=...][&state=Ready,Doing][&type=task,feature][&limit=200][&offset=0]`
  - `GET /api/items/<id>?product=all|<name>[&products=a,b]`
  - `GET /api/tree?product=all|<name>[&products=a,b][&q=...][&state=...][&type=...][&limit=...]`
  - `GET /api/kanban?product=all|<name>[&products=a,b][&q=...][&state=...][&type=...][&limit=...]`
  - `GET /api/refresh[?product=all|<name>][&products=a,b]`
- Server-rendered partials:
  - `GET /partials/tree?...`
  - `GET /partials/kanban?...`
  - `GET /partials/review?...`
  - `GET /partials/context?...`
  - `GET /partials/filters?...`
  - `GET /partials/item/<id>?product=all|<name>`
- First-party UI runtime:
  - `GET /assets/kob-ui.js`
- UI: product/state/type/search filters + context summary + tree + kanban at `/`

`kob-ui.js` is intentionally small and first-party. It owns partial fetch/swap,
delegated partial links, filter debounce support, URL query-state helpers, and
bounded error rendering without npm or a frontend build step.

## Security Defaults

- Binds to `127.0.0.1` only
- Product path constrained to configured products root
- No mutation endpoints

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
