# KOB Webview Technology Boundary

Status: guideline for the default KOB Webview technology split.

## Goal

This document explains the default boundary between HTML5 and DOM structure,
small first-party JavaScript behavior, and optional heavier visualization
technology inside Backboard and the KOB Webview runtime.

The intent is clarity, not a blanket ban on experiments. KOB should keep the
current no-npm, no-Vite, no-React baseline unless a separate reviewed design
contract says otherwise.

This guideline also aligns with the KOB-FTR-0016 modernization direction: a
Drogon-native path with server-rendered HTML templates, reusable C++ and HTML
partials, modern CSS variables, small vanilla JavaScript, optional vendored
htmx only when it stays minimal, and JSON APIs that can support richer future
frontend work without changing the baseline.

## Terms In KOB Language

### HTML5 and DOM

In KOB terms, HTML5 and the DOM are the document surface that Backboard uses to
present backlog state as inspectable UI. This includes headings, tables, forms,
buttons, details panels, links, status badges, inline diagnostics, and other
ordinary review controls.

DOM-first means the page is readable as structured document content. The
reviewer should be able to inspect the state, understand the current filter or
decision path, and fall back to text and links without needing a heavy client
runtime.

### JavaScript

JavaScript is the small client behavior layer that improves responsiveness. In
KOB, that means progressive enhancement such as partial refresh, filter debounce,
drawer or tab behavior, loading and timeout feedback, retry controls, and safe
request lifecycle handling.

JavaScript is not the primary source of backlog truth. Backlog query logic,
review classification, transition policy, and file authority stay in native KOB
services and repository source artifacts.

### WASM

WASM is an optional browser execution target for isolated, computation-heavy,
non-critical view logic. In KOB, that may fit specialized visual islands where
layout or rendering work is expensive enough that plain DOM updates would be
awkward or slow.

WASM is not the default application layer for ordinary Backboard review work.
It should not become a second backlog runtime, a hidden policy engine, or a path
around KOB and KOA controls.

### Canvas and WebGL

Canvas and WebGL are rendering surfaces for custom visuals. In KOB, they are
best treated as optional presentation islands for cases where a plain document
view is still available and remains the authoritative fallback.

Canvas may fit 2D graph rendering, dense timelines, or heavy layout previews.
WebGL may fit more advanced visual scenes only when those scenes remain optional
and non-critical to review completion.

## Default Architecture

Backboard should stay DOM-first by default.

The ordinary product experience should come from:

- Drogon-rendered HTML pages and partials
- reusable native-side view composition
- modern CSS variables and simple layout primitives
- small first-party JavaScript
- optional minimal vendored htmx only where it helps partial refresh behavior

That default keeps the webview local, inspectable, and compatible with the
existing no-npm, no-Vite, no-React baseline.

## DOM-First Surfaces

The following KOB surfaces should default to HTML and DOM rendering:

- Review Inbox
- item detail
- backlog tables
- filters and search controls
- forms
- review decisions and review actions
- release views
- taxonomy views
- evidence views

These surfaces are document-heavy, review-heavy, and already map well to native
HTML controls. They benefit from inspectable markup, simple keyboard behavior,
copyable text, and clear fallback behavior.

### Example, item detail stays DOM-first

An item detail screen should render fields such as title, id, state, owner,
Ready sections, evidence summaries, links, and raw markdown access as normal DOM
content.

If the page later adds a small dependency mini-map or evidence sparkline, that
visual can remain secondary. The detail contract still lives in readable HTML.

### Example, review table stays DOM-first

A review inbox table should stay a normal HTML table or list with visible lane,
reason code, confidence, suggested human decision, and links to detail views.

Sorting, filtering, partial refresh, and draft review actions may use small
JavaScript behavior, but the base surface remains an inspectable document.

## Optional Visualization Islands

Some visual work may justify a bounded island instead of plain DOM rendering.
These are allowed as optional future directions, not current requirements:

- dependency graph visualization
- heavy graph layout
- Ark, robot, or work-order visualization
- non-critical visual dashboards

These islands should exist only where they add value beyond ordinary review text
and where a plain DOM or text fallback still covers the core task.

### Example, dependency graph island is allowed

A dependency graph may use SVG, Canvas, or a WASM-assisted layout island when
the input is bounded and the reviewer can still fall back to a text list or DOM
edge table.

### Example, Ark robot scene is optional

An Ark robot or work-order scene may use Canvas or WebGL as an optional visual
surface for exploration or demonstration. It should not be required to inspect
review state, submit a review decision, or understand core backlog evidence.

## Island Rules

Any Canvas, WebGL, or WASM island should follow these default rules:

- bounded data input only
- no arbitrary filesystem access
- no raw shell access
- no secret access
- no mutation start unless explicitly routed through KOB and KOA policy
- graceful fallback to a DOM or text view

More specifically:

1. Input data should come from explicit KOB JSON or partial-route responses with
   clear size and purpose.
2. The island should not read arbitrary repo files directly from the browser.
3. The island should not proxy shell execution, agent dispatch, or other raw
   workstation controls.
4. Any mutation path must stay explicit, auditable, and policy-checked through
   the existing KOB and KOA boundaries.
5. If the island fails to load, times out, or is disabled, the user should still
   get a useful DOM or text representation.

## Decision Guidance

When choosing between DOM-first rendering and an island, use this default test:

- If the surface is mostly text, forms, filters, or review actions, keep it in
  DOM.
- If the surface needs dense layout or graphics but is still secondary to the
  review task, an island may be reasonable.
- If the surface would hide review-critical state behind a graphics runtime,
  keep it DOM-first.

The burden of proof is on the heavier runtime. DOM-first is the default because
it matches KOB's evidence-first, repository-first, and local review goals.

## Relationship To Existing Contracts

- `backboard-information-architecture.md` defines the user-facing review
  surfaces and navigation priorities.
- `backboard-review-inbox-model.md` defines the review inbox data and decision
  contract.
- `backboard-partial-ui-boundary.md` defines the partial-update baseline and the
  no-npm, no-Vite, no-React implementation direction.

This document adds one more layer: which surfaces should remain plain document
UI by default, and where optional heavier visualization technology can fit
without taking over the product architecture.

## Future Modernization Note

KOB-FTR-0016 is the right place for future Drogon-native modernization work that
improves templates, partial composition, CSS structure, small JavaScript
behavior, and JSON-backed frontend evolution while preserving the current repo
baseline.

That future path may include well-bounded experiments, but this guideline keeps
Backboard grounded in a document-first architecture unless reviewed work proves a
clear need for more specialized rendering.
