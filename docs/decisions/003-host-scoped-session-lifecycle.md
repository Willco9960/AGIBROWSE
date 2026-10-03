# ADR-003: Host scope authority bound to authenticated transport lifetime

## Status

Implementation contract approved by coordinator,2026-10-03. Public application-session/resume and engine dispatch remain021/026/031.

## Date

2026-10-03

## Context

Task008 authenticates client certificates through the real inherited host–broker channel but supplies no browser permission. Task009 must reject cross-scope reads/writes and prevent queued commands or observations from retaining revoked authority. A separate native test library alone would leave real host socket closure, pairing revoke and channel loss disconnected from that authority.

## Decision

Keep immutable scope records, native documents, exclusive leases and128 one-use delivery tickets in the host. Recheck their revisions and trusted monotonic time during the actual bounded synchronous handoff. Pairing validation/binding and revoke serialize under the pairing mutex, followed by host binding and scope locks. Native revoke listeners are weak, bounded and invoked before persistence; failures deny runtime authority and propagate to transport shutdown.

Bind current scope state to task008's fresh60-second transport identity/deadline. Add only closed private IPCv2 kind10 with client/session/epoch/shared sequence to retire an authenticated socket. It is one-way and never serializes a grant. Bound live sessions at64 and all retained entries at4096; retire tombstones at the original lifetime, with host-only10s expiry-close grace. Reconnect grants nothing. Preserve policy006's future public application-session/replay design; this narrower transport integration is not a promise of implemented MCP resume.

## Alternatives considered

Letting the broker approve scope would cross the trusted human/native authority boundary. Checking only at admission would let queues and cached delivery outlive a revoke, lease change or document replacement. Permanent64-session tombstones would stop the host after64 connections; unbounded tombstones would exhaust memory. An unsequenced close could race another identity exchange; an unsolicited reply could poison that exchange. Expanding the public tool/native grant UI now would bypass the assigned021/031 contracts. Each is excluded by the selected bounded host design.

## Consequences

The real authenticated host lifecycle and scope gate are testable now while production grants/actions remain zero. Future native UI must supply the browser canonical-origin parser and document registry; every engine dispatch/delivery must occur within the synchronous gate. Socket-drop grant resumption needs a separately reviewed application identity/replay lifecycle. No in-memory design can guarantee durable credential deletion when the OS refuses every storage write; transport stops and local recovery is required. A previously entered handoff may finish before concurrent revoke completes; revocation does not roll back effects already sent.
