# AGI-BROWSE

AGI-BROWSE is an early local browser with a separate agent broker. Its optional Windows x64 C++/CEF Views host now builds against a pinned upstream CEF distribution with the Chromium sandbox enabled. A local fixture open/close test verified restricted renderer tokens and complete process shutdown. The broker transport, MCP tools and SDK protocol behavior remain scaffolds. The accepted boundaries and target platforms are described in [docs/architecture.md](docs/architecture.md).

Task 003 remains pending until the clean Windows runner actually passes; the local result and remaining gate are recorded in [AIBROWESE-003 evidence](docs/evidence/AIBROWESE-003.md).

Start with the [contributor build guide](docs/README.md). Project code is licensed under Apache-2.0; upstream notices are tracked separately in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
