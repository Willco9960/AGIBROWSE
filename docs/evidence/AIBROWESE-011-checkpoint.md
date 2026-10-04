# AIBROWESE-011 implementation checkpoint

Status: **Historical checkpoint, superseded by [current implementation evidence](AIBROWESE-011.md).** Task011 remains unaccepted pending parent review and clean Windows GUI CI. Based on accepted task010 HEAD `68da594`. No commit/push or board mutation performed by the implementation lane.

## Implemented and checked

Host-only `apps/browser/lifecycle.*` reserves bounded native window/tab identities (32 windows,64 pending/live tabs). Identity counters are unique only within one host registry lifetime, are not credentials, and are never reused in that lifetime. Tests cover activation, reorder, moves, duplicate bindings, engine-ID reuse, popup profile/opener matching, close invalidation, pending reservation release and resource bounds.

`ScopedAuthority::InvalidateNativeTab` synchronously removes exact profile/tab documents, lease and pending scope tickets. The new lifecycle suite tests queued reads/actions, subsequent denial, lease removal and an unaffected tab. Production host transport exposes this barrier only to native lifecycle code; no new grants, IPC kind or public schema.

CEF source now reserves native identities, binds real engine browsers, attaches Views to native windows, provides Ctrl+T/Ctrl+W/Ctrl+Tab, registers popups with native opener/profile identities, and retains the inherited CEF request context. Ctrl+W reserves a fresh blank replacement before requesting final-tab closure. Closing browser callbacks erase retained frame/provenance maps and Views. These source paths are **compiled, not demonstrated**.

Local verification:

- Native lifecycle executable: **122 checks passed**.
- Existing native scoped-session executable: **348 checks passed**.
- Pinned CEF host `ClCompile` target with warnings treated as errors: **passed**.

MSBuild required a child `ProcessStartInfo` environment with one PATH entry because this environment contains both PATH and Path. Two focused compile corrections renamed `CreateWindow` to `NewWindow` to avoid the Windows macro and included the full `CefFillLayout` declaration. No system environment changes.

## Required next work before acceptance

1. Harden canceled beforeunload closure: current BeginClose state can outlive a canceled browser close; preserve immediate scope revocation while keeping human controls usable.
2. Release or reject all asynchronous reservations on failed creation, opener shutdown and pending popup cancellation; add timeout/cancellation proof and exception-safe invalidation callbacks.
3. Wire actual native view reorder/window moves and test real pinned CEF lifetime/order behavior; registry-only movement is insufficient acceptance evidence.
4. Add lifecycle GUI fixture/harness and CI evidence, run clean full Windows build plus existing security/privacy regression suite, then parent review.

Local Defender quarantined the pinned launcher and copied host executable before task011. No exclusions, restoration, replacement download, local full link/package or GUI execution was attempted. Clean Windows CI remains required. Existing task010 logging/redaction controls were preserved in source; their full runtime regressions have not been rerun for this checkpoint.
