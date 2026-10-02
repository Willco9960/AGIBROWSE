# Third-party notices

This file records third-party notices and development-tool licenses separately from the project license in `LICENSE`.

## CEF runtime

The optional native browser build uses CEF 154.0.33+ga03e714+chromium-154.0.8037.94, pinned in `tools/cef/cef.lock.json`. CEF is BSD licensed; the build copies the distribution's complete `LICENSE.txt` to `CEF-LICENSE.txt` beside the launcher. The distribution README is copied as `CEF-README.txt`. Chromium and its bundled dependencies have additional licenses available through `about:credits` in the browser. Upstream binaries and resources retain their names and contents; the sandbox bootstrap is copied byte-for-byte as `agi-browse-host.exe`.

Official source: https://github.com/chromiumembedded/cef/tree/a03e7146331fc5bd72784591e82df01e8007e17b. Distribution: https://cef-builds.spotifycdn.com/. Signing and end-user packaging are deferred to release tasks; this development build is not a signed release.

## Development tools

- TypeScript 6.0.3 is a development-only compiler dependency, licensed under the Apache License 2.0. Its upstream license text is distributed with the npm package at `node_modules/typescript/LICENSE.txt` after dependency installation. It is not bundled into the runtime packages by this scaffold.
- setuptools 80.9.0 is a development-only PEP 517 build backend, licensed under the MIT License. Its license text is distributed with the pinned Python package in `build/sdk-python-venv/Lib/site-packages/setuptools-80.9.0.dist-info/licenses/LICENSE`. It is not bundled into the runtime package by this scaffold.
