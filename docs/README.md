# Architecture contracts

Task AIBROWESE-001 documents the approved architecture; no browser runtime is implemented.

1. Read [architecture](architecture.md) for ownership, trust boundaries, flow and platform scope.
2. Read [interfaces](contracts/interfaces.md) and the exact [MCP input manifest](contracts/agent-tools.reference.json) before implementing an interface.
3. Use the [module map](module-map.json) to scaffold reserved paths and find each task's owner.
4. Read [ADR-001](decisions/001-engine-and-process-boundaries.md) for accepted choices and deferred policies.
5. Check [task evidence](evidence/AIBROWESE-001.md) for the documentation gate.

Reserved paths are implementation destinations, not existing packages or working commands. Task 002 owns scaffolding and contributor build commands; task 003 owns the pinned, sandboxed Windows CEF build. Sources and hashes are recorded in the module map. The 2026-10-01 execution policy supersedes old model routing only. All original acceptance criteria and 18 milestone gates remain required, in task order 001–090.
