# Authenticated loopback transport — task 008

Implementation contract: Windows x64, reviewed by coordinator before implementation. [Policy](policy.md) is authoritative. [Evidence](../evidence/AIBROWESE-008.md) distinguishes actual socket tests from unautomated native enrollment dialogs. Application sessions, schemas and grants remain tasks 009/021/031.

## Dependencies and trust

OpenSSL **3.5.9 LTS** provides TLS and X.509/CSR/signature validation; Boost **1.92.0** Beast/Asio provides HTTP/WebSocket framing and asynchronous deadlines. Exact archive SHA256 pins and official checksum URLs are in [dependencies.lock.json](../../tools/transport/dependencies.lock.json). Sources were verified against the [OpenSSL official release page](https://openssl-library.org/source/), [OpenSSL release checksum](https://github.com/openssl/openssl/releases/download/openssl-3.5.9/openssl-3.5.9.tar.gz.sha256), [Boost official release](https://www.boost.org/releases/1.92.0/) and [Boost archive metadata](https://archives.boost.io/release/1.92.0/source/boost_1_92_0.tar.gz.json). This is dependency selection/code review, not an independent cryptographic audit.

Windows MSVC v143 compiles static OpenSSL with `/MT`, `no-shared no-asm no-tests no-module no-legacy no-ssl3 no-comp`; disabling assembly avoids a separate NASM dependency. The native socket tests independently exercise required TLS behavior. The build-only native Strawberry Perl 5.42.3.1 portable archive is pinned by its official release SHA256. No global installation/PATH changes occur. Boost templates require MSVC `/bigobj`; Windows consumers declare `WIN32_LEAN_AND_MEAN/NOMINMAX` before Windows headers. Dependency bootstrap enforces finite curl/extraction/build deadlines and validates a complete source marker and toolchain/flags/library-hash receipt. A stale/partial install fails closed. A measured full OpenSSL rebuild took **365 seconds**, plus source extraction **30 seconds** on this workstation; the hosted clean-build duration remains a CI measurement. CI has an explicit 45-minute total bound and 15-minute dependency phase, replacing the previous 25-minute total bound. No credential/profile directories enter CI artifacts.

The host retains its enrollment CA signing key, transport server leaf key/certificate, and paired allowlist in a bounded **current-user DPAPI** protected binary vault. The native client stores its private key through current-user DPAPI too; failure never falls back to plaintext. Private keys enter neither argv, query, clipboard, logs nor model messages. Public certificate/fingerprint/challenge files are bounded and contain no private key. Host and broker necessarily use their leaf keys in process memory while TLS runs. The broker receives only the transport server leaf key, server certificate and CA certificate over the private inherited startup channel; it never receives the enrollment CA signing key or any client private key. Trusted installation/host/broker and same-user/admin compromise limits remain those in policy.

TLS is exactly **1.3**, with peer certificate verification and a required client certificate. The enrolled CA validates certificate chain, purpose and validity; the client additionally validates literal IP `127.0.0.1` and the explicitly enrolled server SPKI SHA256. The host validates the client's exact issued certificate SHA256 and SPKI against its persisted allowlist, including current credential generation/expiry, on admission and every message. Certificate subjects alone have no authority. Tickets, session cache, early data and TLS/WebSocket compression are disabled. There is no trust-on-first-use, reconnect identity replacement or plaintext fallback.

Certificates use P-256/ECDSA-SHA256. The local CA lasts ten years; server/client leaves last **30 days**. Rotation is deliberately unavailable in this card: expired/corrupt store or a pending revocation recovery marker disables agent transport while human CEF browsing remains usable. No automatic CA/server-key replacement is accepted. The native transport-unavailable dialog gives recovery guidance; an explicit local installation recovery and fresh enrollment are needed. Future authenticated rotation must preserve policy's reviewed overlap/fresh-enrollment rule.

## Native enrollment and revocation

The host's `Ctrl+Shift+P` shortcut requires a non-null Windows `CefEventHandle` (`MSG*`), `WM_KEYDOWN`, matching virtual key, `KEYEVENT_RAWKEYDOWN`, and Control/Shift modifier flags. These callback checks do **not** prove physical or human input provenance. In the pinned CEF implementation, [SendKeyEvent forwards a constructed UI key event](https://github.com/chromiumembedded/cef/blob/a03e7146331fc5bd72784591e82df01e8007e17b/libcef/browser/native/browser_platform_delegate_native_aura.cc#L61), [Windows constructs it without a native message](https://github.com/chromiumembedded/cef/blob/a03e7146331fc5bd72784591e82df01e8007e17b/libcef/browser/native/browser_platform_delegate_native_win.cc#L452), and Chromium [clones that key into a non-null `os_event`](https://github.com/chromium/chromium/blob/154.0.8037.94/components/input/native_web_keyboard_event_aura.cc#L134). CEF [returns the address of its stored native-event field](https://github.com/chromiumembedded/cef/blob/a03e7146331fc5bd72784591e82df01e8007e17b/libcef/browser/native/browser_platform_delegate_native_win.cc#L422); pointer presence alone therefore cannot distinguish injected input. No blanket rejection of synthetic input or OS message injection is claimed.

Enrollment still requires native file selection and explicit fingerprint confirmation; a shortcut never approves a client automatically. A website has no JS binding, privileged IPC endpoint or network enrollment route. Current production dispatch enables zero application actions, and the [closed public press enum](../contracts/agent-tools.reference.json) excludes `Ctrl+Shift+P/I/R`. Any future command dispatcher must exclude privileged application shortcuts regardless of synthetic-event provenance. The callback and dialog paths are code-reviewed; complete file-picker/dialog automation is not claimed by API tests.

1. The native client executable creates a fresh P-256 protected key and signed PEM CSR using `agi-pair-client create <key-file> <csr-file>`; these arguments are file locations, never secret values.
2. The human opens native enrollment, selects the local CSR and confirms the displayed client SPKI/server SPKI. The host parses ≤4,096 bytes, requires a supported P-256 public key, valid ECDSA-SHA256 CSR signature, zero requested attributes, correct CSR version and no trailing material; requested names/extensions never become authority.
3. The native host saves a challenge outside network/page/model context. Its signed input binds the exact client SPKI, server SPKI and a fresh 256-bit nonce. The host owns one pending challenge, with a monotonic **300,000-ms** deadline and **five** failed proofs maximum. A later native enrollment invalidates the previous challenge. Copies, concurrent consumption, changed identities, expiry and replay fail closed.
4. The local client signs the selected challenge using `agi-pair-client prove <key-file> <challenge-file> <proof-file>`. Native enrollment selects the proof and atomically consumes the host record before certificate issuance/persistence.
5. The host saves a public credential bundle; canceled/failed export revokes the newly issued record. `Ctrl+Shift+I` shows the literal current endpoint and server fingerprint; `agi-pair-client probe <key-file> <credential-file> <port>` validates that endpoint. `Ctrl+Shift+R` confirms client fingerprints for native revocation.

Revocation writes a protected pending marker **before** removing the live record and updating the vault. A failed vault write leaves the marker, so restart cannot resurrect old disk pairings. Runtime identity validation denies the removed record; active connections recheck within 250 ms plus the bounded IPC exchange. Native persistence errors stop agent transport. Successful pairing persists across host restart; **no tab/origin/application grants persist or arise from pairing**.

Default storage is `%LOCALAPPDATA%/AGI-BROWSE/Transport/authority.dpapi`; an operator's explicit `--profile-dir` uses `<profile-dir>/Transport/authority.dpapi` for isolated testing. Protected data is never automatically replaced when unreadable/expired. Public exports use an atomic temporary-file replacement. OS-managed backups and compromised privileged processes are not forensic-isolation guarantees.

## Endpoint, protocol and finite bounds

The only listener is `127.0.0.1` on a fresh OS-assigned port. Non-loopback peers fail. A native private IPC ready message reports the selected port to the host. HTTP must be GET HTTP/1.1, exactly `/transport/v1`, and have exactly one `Host: 127.0.0.1:<configured-port>`. DNS aliases, unexpected authority, any query and other paths fail. **Any Origin header** (empty, `null`, browser origin, duplicate) rejects before WebSocket upgrade; absence is never authentication. Compression offers reject.

| Resource | Bound and enforcement |
|---|---|
| TLS handshake / HTTP upgrade | 3 seconds each, asynchronous socket deadline, including unauthenticated/partial clients |
| Native client | 3-second connect/TLS/WS stage bounds; unconditional 8-second overall cancellation; no resolver/DNS |
| Pending + active server sessions | 8 total; 64 accepted attempts per second; listen backlog 8 |
| HTTP parser | 4,096-byte header/buffer, body limit 0, strict reviewed Beast parser |
| WebSocket | 256-byte aggregate message limit; binary `AT01` only (four bytes); no nested data or extension negotiation |
| Rate / queues | 16 data/control messages per second per connection; one read/write in flight, one fixed 17-byte response, no application queues/caches |
| Idle / transport lifetime | 5-second idle timeout; 60-second absolute transport lifetime; 250-ms live identity recheck |
| Host transport session bindings | 64 opaque bindings, original 60-second monotonic deadline, immutable client/certificate binding |
| Pairing/state | 32 approved clients; one pending challenge; PEM/key/CSR field ≤4,096 bytes; protected vault ≤262,144 bytes |
| Private IPC | Config ≤16,384 bytes; identity/ready frames ≤2,048 bytes; 1,000-ms identity response deadline and existing bounded pipe writer |

`AT01` is a temporary transport-only admission probe, not a public tool/native application schema. Successful authentication returns only `PERMISSION_DENIED`. Unknown/nested/scope requests close. The host still installs **zero grants**, returns no page/profile/tab metadata and has no enabled engine dispatch. The five-tool manifest is unchanged.

## Private channel extension

Task 007's version-1 challenge/proof/intent/result/stop messages and permission semantics remain unchanged. The coordinator reviewed a closed, distinct **IPCv2** extension; version 1 cannot carry these fields/kinds, version 2 cannot carry legacy grant-bearing intent shapes, and duplicate/unknown/missing/type/size errors reject. It uses the existing inherited handle allowlist, launched-process lifetime, fresh channel challenge/epoch, and renderer exclusion.

| Kind / required tags | Meaning |
|---|---|
| `6 transport_config`: `{1,2,14,15,16,17}` | version 2; current channel epoch; server leaf private key PEM, leaf cert PEM, CA cert PEM (tags15–17 type3, ≤4,096 each). No grant, client private key or CA signing key. |
| `7 identity_check`: `{1,2,3,5,6,14,18}` | exact next positive identity sequence; client SPKI ID, opaque transport session, current channel epoch, leaf cert SHA256 (tag18 type3, exactly64 lowercase hex). |
| `8 identity_result`: `{1,2,3,13,14}` | matching sequence/epoch; result1=paired,2=denied. This is transport identity, **never application permission**. |
| `9 transport_ready`: `{1,2,14,19}` | channel epoch and literal listener port (tag19 type2 unsigned64,1–65535). |

Identity sequence is independent of legacy intent sequence. Channel epoch binds only the launched peer; credential generation binds only the host's approved persisted pairing. The host binds session/client/exact certificate and rechecks the native allowlist. Reconnect creates a fresh opaque transport session, without grants. Channel loss stops the broker/listener and closes active clients. The broker is a trusted transport TCB member as policy states; forwarding an identity does not create browser authority.
