# M01 foundation acceptance

Accepted 2026-10-02 after a read-only milestone review assigned to GPT-6.1 Sol/high. The reviewer compared all five execution records with the canonical routes and inspected their evidence. No unchanged tests were rerun. Provider-confirmed model identity and per-task token costs remain unavailable.

The gate is satisfied: a Windows development browser build exists, architecture/contracts are documented, and execution records preserve the requested model/reasoning with honest provenance.

- 001: architecture, contracts and module ownership — [evidence](AIBROWESE-001.md).
- 002: package build commands and licensing — [evidence](AIBROWESE-002.md).
- 003: clean Windows native build and restricted-renderer lifecycle, source `62019d8`; exit0, no orphans or forced cleanup — [verified CI receipt](AIBROWESE-003-ci-acceptance.json).
- 004: actual native browser seeds004/005 each14/14 assertions, server2/2 and packaging — [evidence](AIBROWESE-004.md), source `658df4f`.
- 005: routing12/12 tests, receipt/prerequisite/fallback gates, append-only accounting and honest missing-receipt demonstration — [evidence](AIBROWESE-005.md), source `4d62d1d`.

Subsequent fixture/runner changes do not alter the CEF host, bootstrap or build implementation. The additive lifecycle URL option has actual task004 native evidence. Stale root/contributor status summaries were corrected after review.

This is a basic Chromium window and test/execution foundation. Human navigation controls, broker/MCP/SDK transports, the two-tier agent runtime and a production executor adapter remain future work. M01 acceptance makes no claim that M02 security or later release gates have passed.
