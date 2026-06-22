# Actor Alias And Assignment Policy

Status: accepted design contract for repo-visible actors and default assignment.

## Goal

Backlog Markdown must be reviewable in public or semi-public repositories without
embedding personal contact details. Work items should still say who or what owns
next action, and whether that value was explicit or inherited from product
policy.

## Repo-Visible Actor Aliases

Use short aliases in tracked backlog files:

```text
owner: koa
external:
  reviewer: reviewer-koa
```

Allowed alias shape:

- lowercase letters, digits, dot, underscore, and dash
- starts with a lowercase letter
- stable within the repository
- does not contain email addresses, phone numbers, access tokens, or private
  account identifiers

Alias examples:

| Alias | Meaning |
| --- | --- |
| `koa` | Local service or agent gateway actor. |
| `codex` | Codex coding agent actor. |
| `maintainer` | Human maintainer role, not a personal identity. |
| `reviewer-koa` | Review role or service alias. |

## Private Identity Mapping Boundary

Tracked backlog files store aliases only. Mapping an alias to a real user,
service principal, chat account, or credential belongs outside the source
backlog, such as a private local config, secret manager, CI credential store, or
organization directory.

This policy is not an authentication or authorization model. It only defines
what may be written to repo-visible backlog files.

## Product Defaults

Product configuration may define default aliases:

```toml
[product]
default_assignee = "koa"
default_bug_reviewer = "reviewer-koa"
```

The same fields may also appear in project-level product registration:

```toml
[products.my-product]
prefix = "MYP"
backlog_root = "_kano/backlog/products/my-product"
default_assignee = "koa"
default_bug_reviewer = "reviewer-koa"
```

Product-local `_config/config.toml` overrides project-level product defaults
when both are present.

## Create-Time Behavior

When creating a work item:

- an explicit `--owner`, `--assignee`, or `--reviewer` wins
- omitted owner inherits `product.default_assignee` when configured
- omitted reviewer inherits `product.default_bug_reviewer` only for Bug items
- inherited values are written as normal frontmatter so later readers do not
  need to resolve historical config
- source metadata records whether the value was explicit or inherited

Current frontmatter fields:

```yaml
owner: koa
external:
  reviewer: reviewer-koa
  owner_source: inherited:product.default_assignee
  reviewer_source: inherited:product.default_bug_reviewer
```

Use `external.owner_source: explicit` and
`external.reviewer_source: explicit` when the CLI caller provided the value.

## Review Rules

- Do not write personal names or email addresses into actor aliases.
- Do not infer a reviewer for non-Bug items unless a future accepted policy adds
  that rule.
- Preserve explicit assignments when product defaults change later.
- Do not use this policy as proof of authorization; it is assignment metadata
  only.
