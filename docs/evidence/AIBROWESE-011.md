# AIBROWESE-011 native tab/window lifecycle

Status: source implementation ready for parent review; **clean Windows full build and GUI CI still required**. No runtime GUI acceptance claim. Based on accepted task010 HEAD `68da594`; implementation lane made no commits, pushes or board writes.

## Local evidence

Final native lifecycle suite: **138 checks passed**. Existing scoped authority suite: **348 checks passed**. Privacy contract suite: **46 checks passed**, including payload rejection for the eight new closed lifecycle diagnostic kinds. Final pinnedCEF154 host `ClCompile` target passed with warnings treated as errors. PowerShell harness parser passed. Local evidence and source hashes are recorded in `AIBROWESE-011-local.json`.

The full CEF launcher was quarantined locally by Defender; false positive has not been established. No launcher restoration, exclusions, replacement download, local full packaging or local GUI execution was attempted. A passing compile is not proof of the lifecycle behavior.

## Implemented scope

- Stable host-native identities survive tab order and window changes; pending and live reservations share64-tab/32-window bounds. Per-registry monotonic IDs are not authentication tokens and do not claim cross-process uniqueness. Reorder/move uses actual `CefPanel::ReorderChildView` and Views detach/attach, retained references, actual engine/context/window checks and rollback; background titles cannot overwrite the active window title.
- Close request removes exact profile/tab documents, leases, queued reads/actions and tab membership in existing grants. Native beforeunload confirmation reports explicit accept/cancel; cancel restores human lifecycle control but grants/queued tickets never revive. Barrier exceptions are contained and each new attempt must re-run its barrier. The production failure path also attempts transport shutdown.
- Final-tab Ctrl+W reserves a fresh blank first; final native-window close shuts down that window. Window delegates release their initial View ownership after attachment. Alloy DoClose destroys only the closing tab's View; final OnBeforeClose releases frame/provenance, title and View ownership. [ADR004](../decisions/004-native-tab-lifecycle.md) records the supported modern Alloy **style** on the same Chrome bootstrap/sandbox.
- Popups reserve native opener/profile state without grants and retain CEF's inherited request context. Ten-second pending creation deadlines are swept every250ms. Popup abort, opener shutdown and final-window shutdown release reservations; late engine creation cannot bind a canceled identity.

## Clean CI verification contract

Run `tools/cef/test-lifecycle.ps1 -TabProbe -EvidenceDirectory build/cef-tabs` after the standard clean pinned build. The workflow now includes this test and exports its result, closed event log and stdout/stderr. It requires exactly five actual browser-created IDs to match five graceful browser-close IDs, a restricted renderer token and no run-owned orphan processes/forced cleanup.

Host fixture proof events require actual CEF popup context/opener identity, native child order and active title, inactive-tab close while siblings stay usable, root move with same engine/profile/request context and actual `GetWindow`, one actual beforeunload cancellation followed by one actual acceptance, initial-tab resource release while the source sibling survives, asynchronous blank replacement remaining visible, deadline sweep and final graceful native window shutdown. The fixture waits for replacement browser creation/drawing. A five-second final hold gives the external harness time to inspect renderer tokens; that timer is not close/cancel evidence.

The pending-expiry fixture deliberately reserves a native tab without initiating CEF creation to exercise the production timer cleanup; it does **not** claim a real CEF creation failure was injected. Core tests separately cover pending cancellation, exact expiry boundary, opener lookup, late binding denial, engine reuse and exception retry. Actual popup-abort/failed-creation behavior remains subject to code review and the supported CEF callbacks; this fixture's ordinary popup creation is a real engine path.

## Remaining acceptance work

Parent review, exact-source clean Windows build/GUI/security/privacy regressions, inspection of event/process evidence, and any focused repairs revealed by that run. Task011 must remain In Progress until those gates pass. No navigation UI012, agent tools/actions/grants, raw CDP or renderer-native API was introduced.

## Focused repair1: Windows macro/link mismatch

Clean Windows CI run `37169082800` for exact source `57d108d` failed at host linking with `LINK2019`: Windows-first `cef_main.cpp` expanded the internal `FindWindow` declaration/calls to `FindWindowW`, while the standalone lifecycle library defined `FindWindow`. ClCompile alone did not expose this mismatched symbol.

Renamed that host-internal API to `LookupWindow` in its declaration, implementation, CEF adapter and native tests. No Win32 macro was globally undefined. The native lifecycle test now includes `windows.h` before the lifecycle header on Windows; its linked library still compiles without that include, making this include-order/link mismatch a tested regression.

After repair, the native lifecycle target rebuilt **and linked**, and138 lifecycle/348 scope/46 privacy checks passed. Final CEF host ClCompile passed with `/WX`. Updated local source hashes reflect the repair. Full host linking/GUI acceptance still requires a new clean Windows CI run; no quarantined launcher was restored and no local full build was attempted.
