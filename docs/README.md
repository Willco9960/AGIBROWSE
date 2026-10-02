# AGI-BROWSE contributor guide

AGI-BROWSE is an early local browser with an optional working Windows CEF Views host. The accepted process and security architecture is in [architecture.md](architecture.md); implementation must follow its contracts and ownership. The broker transport, MCP tools and SDK protocol behavior remain scaffolds.

## Prerequisites

- CMake 3.21 or newer and a C++17 compiler for scaffold checks. The Windows CEF build requires native Windows CMake (not the MSYS build), VS2022 MSVC v143 x64/x86, a Windows SDK and C++20. Verified locally: official Kitware CMake4.2.0, MSVC19.44.35229.0 and SDK10.0.26100.0. ATL and Spectre components are not required.
- Node.js 20.11+ and npm for TypeScript package checks (`import.meta.dirname` is used by the package script).
- PowerShell 7+ for the Windows CEF scripts; PowerShell 7+ or Bash for the documented `&&` command chaining.
- Python 3.10+ for the Python SDK wheel. The Windows CEF SDK is downloaded and checksum verified by `tools/cef/bootstrap.ps1`; its exact version and archive hashes are in `tools/cef/cef.lock.json`.
- For the Python wheel, initialize an ignored local build environment once with a standard Python install that includes `venv`: `C:\path\to\python.exe -m venv build/sdk-python-venv` on Windows or `python3 -m venv build/sdk-python-venv` on POSIX, then `build/sdk-python-venv/Scripts/python.exe -m pip install setuptools==80.9.0` on Windows (`build/sdk-python-venv/bin/python -m pip install setuptools==80.9.0` on POSIX).

## Build each package

Run from the repository root. Each command builds or packages one named package; build products go under ignored `build/` or `dist/` directories.

1. Native scaffold host and separate broker: `cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/native`
2. TypeScript MCP bridge: `npm ci --ignore-scripts && npm run build:bridge`
3. TypeScript SDK: `npm ci --ignore-scripts && npm run build:sdk-ts`
4. Python SDK wheel: `build/sdk-python-venv/Scripts/python.exe -m pip wheel --cache-dir build/pip-cache --no-deps --no-build-isolation --wheel-dir dist/sdk-python packages/sdk-python`
5. Reference packages: `npm run package:schema`, `npm run package:fixtures` and `npm run package:docs` copy their inputs under `build/packages/`. The schema remains planning input; the fixture package contains its status document; the docs package includes task evidence. The CEF lifecycle test loads `tests/fixtures/cef-lifecycle.html` directly.

Run `npm ci --ignore-scripts` once before either TypeScript package build. `package-lock.json` pins the exact TypeScript compiler version and integrity hash.

Package commands are intentionally scaffold checks. They do not imply CEF, MCP transport, broker transport, or browser functionality. On Windows, use an installed compiler CMake can detect; MSVC is the supported release compiler, while the available GCC may be used to check scaffold compilation.

## Build and test the optional Windows CEF host

Run from the repository root using PowerShell7. Install [native Windows CMake](https://github.com/Kitware/CMake/releases/tag/v4.2.0) and VS2022 Build Tools with MSVCv143 x64/x86 and a Windows SDK. A normal checkout can use `./tools/cef/build.ps1` when native Windows CMake is on PATH; it downloads the pinned SDK into ignored `build/cef-sdk` and builds Release into `build/windows-cef`.

The exact command verified in this workspace uses its already downloaded native CMake and CEF cache:

```powershell
./tools/cef/build.ps1 -CacheDirectory build/cef-research -BuildDirectory build/windows-cef-normalized -CMakeExecutable build/cmake/cmake-4.2.0-windows-x86_64/bin/cmake.exe
./tools/cef/test-lifecycle.ps1 -BuildDirectory build/windows-cef-normalized
```

The build creates a client DLL and copies the upstream sandbox bootstrap byte-for-byte as `build/windows-cef-normalized/browser/Release/agi-browse-host.exe`. CMake/MSBuild runs in a child environment with case-insensitive variable deduplication; user and machine settings are unchanged. The lifecycle test needs a normal Windows user context with permission to query WMI process identities and tokens. Codex's restricted tool account returned WMI AccessDenied before launch; the same test passed in the user context. It requires renderer JavaScript execution, restricted low-or-lower-integrity renderer tokens, a Views window, normal WM_CLOSE, completed CEF shutdown and zero run-owned orphan processes. Forced cleanup fails the test.

For a manual local launch:

```powershell
./build/windows-cef-normalized/browser/Release/agi-browse-host.exe --url=https://example.com
```

The `.github/workflows/windows-cef.yml` workflow builds and runs the lifecycle test on Windows2022. Its first hosted attempt was cancelled after25 minutes inside the original silent SDK bootstrap, before CMake started. The updated bootstrap uses bounded native Windows curl/tar processes, verifies the same pinned checksums and logs download, checksum and extraction phases. CI preserves those logs and runs once per PR update; the overall limit remains25 minutes. The fresh local retrieval passed, but the updated hosted run still needs to pass. See [task003 evidence](evidence/AIBROWESE-003.md) and the [local runtime result](evidence/AIBROWESE-003-lifecycle.json); clean-runner acceptance remains pending.

## Repository conventions

- Keep CEF in-process with the native browser host and the broker as a separate C++ executable.
- Treat `docs/contracts/agent-tools.reference.json` as immutable planning input; later canonical schemas belong in `schemas/`.
- Add upstream notices to `THIRD-PARTY-NOTICES.md`; keep the project Apache-2.0 license in `LICENSE`.
- Build outputs, dependency downloads, and local board state (`.kanbots/`) are ignored.
- Before implementing an interface, read [contracts/interfaces.md](contracts/interfaces.md), the reference manifest, [module-map.json](module-map.json), and the applicable ADR/task evidence.

## Current status

The optional Windows CEF host has passed a native build and local lifecycle test. Task003 still requires an actual clean Windows runner pass. Detailed protocol choices remain with their assigned tasks; no public MCP tools or SDK operations are available yet.
