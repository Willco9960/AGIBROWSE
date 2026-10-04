# Two-tier agent integration contract

Status: accepted architecture, 2026-10-02; runtime unimplemented. [ADR-002](../decisions/002-two-tier-agent-runtime.md) records the decision. [Existing interfaces](interfaces.md) remain authoritative for browser access. General browser workflows are the product goal; virtual trading is a later workflow built on the same guarded interface.

## Responsibilities and flow

| Component | Owns | Cannot do |
|---|---|---|
| External smartLLM | Major goal/strategy choices, decomposition, evidence requirements, permitted template parameters and replanning | Grant native scope, manufacture observations, certify dispatched business effects |
| SDK orchestrator | Strategy validation, deterministic candidate shortlist, rule evaluation, progress/fallback state and typed proposal validation | Treat page text or model confidence as authorization |
| Pluggable local provider | Choice among supplied candidate IDs, yes/no or bounded score; explicit abstention | Generate scripts, refs, prices, unrestricted action arguments or strategy changes |
| Deterministic executor | Bind approved parameters to current refs, issue one guarded action, inspect receipt/event/fresh observation | Replay uncertain effects, bypass host checks or rely on model-invented numbers |
| Native host/broker | Existing scope/identity/lease/dispatch checks, redaction, state/replay/resource bounds | Delegate permission authority to either model |

Flow: human goal → smartLLM typed strategy → current permitted observation → deterministic shortlist/rules → local typed decision → deterministic proposal validation/binding → host-validated action → receipt/events/fresh observation → verified progress or escalation. This runtime stays external to the native host/broker. Models are optional to human browsing; credentials remain outside model context.

## Typed strategy and decision contract

Task 031 will publish versioned closed schemas and valid/invalid vectors under schemas/agent/. The table is an implementation contract, not an existing schema implementation. TS and Python use the same vectors; unknown fields/types are rejected.

| Record | Required typed information |
|---|---|
| Strategy | schema_version, strategy_id/revision, goal, host-approved session/profile/tab/origin scope binding, validity/refresh conditions, operation templates with typed parameter sources, success/evidence predicates, numerical rules if relevant, and replanning conditions |
| Decision input | strategy identity, current observation_id plus tab/document/projection/revision identity, decision kind, bounded candidates, required coverage, validated fact references, and provider/runtime deadline |
| Candidate | candidate_id, observed target ref(s), supported public action type/template ID, origin, supplied semantic label/state and observation identity; no raw CDP ID or secret value |
| Decision output | same strategy/input identity, kind=choice/boolean/score/abstain, candidate_id or typed value, provider/code/checkpoint identity, calibration version, confidence with explicit calibrated/uncalibrated status, abstention/escalation reason |
| Execution record | strategy/decision/request IDs, observed evidence identity, deterministic rule result, chosen provider, host receipt/dispatch status, verification outcome and measured stage timings/usage |

Strategies are plans within already granted authority. They cannot widen platform permissions. Strategy updates create a new revision and invalidate pending decisions under the old revision. Operation templates use the existing closed action union; local outputs select/bind them rather than write arbitrary actions. Public MCP inputs remain the same five tools. Opaque secret/file refs resolve only through existing native approval/scope rules.

Success predicates describe observable progress; a model's assertion or a click receipt alone is not workflow success. Numeric rules use a constrained typed comparison/window specification, not embedded JavaScript or free-form predicates. Exact schema/version compatibility detail belongs to task 031 and state identity/validity to task 021.

## Shortlists, deterministic rules and execution

Build the shortlist from the current permitted semantic tree. Filter by strategy scope, role/name/state, supported action and coverage; retain original opaque refs and source identity. Size/input-token limits are runtime/provider configuration, not financial policy. Report omitted candidates and insufficient coverage. If the correct target is absent, expand/reobserve or escalate; never let the local model invent a substitute. A returned candidate_id must belong to that exact input and observation.

For price targets and spikes, parse observed/provider-supplied numeric facts deterministically with instrument, currency/unit, source time and evidence provenance. Reject ambiguous/missing/stale inputs. Calculate comparisons, deltas and windows in code using consistent units/decimal handling; do not ask a small model to guess prices, arithmetic or spike magnitude. SmartLLM/user strategy supplies the rule and parameters; this architecture selects no trading thresholds or quantity limits. Local inference can classify context or choose a permitted template after numerical rules have been evaluated.

Virtual buy/sell templates resolve through the observed paper interface and strategy-supplied quantities/parameters. Verify paper environment/account identity before preparing a mutation; missing/ambiguous paper status blocks it. Do not switch to a live account, funding flow or real-money order. This integration grants no live-finance authorization and invents no user-facing paper-trade risk caps. Profile/tab/origin/operation permissions, native approval requirements and runtime memory/queue/deadline bounds still apply.

Every step in a multi-step template uses a current observation. Immediately before dispatch, the host rechecks target/document identity, actionability, origin/scope and the human-priority lease. Replacement/staleness rejects or triggers reobservation; never click a similar replacement merely because its label matches. Human takeover cancels undispatched work and stops the local loop. Revoked access removes pending candidate state.

Completed browser actions require post-action evidence for the intended workflow effect. Sent/unknown execution status never triggers automatic replay; retrieve an existing receipt only by identical live-session request ID and payload. Unknown effects require reconciliation/human review, not a new request ID concealing uncertainty. Decisions, templates and models gain no raw JS/CDP authority.

## Local provider candidate and adoption gate

Verified 2026-10-02: primary [Laya repository](https://github.com/NandhaKishorM/laya) and [code license](https://github.com/NandhaKishorM/laya/blob/main/LICENSE) declare Apache-2.0. The [typed-decisions model card](https://huggingface.co/convaiinnovations/laya-typed-decisions) also declares Apache-2.0 and supports typed choice/score/yes-no decisions. These are upstream declarations, not an artifact pin or integration benchmark.

The card limits the checkpoint to four synthetic workflow domains, English inputs and small choice sets, and explicitly treats confidence as uncalibrated. Browser target selection/trading competence cannot be inferred. Local-provider adoption requires pinned code and weight revision/hash, separate code/weight/base/dependency license records, approved loader/artifact provenance, supported-OS resource tests and held-out target-domain calibration. No weights have been installed or approved by this document. Preserve a pluggable provider boundary and log which checkpoint/device/precision actually ran. Automatic router behavior must not silently select a different checkpoint.

## Confidence, fallback and loop control

Calibrate abstention thresholds on held-out browser tasks by decision kind, candidate count and domain/language, independently of training/tuning. Raw scores are not measured probabilities of successful action. Uncalibrated/out-of-domain decisions cannot qualify for confidence-gated local dispatch; use the validated deterministic path or escalate. Track calibration error, coverage and wrong-selection/false-action rates.

The loop state distinguishes observe, decide, dispatch, verify, replan, handoff and stopped. Low confidence, unsupported coverage, invalid proposals, provider failure or repeated no-progress escalates to smartLLM with bounded evidence. SmartLLM obtains fresh state before replan; it does not waive host policy. Shared attempt/deadline/no-progress budgets apply across local and strategic tiers so they cannot bounce indefinitely. These are runtime resource bounds, with explicit terminal reasons; they do not impose additional business risk limits. Human stop/lease loss and unknown effects end automated mutation until resolved.

## Roadmap implementation and acceptance

Continue sequentially from task 003 with all original dependencies/gates. This table adds two-tier deliverables inside existing owner modules; it does not replace per-card acceptance or claim completion.

| Task | Owner and concrete deliverable | Required evidence |
|---|---|---|
| 031 | schemas/: versioned strategy/input/candidate/decision/execution contracts and compatibility vectors | Closed-type validation; forged refs/candidates/scope and obsolete strategy vectors rejected; original browser schemas unchanged |
| 034 | packages/sdk-typescript: orchestrator/provider interface, shortlist/rules/executor/fallback state machine | General fixtures complete locally and via smartLLM fallback; stale target, takeover and unknown effects remain guarded |
| 035 | packages/sdk-python: equivalent runtime plus optional pinned Laya adapter/adoption record | Shared vectors and TS parity; held-out calibration, provider abstention/failure, offline/cold/warm behavior and resource bounds |
| 036 | skills/aibrowese-native: teach strategy versus bounded decision/execution, escalation and honest verification | Examples cover replanning, insufficient candidates/coverage, human control and uncertain effects |
| 037 | examples/: general browser workflow plus virtual-only target/spike/buy/sell demonstration | Clean fixture setup; deterministic numeric rules; paper identity verified; no real orders/embedded credentials |
| 061 | benchmarks/: matched human, LLM-only and hybrid harness/corpus | Same goals, semantic facts, permissions, hardware/network and success labels; record retrieval/inference/dispatch/verification separately |
| 063 | SDKs: context compaction preserving active strategy, observation validity, progress and uncertainty | Reset/reconnect/replan recovers without stale refs, duplicate effects or local/LLM escalation loops |
| 065 | benchmarks/ + acceptance suite: comparative integration gate | Held-out completion/false-action/abstention and latency/cost evidence, existing efficiency/completion criteria still pass |
| 068 | tests/security: adversarial strategy/shortlist/provider/page influence tests | Page text cannot alter strategy authority; forged local output cannot widen scope/access secrets, replay unknown effects or cross paper/live boundary |

Tasks 003/006/007/009/010/015/021/026 remain prerequisites for the relevant guarded runtime behaviors. Laya adaptation is conditional work within task 035, not a reason to bypass their gates. Detailed security/state contracts stay with 006/021.

## Comparative test design

Use a frozen general-browser corpus plus deterministic virtual-account fixtures with known numbers, delayed updates, replaced targets, missing coverage, stale quotes, human takeover and crash-after-dispatch. Human participants are real testers, never fabricated AI sessions. Match starting state/goal and permitted interfaces, disclose human practice and model prompt/provider versions, randomize run order and separate cold/warm local runs.

Measure complete-workflow p50/p95 latency and stage timings, completion/effect verification, incorrect-target/false-action rate, unresolved unknown effects, repeated-action rate, abstention/escalation/no-progress counts and coverage failures. Record actual major-model tokens/cost when available, local compute/RAM/VRAM/load costs, network delay and human active time separately; distinguish measured costs from estimates. Attribute retrieval errors separately from local decision and executor failures.

Task 061 freezes methodology/corpus/calibration split; task 065 publishes paired results and uncertainty against the human and LLM-only baselines, alongside all existing efficiency/security gates. A faster local forward pass alone is not a faster or safer workflow. This plan makes no speed, profit or universal-automation guarantee.
