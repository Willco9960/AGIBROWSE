# AGI-BROWSE contributor guide

AGI-BROWSE is an early scaffold for a local, agent-controllable browser. The accepted process and security architecture is in [architecture.md](architecture.md); implementation must follow the contracts and ownership in that document. This repository does not yet implement browser behavior, MCP tools, or SDK protocol behavior.

## Prerequisites

- CMake 3.20 or newer and a C++17 compiler (Ninja is recommended).
- Node.js 20.11+ and npm for TypeScript package checks (`import.meta.dirname` is used by the package script).
- PowerShell 7+ or Bash for the documented `&&` command chaining.
- Python 3.10+ for the Python SDK wheel.
- Pinned CEF SDK is not part of this scaffold; AIBROWESE-003 owns its pin and integration.
- For the Python wheel, initialize an ignored local build environment once with a standard Python install that includes `venv`: `C:\path\to\python.exe -m venv build/sdk-python-venv` on Windows or `python3 -m venv build/sdk-python-venv` on POSIX, then `build/sdk-python-venv/Scripts/python.exe -m pip install setuptools==80.9.0` on Windows (`build/sdk-python-venv/bin/python -m pip install setuptools==80.9.0` on POSIX).

## Build each package

Run from the repository root. Each command builds or packages one named package; build products go under ignored `build/` or `dist/` directories.

1. Native browser host and separate broker: `cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/native`
2. TypeScript MCP bridge: `npm ci --ignore-scripts && npm run build:bridge`
3. TypeScript SDK: `npm ci --ignore-scripts && npm run build:sdk-ts`
4. Python SDK wheel: `build/sdk-python-venv/Scripts/python.exe -m pip wheel --cache-dir build/pip-cache --no-deps --no-build-isolation --wheel-dir dist/sdk-python packages/sdk-python`
5. Schema reference package: `npm run package:schema` (copies the planning input into `build/packages/schema`; it is not a canonical wire schema).
6. Fixture package: `npm run package:fixtures` (packages the fixture status document; deterministic browser fixture content is assigned to later tasks).
7. Documentation package: `npm run package:docs` (collects the complete `docs/` tree, including the module map, ADRs and linked evidence).

Run `npm ci --ignore-scripts` once before either TypeScript package build. `package-lock.json` pins the exact TypeScript compiler version and integrity hash.

Package commands are intentionally scaffold checks. They do not imply CEF, MCP transport, broker transport, or browser functionality. On Windows, use an installed compiler CMake can detect; MSVC is the supported release compiler, while the available GCC may be used to check scaffold compilation.

## Repository conventions

- Keep CEF in-process with the native browser host and the broker as a separate C++ executable.
- Treat `docs/contracts/agent-tools.reference.json` as immutable planning input; later canonical schemas belong in `schemas/`.
- Add upstream notices to `THIRD-PARTY-NOTICES.md`; keep the project Apache-2.0 license in `LICENSE`.
- Build outputs, dependency downloads, and local board state (`.kanbots/`) are ignored.
- Before implementing an interface, read [contracts/interfaces.md](contracts/interfaces.md), the reference manifest, [module-map.json](module-map.json), and the applicable ADR/task evidence.

## Current status

Scaffold only. Detailed protocol choices remain with their assigned tasks. CEF has not been integrated, and no public MCP tools or SDK operations are available yet.
