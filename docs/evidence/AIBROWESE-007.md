# AIBROWESE-007 — private IPC and renderer application boundary

Implementation and local runtime verification: 2026-10-02. Parent acceptance/publication and exact-head clean-Windows CI are separate gates; no remote CI success is claimed by this local record. Assigned routing: Sandbox engineer, Sol 6.1 / high / standard. [Internal protocol and limits](../security/private-ipc.md) explain the implementation and remaining scope.

## What now runs

The actual pinned Windows CEF host launches a separate private broker with an explicit inherited-handle allowlist, BCrypt startup challenge, pre-resume lifetime job and bounded channel I/O. Production starts with zero authority and no external listener. Closed binary intent admission validates shape, sender/session, channel/grant generation, sequence, destination and exact operation. Broker/renderer input cannot create a grant. CEF renderer callbacks have an independent unprivileged lane bound to current native frame provenance; forged broker/grant/shell/CDP tags never enter privileged admission.

## Local results

| Check | Actual result |
|---|---|
| Pinned CEF build | PASS: unchanged CEF 154.0.33 / `ga03e714`, upstream bootstrap launcher, `/MT` DLL/native IPC/broker |
| Native CTest | PASS: **193 checks**, 1.67 seconds; [sanitized log](AIBROWESE-007-native.log) |
| Actual renderer/native lifecycle | PASS: **1.9953811 seconds**, host exit 0; [sanitized result](AIBROWESE-007-local.json), [events](AIBROWESE-007-lifecycle.jsonl) |
| Renderer containment | Two actual renderer tokens restricted, integrity RID 0; eight forged application messages rejected; live page native globals absent; actual private endpoint handles absent |
| Shutdown | Native WM_CLOSE, normal browser/window/CEF/broker shutdown; zero orphan processes; forced cleanup false |

Native negatives cover closed tags/types/version, all payload truncations, duplicate fields, missing/extra fields, byte/string/wildcard bounds, unsupported public operations, zero-grant actual wire denial, sender/session/profile/tab/frame/document mismatch, grant/channel epoch, exact sequence/replay, monotonic expiry, control lease, tuple-key collisions, channel invalidation, correct/wrong startup challenge, extra inherited object exclusion, broker death, restart, oversized wire framing, slow partial headers/bodies, nonreading flood peer and bounded normal/hostile cleanup. Native-test grants are private test-executable C++ records; none can be activated in the browser through a command line, renderer message or broker wire.

The real renderer probe executes C++ code in the sandboxed CEF renderer and evaluates the real loaded page's V8 context. It does not inject a production JS API or claim a hostile webpage can directly call `CefProcessMessage`. It deliberately tests a compromised renderer's attempted application tags plus the absence of a page-accessible native bridge. The file fixture remains test infrastructure and grants no agent file-navigation authority.

## Failures retained honestly

Initial renderer run `7c28dc2ed7fa4e00a357f6692a6b6d36` failed after 45.136 seconds and required cleanup: all eight probes and their result were rejected as identity mismatches. The doubtful implementation assumption was that identical CEF engine frames share the same C++ wrapper pointer. Pinned CEF uses different wrappers. The fix checks stable engine identity through the attached-frame registry, frame validity and current `GetFrameByIdentifier` browser lookup. The first corrected run `424cb94ab0564e37918fedeb36c0cd29` passed; final map-cleanup/log-saturation run `c8520f1f783d495fa0b4741ceae344f5` is the recorded final pass.

The initial native inheritance test also incorrectly treated an occupied numeric handle as the inherited extra event; CRT handles can reuse that number in the child. It was corrected to compare actual kernel object name. The native suite passed after that focused repair. A direct MSBuild invocation inherited duplicate `PATH`/`Path` names; the existing normalized `tools/cef/build.ps1` resolved that environment issue. No sandbox-disable flag or global Git ownership setting was added.

## Reproduce

From the repository root, on Windows x64 / VS2022 v143:

```powershell
./tools/cef/build.ps1
ctest --test-dir build/windows-cef -C Release --output-on-failure
./tools/cef/test-lifecycle.ps1 -SecurityProbe -EvidenceDirectory build/cef-security
```

Local cached build used `-BuildDirectory ./build/windows-cef-normalized -CacheDirectory ./build/cef-research -CMakeExecutable ./build/cmake/cmake-4.2.0-windows-x86_64/bin/cmake.exe`. CTest used that same build directory/configuration. Native WMI/token/window inspection ran from the parent agent's approved user context. Workflow `windows-cef.yml` now triggers for broker/IPC/security-test changes and requires native negatives plus real renderer negatives; lifecycle, security and CTest evidence are uploaded. CI has not been run by this subagent.

## Explicit remaining work

No task-008 WSS/mTLS enrollment/client authentication, task-009 native approval/full origin-grant lifecycle/egress revocation, target/action dispatch, POSIX private socket pair, cross-platform containment or independent security audit is implemented or accepted by this evidence. The broker is still a trusted transport component under the accepted policy. The host admission and zero-grant start are the foundation for those subsequent tasks, not substitutes for their gates.

## Coordinator acceptance — clean Windows CI verified

Accepted after direct artifact inspection. Published implementation `e22c64626d25b646985efc21a79f354f95a235db` passed [Windows CI run 37076718675](https://github.com/Willco9960/AGIBROWSE/actions/runs/37076718675), job 111068158893. Pinned SDK retrieval and native build passed. Native CTest passed 193 checks in 1.63 seconds. Normal lifecycle passed in 2.481 seconds; actual renderer security lifecycle passed in 1.943 seconds with eight forged messages rejected, two restricted RID0 renderer tokens, host exit0, zero orphan processes and no forced cleanup.

Artifact11256863429 was downloaded; its SHA256 matched GitHub digest `b1220b093484fe97ecdaac55fb398c2c4a5677b416dc731b31052f578c574f0e`. [Sanitized CI acceptance receipt](AIBROWESE-007-ci-acceptance.json) preserves the source/run/artifact identities and actual results. Raw synthetic fixture artifacts stay ignored under `build/ci-37076718675`; they are not production diagnostic exports. Source acceptance applies to this implementation commit. This evidence-only follow-up changes no native source or tests. Full M02 acceptance still requires008–010.
