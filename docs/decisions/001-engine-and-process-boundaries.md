# ADR-001: Upstream CEF and separate authority boundaries

## Status

Engine/process boundaries accepted from approved roadmap. Separate C++/CMake broker accepted as the task-001 implementation contract by the coordinator on 2026-10-01. Runtime and detailed security/state/update policies remain pending.

## Date

2026-10-01

## Context

Normal human browsing and structural agent observations must coexist without granting renderer privileges or requiring a browser-owned model subscription. Windows first; macOS/Linux parity before 1.0.

## Decision

Pinned upstream CEF/Chromium sandboxed distributions, C++/CEF Views host, no Chromium fork. Host owns GUI/profiles, permissions, leases, guarded execution and approved OS work. Separate local broker owns validated clients and bounded state delivery; authenticated narrow IPC cannot supersede host policy.

Implement the broker as a separate C++ executable built with CMake. Reusing the native toolchain reduces packaging/runtime maintenance across the three OS targets. The broker does not link GUI/renderer behavior or gain host authority. Task 002 scaffolds this direction; 006/008/021 select reviewed IPC, transport libraries and wire details.

Expose minimal semantic JSON snapshots/deltas over loopback WSS to TS/Python SDKs. TypeScript MCP bridge exposes five approved tools through standard MCP transports. Internal CDP is an engine adapter; raw CDP/JS is never public.

See [architecture](../architecture.md), [interfaces](../contracts/interfaces.md) and [module map](../module-map.json).

## Alternatives considered

A Chromium fork adds engine/security maintenance outside the approved scope. Public raw CDP/JS expands authority beyond scoped semantic operations. An in-GUI broker couples slow consumers/state caches to human responsiveness. Hosted control conflicts with local-only 1.0. These alternatives are excluded by the approved plan; no comparative runtime experiment is claimed.

A TypeScript broker could reuse SDK code but adds a native-distribution runtime; a Rust broker could add memory-safety advantages but introduces another compiler/FFI/build surface. C++ is the bounded implementation choice for the existing native product. It requires resource-limit tests, parser fuzzing and sanitizer evidence in tasks 024/067; those gates are not waived.

## Consequences

Pinned-release sandbox/platform/DevTools behavior needs real tests. Host/broker validate independently; clients cannot grant themselves scope. Model calls remain external; state changes do not force model calls. Human control wins; unknown effects remain unknown without replay.

Tasks 006, 021 and 076 own capability/security, synchronization and update trust detail. This ADR chooses none of their cryptographic/state algorithms and replaces none of their gates.
