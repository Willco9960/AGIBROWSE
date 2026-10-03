# AIBROWESE-005 routing and usage evidence

Implemented 2026-10-02. Scope: tools/runner, additive package scripts, this evidence and runner guide. External hosts still own model calls/credentials. No future browser/security protocol is implemented.

## Acceptance

The library and contributor CLI enforce the canonical approved model, reasoning, standard mode and declared fallback, reject unavailable combinations before launch, authenticate prerequisite completion through a trusted operator-owned adapter, hash cited evidence and require all criteria. The actual adapter receives the enforced route. Two verified focused failures require an explicit allowed handoff; three failures stop subsequent launch. Authoritative history belongs to the trusted adapter and cannot be reset by a caller-supplied empty history. A production adapter must serialize/persist execution; caller JSON alone cannot authenticate model identity or completion.

Launch/attempt/completion JSONL records append timestamps, run/attempt IDs, settings, model provenance, measured latency, outcome/errors and input/output/cached/reasoning counts. Missing provider model/tokens/rates remain null. Versioned supplied rates can compute cost with source/currency; no real dollar prices or subscription percentages are invented. Verified launcher settings and authenticated provider model are separate fields. Missing, contradictory, failed or unknown receipts and partial/unverified/changed acceptance evidence block Done.

The repository manifest copies all 90 approved routing assignments unchanged, adds original prerequisite edges/criteria, and incorporates the eight addendum entries plus nine task extensions in docs/contracts/two-tier-agent.md (including task 031). It contains no board credentials/IDs. Models, reasoning, plan IDs and sequence remain unchanged.

## Executable verification

`npm run test:runner`: 12/12 pass, Node v24.19.0, final run 257 ms. Adversarial cases cover missing/mismatched/unavailable settings, omitted/reordered prerequisites, forged/partial/changed evidence, invalid fallback/duplicate history, three-failure stop across routes, explicit same-route fallback, forged/missing/contradictory receipts, wrong run/attempt, failed/unknown execution, malformed usage, complete criterion gating, cost with missing versus supplied synthetic rates, preserved earlier audit lines and changed adapter pin. Test trust uses object identity, so JSON copied with `trusted:true` does not pass. Synthetic executor/provider records validate gates; they are not production execution evidence.

`node tools/runner/demo.mjs`: active AIBROWESE-005 gpt-6.1-sol/medium/standard configuration passes preflight using the coordinator-reviewed 004 evidence. The demonstration adapter visibly consumes that exact route; no current-task executor/provider receipt is available. Output therefore has `status:unverified`, `done:false`, `actualModel:null`, all four usage counts null and cost null, with direct Node exit 2. The CLI prints the temporary append-only audit location. npm may remap its nonzero exit to 1. Demo prerequisite trust is explicitly limited to previously reviewed 004; it cannot certify current completion. Coordinator captured demonstration output in build/runner-demo.log.

Implementation orchestration: parent explicitly selected gpt-6.1-sol/medium through collaboration launcher metadata. That is launcher-assigned evidence, not a provider-confirmed response model. Provider token accounting and task dollar cost are unavailable; they remain unknown. Account usage is recorded separately by the coordinator. Neither this file nor demo asserts that a configured model proves provider execution.

Initial test setup had a structuredClone callback arity error, corrected to a unary callback. A same-route regression initially picked a card without an approved fallback; corrected to an explicitly synthetic same-route manifest fixture. These were test setup repairs, not failed task execution attempts or model escalations. No implementation fallback was used.

See [runner guide](../runner-guide.md) for operator trust assumptions, request shape, adapter methods and exit codes. Run `npm run test:runner` to reproduce the verification in under two minutes.
