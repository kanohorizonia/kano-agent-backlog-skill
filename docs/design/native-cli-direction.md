# Native CLI direction

Status: accepted as a **future direction**, not the public `0.0.3` contract.

## Context

Historically, Python was attractive for this project because it is easy for humans to learn, write, debug, and extend. It has no compile step and has a broad library ecosystem, which made it a sensible first public implementation.

Agentic programming changes part of that tradeoff.

When AI agents are the primary operator, they can tolerate stricter compile/debug loops and may invoke the same tool many times while exploring, validating, repairing, and re-checking a task. In that environment, interpreter startup and repeated import overhead matter more than they do in occasional human usage.

## Direction

A native CLI remains an attractive future direction because it can:

- reduce repeated interpreter and import overhead
- simplify distribution as a single binary in some environments
- improve CI and agent sandbox ergonomics
- reduce dependency friction for repeated automated invocation

## What this does **not** mean for 0.0.3

- Python remains the public implementation for `0.0.3`.
- The public package path remains Python-based.
- The native C++ CLI is **not** declared stable public interface in `0.0.3`.

## Release-policy consequence

Python removal or a switch to a native-first public contract requires a separate parity milestone. That milestone must demonstrate test, documentation, and operational parity for the native implementation before any public deprecation or removal of the Python path is considered.

## Why record this now

The repository already contains native work and native build infrastructure. This note makes the intent explicit without overclaiming maturity:

- native CLI work is real
- native CLI work is future-facing
- `0.0.3` remains an OSS-readiness release, not a Python-removal release
