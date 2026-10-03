# M02 sandbox and local authorization acceptance

Accepted 2026-10-03 after read-only milestone review assigned to GPT-6.1 Sol/high and coordinator inspection of exact-source clean Windows evidence. Provider-confirmed model identity and task token costs remain unavailable.

The gate is satisfied for the implemented foundation: unpaired clients and untrusted pages cannot obtain browser-control privileges. The [M01 prerequisite](AIBROWESE-M01.md) is accepted. Pairing creates zero application grants, the renderer lane cannot install authority, and host-owned session scope is checked again at actual synchronous delivery.

- 006: explicit threat/capability contract and attack ownership — [design evidence](AIBROWESE-006.md).
- 007: restricted renderer, private inherited IPC and negative application escape tests — [evidence](AIBROWESE-007.md).
- 008: authenticated literal-loopback TLS, native pairing/revocation and browser-Origin rejection — [evidence](AIBROWESE-008.md).
- 009: scoped grants, document/lease revisions, disconnect/revocation and private close ordering — [verified CI receipt](AIBROWESE-009-ci-acceptance.json).
- 010: closed host diagnostics, redaction staging, actual broker denial and live privacy sentinels — [verified CI receipt](AIBROWESE-010-ci-acceptance.json).

Final integration source `9c742c9a19d3649ae72ba9519c99e30aef37fe5d` passed CI37135197435. Artifact11278528726 SHA256 `4bf0afe60402f60e503bdacd9ffd3c4719168bf777b5989ab2266a913787c85c` was digest-verified and inspected. Seven native suites passed (193 IPC,348 scope,60 TLS,77 host lifecycle,143 socket churn,30 privacy and7 actual broker checks), as did seven cache cases. Healthy/security/privacy/expired/corrupt CEF scenarios each had restricted RID0 renderers, exit0, no orphans and no forced cleanup. Positive privacy callbacks, sentinel scans and five rejected logging startups passed. No unchanged suites were redundantly rerun for this documentation review.

This is a security foundation, not an independent penetration test or a finished browser. Production enables zero application observations/actions. Engine origin parsing/extraction, structured sensitive subtrees, UTF-8, stream/replay/cache adapters, actual dispatch, fatal/crash exports, non-Windows parity and independent review066 remain mandatory later gates. Native keyboard callback handles do not prove physical human input; future dispatch must exclude privileged application shortcuts. Failed revocation persistence can require explicit local recovery. The local upstream launcher remains quarantined by Defender; no protection changes were made and clean CI success does not restore that local executable.
