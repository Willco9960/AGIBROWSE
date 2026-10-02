# Interface contracts

Status: unimplemented architectural contracts. [Exact MCP inputs](agent-tools.reference.json) preserve the approved manifest byte-for-byte; task 031 owns final versioned schemas and compatibility vectors. Task 021 owns native state detail. Bridge/SDKs must use one canonical schema source.

## Public tools

| Tool | Input → result | Authority |
|---|---|---|
| aibrowese_connect | Optional profile_id=agent, mode=observe/control (observe default), requested_origins=[] → session_id, granted mode/capabilities, permitted tabs, native protocol version, event cursor | Broker sessions; host grants scope |
| aibrowese_observe | session_id; optional tab_id/root_ref/text_query/token_budget/since_observation_id/cursor → tab inventory or bounded snapshot/delta | Host extraction/redaction; broker projection/state |
| aibrowese_act | session_id, UUID request_id, tab_id, observation_id, typed action → host receipt | Host guarded dispatch |
| aibrowese_wait | session_id, tab_id, after_cursor; event, timeout_ms=10000 (0–30000) → events/next cursor or explicit timeout | Broker event history |
| aibrowese_disconnect | session_id → release subscriptions/control, retain human tabs | Broker sessions and host control |

root_ref/text_query/since_observation_id/cursor require tab_id; cursor and since_observation_id are mutually exclusive. Server applies defaults. Manifest bounds/enumerations/additionalProperties=false are authoritative.

Act supports click/hover/focus, fill/fill_secret, select/set_checked/press/scroll, navigate/open_tab/back/forward/reload/focus_tab/close_tab, upload/dialog/drag. Preserve exact variant fields in the manifest. No raw CDP, arbitrary JavaScript, local-path access, shell or self-approval tool exists.

Origins canonicalize to exact origins, without wildcard grants. Host performs authoritative URL/redirect checks beyond preliminary URL validation. Each active profile keeps a blank tab after last-tab closure, enabling open_tab through the same guarded observed context.

## Results

| Result | Contract |
|---|---|
| Observation | kind, observation_id, tab/document/projection identity, revision, event cursor, node data/delta, token accounting, coverage, continuation when truncated; incompatible prior observation becomes labeled fresh snapshot |
| Receipt | request_id, outcome=completed/rejected/failed/unknown, dispatch=not_sent/sent/unknown, tab identity and event cursor |
| Wait | Matching events/next cursor or explicit timeout; timeout proves neither readiness nor failure; expired history is RESYNC_REQUIRED |
| Error | Stable actionable code/message and execution status where relevant |
| MCP representation | Failures use isError=true; successful structured results use structuredContent; compatibility serialization avoids duplicate text/structured model context |

Codes: NEEDS_PAIRING, NEEDS_APPROVAL, PERMISSION_DENIED, STALE_OBSERVATION, INVALID_REFERENCE, CONTROL_REVOKED, RESYNC_REQUIRED, UNSUPPORTED_CONTENT, OUTCOME_UNKNOWN. Task 031 defines exact response schemas. Adapters preserve uncertainty instead of turning it into a generic safe-to-retry failure.

## Native and privileged boundaries

| Boundary | Contract and validation | Detailed owner tasks |
|---|---|---|
| SDK ↔ broker | Version/session negotiation, typed commands, snapshot/delta/reset, ack/resume, events and receipts over paired loopback WSS; credential/cert identity, browser-origin rejection, bounded messages/queues | 006/008/009/021/024/031 |
| Broker → host | Typed session-bound intent, scope IDs, observation/target identity and request ID; authenticated sender/destination, host policy/lease/actionability recheck at dispatch | 006/007/015/026 |
| Host → broker | Redacted semantic state/lifecycle, authoritative receipts and permission/control revocation; no secret/file payloads or backend IDs | 009/010/019/021/026 |
| Host → OS | Native dialogs and opaque file/secret resolution scoped to session/origin/grant/expiry; no agent-chosen paths; unavailable secure storage fails explicitly | 006/044/050/057/073 |
| Updater → installation | Authenticated artifact/channel/version metadata and recovery outcomes; sandbox preserved | 076–080 |

These reserve responsibility/minimum information, not exact wire schemas or TLS/IPC mechanisms. IDs are opaque/scoped, not authorization. Redaction precedes broker publication and recording. Host owns engine/document/node identity and authoritative execution; broker owns bounded per-client projections/replay/history; SDK owns client materialization. Tasks 021–025 formalize revision, hashes, atomic reset, pagination, ack/resume and resource limits.

Human takeover cancels undispatched work; in-flight effects retain honest receipts. Disconnect keeps human tabs. Permission changes, document replacement, gaps and invalid hashes cannot silently become empty deltas. Identical live-session retries may recover an existing receipt; no automatic uncertain-action replay or new request ID hiding unknown effects.

Credentials stay outside model context. Opaque secret/file references may appear in typed tool inputs; resolved values and local paths never do. Only native UI approves. NEEDS_APPROVAL occurs before dispatch; after approval obtain fresh observation and issue a new command. Sensitive values never return through state/errors/logs.

## Transport and version boundary

Native WSS is not an MCP transport. Bridge uses stdio first and authenticated loopback Streamable HTTP later. Application sessions remain explicit, separate from MCP transport state.

The approved plan names MCP 2026-07-28 with a tested 2025-11-25 adapter. Task 032 must verify named revisions against authoritative SDK/specification support; unavailable/inconsistent revisions require reporting the blocked assumption and an approved contract correction. Task 001 does not certify their availability. Task 031 owns native version negotiation and major-version compatibility.

## Deferred decisions

| Task | Open design | Required gate |
|---|---|---|
| 006 | Attacker/capability policy, pairing/cert/IPC mechanisms, grant/revocation and sensitive-state policy | M02 malicious-page, rogue-client, cross-profile and confused-deputy negative tests |
| 021 | Exact snapshot/delta/reset/ack/resume identity/atomicity schemas and validity/pagination rules | M05 valid/invalid vectors; convergence/replay/gap/hash/backpressure tests |
| 076 | Signing authority, channel separation, key rotation, compromised-key handling, interruption and safe recovery versus unauthorized downgrade | M16 authenticated installation/update/recovery on each target |

This documentation does not accept those future designs or runtime tests.
