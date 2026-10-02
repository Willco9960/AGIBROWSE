# Windows private application IPC — task 007

Implemented boundary, 2026-10-02. This document defines the task-007 internal binary protocol, not a public tool or the task-008 WSS protocol. [Policy](policy.md) remains the authority. [Local evidence](../evidence/AIBROWESE-007.md) records the actual pinned Windows tests.

## Endpoint and process admission

The host launches the sibling `agi-browse-broker.exe` with two anonymous pipes. `CreateProcessW` uses `STARTUPINFOEXW` and `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` containing exactly the broker input pipe, output pipe and a SYNCHRONIZE-only handle to the launching host. Standard input/output carry those pipe handles; standard error carries the lifetime handle, never diagnostics. No pipe name, handle number, pairing credential or challenge is supplied in an argument. There is no listener, named-pipe admission endpoint or fallback.

Host pipe ends have inheritance disabled before launch. Child-side copies are closed in the host before CEF initialization can launch a renderer/helper. The broker clears inheritance on all three received handles before doing anything else. An additional inheritable native test event is demonstrably excluded from the allowed inherited set. CEF's own engine handles remain upstream-owned and separate.

The child starts suspended. The host assigns it to an unnamed kill-on-close job before resuming. It holds the actual launched process handle, not a PID/name assertion. BCrypt system-preferred RNG supplies a 32-byte channel challenge and independent nonzero 64-bit channel epoch. Only a proof returning that exact challenge and epoch over the private response pipe within 3,000 ms starts service. Startup errors terminate the exact launched child, including assignment failure before it belongs to the job. A standalone broker launch fails with exit 71. Parent death closes the job; the broker also tests its SYNCHRONIZE-only parent handle while polling. Ordinary host shutdown sends a closed `stop` message and waits; broker death/channel trust loss invalidates in-memory authority and ends that private peer.

The echo proof demonstrates possession of this private inherited channel, bound to the launched process/lifetime. It is not cryptographic pairing of an external client. Installation integrity and the launched broker are trusted as stated in policy; administrator injection and privileged same-user handle theft remain outside that guarantee.

## Closed binary schema

Each frame begins with an unsigned little-endian 32-bit payload length: 14–2,048 bytes. Payload starts with ASCII `AIPC`, followed by flat TLVs: `tag:u8`, `type:u8`, `length:u16 little endian`, exact value bytes. No nesting, JSON parser, executable object, method-name dispatch or optional extension fields exist. Every required tag occurs once; duplicate, unknown, extra, missing, truncated and wrongly typed fields close/reject before authority evaluation. IDs contain 1–128 printable ASCII bytes, excluding wildcard `*`; NUL/control/non-ASCII bytes fail closed. These are opaque internal IDs, not origins or user-facing names.

| Tag | Type and exact value | Applicable kinds |
|---|---|---|
| 1 | type 1, length 1; version exactly 1 | all |
| 2 | type 1, length 1; kind 1=challenge, 2=proof, 3=intent, 4=result, 5=stop | all |
| 3 | type 2, length 8; positive sequence | intent/result |
| 4 | type 2, length 8; positive generation | challenge/proof: channel epoch; intent: host grant generation; result: channel epoch |
| 5–11 | type 3; client, session, profile, tab, frame, document, operation respectively | intent |
| 12 | type 4, length 32; random challenge bytes | challenge/proof |
| 13 | type 1, length 1; 1=denied, 2=unsupported | result |
| 14 | type 2, length 8; positive channel epoch | intent |

The exact required tag sets are `{1,2,4,12}` for challenge/proof, `{1..11,14}` for intent, `{1,2,3,4,13}` for result, `{1,2}` for stop. Intent operation is exactly `observe`, `wait`, or one of the 19 accepted manifest action variants. `shell`, arbitrary JS, raw CDP, filesystem paths, approval and grant creation are absent. Version/tag/type changes require a new reviewed protocol version.

Idle reads poll every 250 ms; startup proof has a 3,000-ms deadline. Partial headers/bodies are not discarded as idle timeouts: the channel closes. Wire length is checked before allocation. Synchronous anonymous-pipe writes run on a cancellable writer with a 500-ms deadline and 1,000-ms cancellation bound. If the OS cancellation primitive cannot terminate its writer, fail-fast tears down the host/job instead of leaking stack state or hanging the GUI. A flooding, nonreading launched peer is tested to close within the bounded shutdown window. Native positive shutdown and adversarial cases are separate; forced cleanup never counts as a normal lifecycle pass.

## Host authority and separate renderer lane

The host's `Boundary` validates the channel epoch and exact next sequence, host-registered client/session binding, grant generation/deadline, live host destination/profile/tab/frame/document identity, exact operation and control lease. Sequence is consumed for denied in-order requests so a later grant cannot revive an old request. Replay/out-of-order intent is denied; a new channel has a new epoch and fresh sequence state. Destination registry keys are `(tab,frame)` tuples, avoiding opaque-ID concatenation collisions. Channel invalidation purges records. Responses contain only sequence, channel epoch and a stable denied/unsupported reason; no page state, paths or secrets.

The bounded internal `Grant` is an admission-test record, not task-009's complete origin/context/native-approval lifecycle. Only C++ host authority can install it; there is no serialized grant or renderer/native test-grant switch. **Production installs zero grants**, creates no external paired sessions and has no engine dispatch. Even the permitted native-test fixture returns `unsupported`, never an execution success. WSS/mTLS/client pairing stays task 008; full UI grants/scopes/egress revocation stay 009; target/observation and dispatch outcomes stay 019/026. Future integration must preserve this fail-closed starting state and bind host-approved records to authenticated task-008 clients.

CEF messages never enter the broker decoder or authority evaluator. Browser/frame provenance comes from CEF callbacks, the live attached-frame registry and the current `GetFrameByIdentifier` engine lookup. Detach/destruction removes both identity and bounded diagnostic counters. Underlying frame IDs are compared, not C++ wrapper addresses: pinned CEF creates different wrappers across callbacks. Unknown/stale/nonrenderer callbacks fail closed. No semantic renderer operation is enabled before its closed schema is implemented.

The optional native `--ipc-renderer-test` flag asks the real sandboxed renderer to send eight forged privileged/unknown renderer tags and report API absence and actual endpoint-handle absence. It exposes no JS binding and cannot install authority. Renderer code evaluates the live page context for absent `aibrowese`, `agiBrowse`, `nativeHost`, `broker` and `cefQuery` globals. Its handle test compares each actual private endpoint's kernel object name against the corresponding handle value in the renderer, avoiding false positives from numeric handle aliases. Diagnostic pipe identity is sent only by this native test flag; startup challenge bytes never cross the CEF lane or enter logs. The result is test evidence, never permission.

This proves the implemented application IPC boundary and pinned renderer negative path. It does not prove containment of arbitrary future engine adapters, all malicious-page semantics, every helper's token policy, external client pairing, a complete origin firewall, POSIX socket-pair behavior or an independent penetration test. Those owner gates remain required.
