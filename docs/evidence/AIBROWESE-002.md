# AIBROWESE-002 evidence

Status: scaffold complete and package commands verified. No runtime browser, broker transport, MCP, or SDK protocol functionality is claimed.

## Changed files

- Root: `.gitattributes`, `.gitignore`, `CMakeLists.txt`, `README.md`, complete Apache-2.0 `LICENSE`, `THIRD-PARTY-NOTICES.md`, `package.json`, `package-lock.json`.
- Native: `apps/browser/CMakeLists.txt`, `apps/browser/src/main.cpp`, `apps/broker/CMakeLists.txt`, `apps/broker/src/main.cpp`.
- TypeScript: bridge and SDK package manifests, configs and scaffold entry points; root lock pins TypeScript 6.0.3 and its SHA-512 integrity.
- Python: `packages/sdk-python/pyproject.toml`, `setup.cfg`, `src/agi_browse_sdk/__init__.py`; standard setuptools PEP 517 backend pinned to 80.9.0.
- Shared/docs: `schemas/README.md`, `tests/fixtures/README.md`, `tools/package-scaffold.mjs`, `README.md`, `docs/README.md`, this evidence file.

## Verified package commands

- Native host and separate broker: `cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release` and `cmake --build build/native` both succeeded with CMake/Ninja and GNU C++ 15.2; outputs were `agi-browse-host.exe` and `agi-browse-broker.exe`. The available compiler is GCC; MSVC is not installed, so Windows release-compiler validation remains outstanding.
- MCP bridge: after `npm ci --ignore-scripts` succeeded, `npm run build:bridge` emitted `packages/mcp-bridge/dist/index.js` and `index.d.ts` using pinned TypeScript 6.0.3.
- TypeScript SDK: `npm run build:sdk-ts` emitted `packages/sdk-typescript/dist/index.js` and `index.d.ts` using pinned TypeScript 6.0.3.
- Python SDK: a repository-local `build/sdk-python-venv` was created and setuptools 80.9.0 installed. `build/sdk-python-venv/Scripts/python.exe -m pip wheel --cache-dir build/pip-cache --no-deps --no-build-isolation --wheel-dir dist/sdk-python packages/sdk-python` produced `agi_browse_sdk-0.0.0-py3-none-any.whl` (SHA-256 `6ebb10eb73ba8791a6194bc664d1f0e8585164e3e630a639e85765f8d3d22207`). Pip installed it into `build/sdk-python-install`; import printed `0.0.0 scaffold-only`.
- Schema package: `npm run package:schema` succeeded and copied the planning reference to `build/packages/schema`.
- Fixture package: `npm run package:fixtures` succeeded and copied the status document to `build/packages/fixtures`; no browser fixture behavior is claimed.
- Docs package: `npm run package:docs` succeeded and collected the complete docs tree (10 files), including `module-map.json`, ADR-001, and linked evidence. Schema and fixture package commands were rerun and succeeded after the packaging safety changes.

The task001 verifier was run once after these changes and passed: 90 tasks, 38 modules, 18 milestones, 14 local links, and no errors. `npm ci` and its package builds succeeded after approved network access to npm. CEF is not included. `THIRD-PARTY-NOTICES.md` tracks TypeScript 6.0.3 (Apache-2.0) and setuptools 80.9.0 (MIT) as development-only tools, separately from runtime notices; CEF notices are assigned to AIBROWESE-003. The project `LICENSE` contains the complete Apache-2.0 terms.
