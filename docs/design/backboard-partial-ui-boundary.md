# Backboard Partial UI Boundary

Status: accepted as the decision boundary for Backboard partial UI work.

## Context

Backboard is the local KOB webview used for human review of backlog state. The
desired modernization path is Drogon-native: C++ renders the page and owns the
backlog query model, while a small amount of first-party JavaScript makes the UI
responsive.

The project should avoid adopting a full frontend application stack for this
surface. In particular, Backboard should not require npm, Vite, React, or a
client-side single-page application baseline.

htmx can be a useful helper for server-rendered partial updates, but it must stay
bounded. The durable architecture is Drogon-rendered HTML partials plus
first-party request lifecycle behavior.

## Decision

Backboard may use vendored htmx core as a small progressive-enhancement helper
for server-rendered partial updates.

This is a permission boundary, not a requirement. A first-party partial update
runtime is still acceptable when it keeps the implementation smaller or clearer.
If htmx is introduced, it must be checked into this repository as a static asset
and served by the native webview. It must not be fetched from a CDN at runtime.

## Allowed htmx Usage

- Vendored htmx core only.
- Static asset served by the native webview process.
- Server-rendered partial HTML responses from Drogon routes.
- Narrow attributes for ordinary partial refresh behavior:
  - `hx-get`
  - `hx-post` only for future explicitly reviewed mutation flows
  - `hx-target`
  - `hx-swap`
  - `hx-trigger`
  - `hx-include`
  - `hx-push-url` or `hx-replace-url` for URL state when useful
- Progressive enhancement where the baseline route remains inspectable.

## Disallowed htmx Usage

- CDN loading.
- npm, Vite, bundlers, or generated frontend build artifacts.
- htmx extensions unless separately reviewed and documented.
- Treating htmx as the application architecture.
- Moving backlog query logic or review classification into browser code.
- Replacing first-party request diagnostics with implicit htmx behavior.
- Introducing client-side routing as the source of truth.
- Adding mutation endpoints without a separate backlog mutation design and Done
  gate.

## First-Party Runtime Responsibilities

KOB-specific lifecycle behavior remains first-party JavaScript even if htmx is
used for partial swaps:

- global loading status
- request timeout handling
- retry controls
- cancel controls
- diagnostics capture and copy
- empty-state rendering
- stale response protection
- drawer, tab, and collapse behavior where Backboard needs custom semantics
- feature-specific accessibility fixes that htmx does not own

This prevents the UI from hiding failure states behind a convenience library.

## Drogon Route Boundary

Partial routes should reuse the same service/query code as the JSON APIs. The
route layer may format HTML fragments, but it should not fork item discovery,
state filtering, review queue classification, or evidence-gap logic.

Recommended route shape:

- `/partials/review`
- `/partials/tree`
- `/partials/item/<id>`
- `/partials/filters`
- `/partials/context`

JSON APIs remain stable for tests, automation, and future non-browser clients.

## Asset Boundary

If htmx is vendored, keep the asset local and explicit:

- source location: `src/cpp/code/apps/kano_backlog_webview/assets/`
- runtime URL: `/assets/htmx.min.js`
- no transitive package install step
- no minification or bundling step required during the normal build
- update notes must identify the upstream htmx version and license

The same local/embedded asset rule applies to any browser library used by
Backboard.

For the broader DOM-first versus optional visualization-island split, see
`webview-technology-boundary.md`.

## Release And Test Expectations

- `pixi run quick-test` remains green.
- The webview release build succeeds without npm or network access.
- Browser console output must not rely on CDN resources.
- Partial route smoke coverage should prove useful failure states, not only
  happy-path HTML.
