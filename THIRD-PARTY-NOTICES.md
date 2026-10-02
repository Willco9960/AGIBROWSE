# Third-party notices

This file records third-party notices and development-tool licenses separately from the project license in `LICENSE`.

No third-party runtime dependency is included in this scaffold. AIBROWESE-003 will record the selected CEF distribution and its applicable upstream notices before integration.

## Development tools

- TypeScript 6.0.3 is a development-only compiler dependency, licensed under the Apache License 2.0. Its upstream license text is distributed with the npm package at `node_modules/typescript/LICENSE.txt` after dependency installation. It is not bundled into the runtime packages by this scaffold.
- setuptools 80.9.0 is a development-only PEP 517 build backend, licensed under the MIT License. Its license text is distributed with the pinned Python package in `build/sdk-python-venv/Lib/site-packages/setuptools-80.9.0.dist-info/licenses/LICENSE`. It is not bundled into the runtime package by this scaffold.
