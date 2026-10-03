# Fixtures

Run `npm run fixtures:serve -- 004`, then open `http://127.0.0.1:43110/`.

1. `/forms` has required name/email controls, native constraint validation, a real POST, server validation and a 303 receipt redirect.
2. `/navigation` supplies anchors/fragments, `/redirect` changes the primary page, and `/cross-redirect` navigates to the secondary origin.
3. `/dynamic` loads exactly three seeded items through explicit button steps; a fourth step reports Complete. No random timers drive fixture content.
4. The secondary origin is `http://127.0.0.1:43111`; `/foreign` supports an origin-checked postMessage. It deliberately has no CORS headers. Browser same-origin restrictions stay enabled.
5. `/proof` runs 14 browser conformance assertions in page scripts, records results to `/api/report`, and sets the lifecycle readiness title only when all pass. It proves browser fixture semantics, not agent APIs, trusted input events or future host permission enforcement.

Node 24 built-ins suffice; no install or network dependency is needed. `npm run test:fixtures` tests HTTP behavior and golden seed data; `npm run test:fixtures:browser -- 004` uses the existing pinned Windows CEF build at `build/windows-cef-normalized` and its lifecycle harness. The latter requires Windows process/token inspection permissions. It preserves the upstream sandbox, uses an isolated profile, requests normal native window close and fails if forced cleanup is needed. Evidence is written under `build/fixture-browser`.

`POST /api/reset` clears submissions, dynamic steps and proof reports without changing the seed. Restart with the same seed reproduces identical data and starts empty. A different 1–64 character seed changes item labels and receipt code. `expected.json` fixes the `004` golden dataset and all assertion names/outcomes. Its SHA-256 oracle was independently checked with .NET SHA256. Ephemeral port numbers and lifecycle run/profile IDs are diagnostic identities, not deterministic dataset outputs.

`GET /api/state` returns the dataset and current submissions/steps/reports. Only the primary origin exposes state and mutations. Both HTTP servers bind only `127.0.0.1`; unexpected Host and cross-origin Origin headers are rejected. The fixtures are local disposable test content; the primary test-control endpoints are not production authorization APIs. Use one fixture run per workflow because state is intentionally shared within a run. Fixed-port startup fails visibly when occupied; it never stops another process. Programmatic `startFixtures()` uses two distinct ephemeral ports and returns an explicit `close()` owner.

`npm run package:fixtures` packages pages, expected outcomes, fixture tools and the existing lifecycle harness under `build/packages/fixtures`. From that package, run `node tools/fixtures/server.mjs 004`. Native CEF binaries are not part of the fixture package. `cef-lifecycle.html` remains the task-003 default file fixture.
