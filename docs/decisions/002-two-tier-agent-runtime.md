# ADR-002: Strategic LLM with bounded local typed decisions

## Status

Accepted architecture, authorized by the user on 2026-10-02. Runtime integration is not implemented. Adoption of any local model checkpoint remains conditional on artifact/license review, calibration and end-to-end evidence.

## Date

2026-10-02

## Context

The browser must support general goal-driven workflows. A smartLLM should make major strategy decisions while a local alternative handles simple, repeated decisions rapidly. The initial trading use case is virtual/paper activity only; it follows the general browser integration rather than replacing it.

## Decision

Put the two-tier orchestrator in the external agent runtime using the TypeScript/Python SDKs. The smartLLM produces a versioned typed strategy: goal, scope, bounded operation templates, evidence requirements and replanning conditions. A pluggable local provider selects among a short list derived from valid semantic observations or returns a typed yes/no/score/abstention. It cannot invent references, action arguments, permissions or a new strategy.

Use deterministic code for numerical comparisons, rule/spike evaluation, argument binding, state reduction and dispatch preparation. Model output is a decision proposal, not execution authority. The browser host retains target/observation validation, permissions, human-priority leases, redaction, deduplication and honest unknown-outcome semantics. Public tools remain unchanged; no raw JS/CDP or trading-specific privilege is added.

Laya is the first local provider candidate. Code and model card currently declare Apache-2.0, but the typed-decisions card warns of narrow synthetic-domain specialization and uncalibrated confidence. Do not adopt/download/package weights by virtue of this ADR. Task 035 must pin code/checkpoint revisions, verify the licenses/artifacts and calibrate/evaluate held-out browser decisions first. Record the provider actually used; no silent checkpoint/provider fallback. See the dated sources and [integration contract](../contracts/two-tier-agent.md).

## Alternatives considered

LLM-only orchestration remains the comparison and fallback path; it spends major-model calls on repeated simple decisions. Fully local strategy planning lacks the requested strategic tier. Executing model-generated scripts bypasses the existing semantic boundary. Calling a small model to calculate prices/spikes makes numerical correctness depend on probabilistic text reasoning. The chosen design retains strategic planning and makes bounded decision/execution boundaries testable.

## Consequences

Candidate retrieval, local inference, deterministic rules and host execution need separate outcome/latency evidence. Confidence is calibrated on held-out target-domain data, never assumed from a score. Low confidence, out-of-domain input, missing coverage or model failure escalates conservatively; loop budgets prevent endless replanning. Human control and uncertain effects stop automatic action retries.

Virtual buys/sells are a later example, not live-finance authorization or a profit claim. Platform permissions and runtime resource bounds remain mandatory. This ADR adds no position/notional/loss/risk cap to the user's paper strategy. Human, LLM-only and hybrid comparisons must measure complete-workflow latency, cost, success and failure risk on the same corpus/hardware. Speed and profitability are not guaranteed.

Tasks 031/034/035/036/037/061/063/065/068 carry the integration and verification deliverables. Task 003 and subsequent prerequisite/milestone gates remain required; acceptance of this architecture does not mark any runtime card Done.
