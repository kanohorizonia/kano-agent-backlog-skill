# Security Policy

## Supported Versions

These repositories are alpha-stage public agent-skill repositories. Security
support is best-effort and follows the active development line.

| Version line | Support status |
| --- | --- |
| `main` | Best-effort security review and fixes. |
| Latest `0.0.x` alpha tag | Best-effort for the current alpha line. |
| Older alpha tags | Not guaranteed. |

No long-term support, response-time SLA, or backport guarantee is provided before
1.0. Fixes normally move forward to `main` and the next alpha tag.

## Reporting a Vulnerability

Do not paste sensitive exploit details, secrets, tokens, private infrastructure
data, or weaponized reproduction steps into public issues.

Preferred reporting path:

1. Use GitHub private vulnerability reporting for this repository if it is
   enabled.
2. If private vulnerability reporting is not enabled, open a minimal public
   coordination issue that says a security concern exists, names only the
   affected component or version line, and asks maintainers to establish a
   private channel.
3. Share sensitive details only after a private channel is available.

When a private channel is available, include:

- affected commit, branch, or release tag;
- affected platform or runtime;
- minimal non-public reproduction steps;
- expected impact;
- whether secrets, credentials, private paths, or private infrastructure details
  may have been exposed.

Public issues are still appropriate for non-sensitive hardening work, dependency
updates, documentation gaps, and security-adjacent questions that do not disclose
an exploitable path.

## CodeQL and Security Alerts

CodeQL is treated as GitHub-native code scanning and a source of GitHub Security
alerts. It may identify issues that should become backlog work, review findings,
or follow-up fixes.

Jenkins remains the canonical CI/CD controller for Kano agent-skill build,
test, release, and publication workflows. CodeQL is not a release gate unless a
future task explicitly configures it as one and documents the gate in the
release policy.

## Secret-Handling Expectations

This repository must remain safe for public source distribution:

- do not commit credentials, tokens, private keys, or service account material;
- do not commit private hostnames, LAN endpoints, private user profiles, or
  release credentials in public defaults;
- inject credentials through the caller's CI or local environment;
- keep generated reports, build outputs, coverage data, and package artifacts
  out of source control unless they are explicitly documented source fixtures.
