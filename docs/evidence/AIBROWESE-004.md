# AIBROWESE-004 deterministic browser fixtures

Implemented 2026-10-02. Owner scope: tests/fixtures, tools/fixtures, fixture scripts/packaging, and an additive FixtureUrl lifecycle-harness parameter authorized by the coordinator. No host, broker, agent protocol or sandbox changes.

## Acceptance behavior

Two HTTP servers bind only 127.0.0.1 on distinct ports. Default CLI origins are ports 43110/43111; tests use ephemeral ports. A SHA-256 seed gives stable labels/receipt code; explicit reset/restart returns empty state with the same dataset. No time, randomness, external services or new dependencies affect outcomes. `tests/fixtures/expected.json` records seed-004 golden data and the 14 browser assertion names.

Fixture coverage includes native required/email validation and form POST/303 receipt, anchor and HTTP navigation, cross-origin redirect/frame/SOP/CORS behavior, and three explicitly ordered dynamic DOM additions with exhaustion. The conformance page exercises these inside a real renderer, then records a report and readiness title. This is page-script conformance verification; no claim is made about agent commands, trusted human input, semantic extraction or future permission contracts.

## Verification and attempts

1. Initial Node test run found an incorrectly transcribed golden digest and a custom Host negative assertion using fetch (which normalized the header). Corrected the oracle and changed that test to raw node:http. This was one focused repair cycle.
2. Independent .NET SHA256 of UTF-8 `004`: `9c1850fcaa632f2189deac5e9b66e02fa85be92a920b6cae7696c9b691e4bacb`. Its first bytes establish the golden code and item labels independently of the Node implementation.
3. `node --test tools/fixtures/server.test.mjs`: 2 tests passed, covering equal/different seeds, reset/restart, invalid/valid submission, receipt, routes/redirects, ordered steps/exhaustion, cross-origin/Host negatives and bounded report input. Initial passing run: 306 ms.
4. `node tools/package-scaffold.mjs fixtures`: completed; runnable server/pages and native harness included. Native CEF binaries intentionally excluded.
5. Worker attempt `node tools/fixtures/test-browser.mjs`: blocked before launching CEF by `Get-CimInstance Win32_Process`, Access denied, HRESULT 0x80041003. The initial browser report recorded exit code 1 and no reports/submissions/steps. Coordinator reran with authorized Windows process-inspection access, resolving that environment limitation without weakening the harness.

## Actual native browser acceptance

Coordinator executed `node tools/fixtures/test-browser.mjs` and `node tools/fixtures/test-browser.mjs 005` against the existing pinned Windows CEF host. Both produced all 14 expected passing assertions, one valid submission, steps `[1,2,3]` and native harness exit code 0. Worker inspected both browser reports and lifecycle results after execution.

| Seed | Receipt code | Native lifecycle run | Browser assertions |
|---|---|---|---|
| 004 | 9c1850fcaa63 | bf41486a0679473693a84dbc3b2d3442 | 14/14 pass |
| 005 | 5a96acc64c72 | 48854b8293c94acab50e895ed91c72c7 | 14/14 pass |

Receipts in both runs were `{id:1,name:"Ada",email:"ada@example.test",choice:"alpha",code:<seed code>}`. Ordered outcome names match `tests/fixtures/expected.json`. Equal-seed data/reset/restart repeatability is established by Node tests; distinct-seed real browser runs preserve workflow outcomes while changing the code/data. The native harness independently verified restricted low-integrity renderer tokens, normal WM_CLOSE, zero exit, all required shutdown events and no orphan processes; neither run required forced cleanup. Ephemeral origin ports/process IDs do not form part of repeatable fixture expectations.

Raw local evidence: `build/fixture-browser/browser-report-seed004.json`, `browser-report-seed005.json`, and each listed run's `result.json`/`lifecycle.jsonl`. These generated artifacts include machine-local profile/process diagnostics and stay outside committed source; this document records sanitized acceptance facts. No global/network configuration was changed. Final Node rerun passed 2/2 (337 ms).

Future extraction/automation APIs remain unimplemented and untested. Per-agent token usage is unavailable to this worker and is not estimated.
