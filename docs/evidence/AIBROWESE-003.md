# AIBROWESE-003: resumable checkpoint

Status: IN PROGRESS. Acceptance: false. Saved 2026-10-02 before dependency changes.

## Verified context

- Work only in the linked checkout `work/agi-browse-dev`, branch `codex/aibrowese-execution`, starting at fd440f7.
- Task 003 owns a native C++/CEF Views Windows 11 x64 browser, using a pinned official upstream binary distribution and enabled Chromium sandbox.
- Existing browser executable is scaffold-only. Previous scaffold build success does not establish CEF integration or acceptance.
- Parent reports MSVC/cl/MSBuild/vswhere absent; GCC 15.2, CMake 4.2 and Ninja available. Toolchain detection still needs fresh verification and upstream compatibility research.
- No CEF distribution selected, downloaded, built or run at this checkpoint. No clean Windows runner or no-orphan lifecycle proof exists.

## Resume steps

1. Read architecture, interface contracts and ADR; inspect current browser/CMake scaffold and preserve other edits.
2. Verify official CEF Windows binary prerequisites, select an exact upstream version/archive with checksum, and tell parent the required Visual Studio components. Parent coordinates global toolchain installation.
3. Implement CEF Views host with subprocess dispatch, sandbox initialization, local fixture and deterministic close/shutdown; bootstrap pinned dependencies and build scripts.
4. Build with supported Windows toolchain; run local fixture open/close and verify renderer/browser process exit. Record actual outputs and unresolved failures.
5. Exercise the same bootstrap/build/lifecycle procedure on a clean Windows runner. Keep acceptance false until clean-runner and local runtime evidence are present.

## Constraints

No `--disable-sandbox`, no fake build/runtime receipts, no floating dependency pins. No commits, board writes or subagents from task 003. Do not install a duplicate global toolchain; coordinate with parent. After three failed fixes stop and name the doubtful assumption.

## Implementation increment (2026-10-02)

- Exact current stable upstream pin: CEF `154.0.33+ga03e714+chromium-154.0.8037.94`, CEF commit `a03e7146331fc5bd72784591e82df01e8007e17b`; Windows64 minimal archive 172788468 bytes. Official metadata SHA1 `fced8a0428b7ef68d0df5e288281c673798d3071` matches the downloaded file; independently recorded SHA256 `a211b8f10a99d1db49afd5b7d9fa7ab3222ebc6ea3026b31c3c9431e7d4a5ac2` pins all subsequent downloads.
- Official upstream CEF M138+ Windows sandbox integration requires its unmodified bootstrap launcher and a client DLL. This SDK supplies `Release/bootstrap.exe`; task 003 copies it byte-for-byte as `agi-browse-host.exe`. `RunWinMain` forwards the non-null upstream sandbox pointer to both subprocess dispatch and browser initialization; no replacement sandbox or unsandboxed fallback exists.
- Implemented CEF Views window/client, exact bootstrap version and sandbox compatibility checks, explicit rejection of unsafe sandbox/debug switches, normal OS close handling and CEF shutdown. Browser diagnostics record only fixed events and numeric IDs. A local fixture changes its title via JavaScript so readiness requires real renderer execution.
- Added checksum bootstrap/build PowerShell scripts, output LPAC read/execute ACLs following SDK macro, runtime token/lifecycle test, and Windows-2022 clean-runner workflow. PowerShell scripts parsed without errors. CMake/native build and runtime tests remain unexecuted at this checkpoint.
- Parent is installing VS2022 Build Tools v143 x64/x86 and Windows11 SDK26100. No ATL/Spectre dependency is required by this Views build. SDK CMake uses C++20 and static CRT `/MT` (the earlier C++17 assumption was corrected by inspecting this exact SDK).

### Sources verified during this run

- Sandbox rules: https://github.com/chromiumembedded/cef/blob/master/docs/sandbox_setup.md (M138+ bootstrap route).
- Client build prerequisites: https://github.com/chromiumembedded/cef-project (VS2022, CMake >=3.21).
- Exact official archive metadata: https://cef-builds.spotifycdn.com/index.json (retrieved 2026-10-02).
- Pinned SDK `README.txt`, `include/cef_sandbox_win.h`, `include/cef_version_info.h`, `cmake/cef_variables.cmake` and macros were read locally after archive verification.
- Pinned upstream `tests/cefsimple/cefsimple_win.cc` and `simple_app.cc` were inspected as integration examples; project code is original implementation.

### Next verification commands

Run `./tools/cef/build.ps1 -CacheDirectory build/cef-research` after parent confirms the Windows SDK install completed. Then run `./tools/cef/test-lifecycle.ps1`. Its passing result requires fixture readiness, a restricted renderer token with low-or-lower integrity, a real Views window, native WM_CLOSE, browser/window destruction, completed CefShutdown, exit code 0 and zero remaining executable-path processes. Forced cleanup never counts as a pass.

No Git remote/hosted Windows runner is configured. The workflow is a recipe only until it actually runs; a fresh isolated local build is useful additional evidence but does not certify clean-runner acceptance. Acceptance remains false.

## Toolchain verification checkpoint: stopped after three failed configure attempts

Parent completed official VS2022 Build Tools installation with exit 0 and no reboot: VS17.14.37710.0, toolset directory14.44.35207 (compiler19.44.35229.0), Windows11 SDK10.0.26100.0. Header `um/Windows.h` and library `um/x64/kernel32.lib` verified present.

1. MSYS CMake4.2 + Ninja + `cl.exe` by name: no compiler found on imported PATH.
2. Same CMake with absolute MSVC compiler: compiler identified as MSVC19.44, but CMake chose GNU `ld.exe` and could not find `rc`/`mt`. This disproved the assumption that the MSYS CMake build was a suitable MSVC driver.
3. Official native Kitware CMake4.2.0 Windows x64 ZIP, verified published SHA256 `cf35a516c4f5f4646b301e51c8e24b168cc012c3b1453b8f675303b54eb0ef45`, using a fresh `build/windows-cef-msvc` directory and VS2022 generator: CMake selected SDK10.0.26100.0 and launched MSBuild17.14.60, but MSBuild raised `MSB6001`, `System.ArgumentException: Item has already been added. Key in dictionary: 'PATH' Key being added: 'Path'`. No compiler identification or application compilation completed.

Doubtful assumption: the inherited Codex shell environment is case-normalized for MSBuild. Task003 stopped further fixes under the user three-failure rule and notified the coordinator. Next action is coordinator verification of a fresh, explicitly normalized Windows process environment; no fourth attempt is authorized by this checkpoint itself.

Logs: `build/cef-research/build.log`, `build/cef-research/build-msvc.log`, and `build/windows-cef-msvc/CMakeFiles/CMakeConfigureLog.yaml`. The latter contains the exact MSBuild environment exception. Official native CMake is at `build/cmake/cmake-4.2.0-windows-x86_64/bin/cmake.exe`; build.ps1 accepts `-CMakeExecutable`.

The CEF bootstrap script was executed against the previously verified archive and succeeded; PowerShell syntax checks passed. Native host, sandbox token/lifecycle checks and clean Windows runner are still PENDING. Acceptance remains false; no browser runtime process has been launched. Parent review corrections ensure late leftover processes fail the test and cleanup targets run-owned PID/creation identities.

## Authorized retry with child environment normalization

User explicitly authorized a new targeted repair cycle. `tools/cef/build.ps1` now launches native CMake through `ProcessStartInfo`, clears only the child environment, repopulates from an ordinal case-insensitive dictionary and canonicalizes PATH. It verifies zero duplicate names and exactly one PATH before launch. No user/machine environment is changed and environment values are not printed.

The first normalized configure PASSED (exit0) in fresh `build/windows-cef-normalized`, with official native CMake4.2.0, VS2022/MSVCv143 and SDK26100. Exact command: `./tools/cef/build.ps1 -CacheDirectory build/cef-research -BuildDirectory build/windows-cef-normalized -CMakeExecutable build/cmake/cmake-4.2.0-windows-x86_64/bin/cmake.exe`. Configure evidence: `build/windows-cef-normalized/configure.log`. Native/CEF-wrapper compilation started; result and runtime proof are pending at this increment. Acceptance remains false.

## Verified native build and local runtime (authorized normalized retry)

- Native configure and complete Release build PASSED (both exit0), official native CMake4.2.0, VS2022 MSVC19.44.35229.0, Windows SDK10.0.26100.0. Build directory `build/windows-cef-normalized`. CEF wrapper static library, client DLL and separate broker all built. The launcher SHA256 matched the pinned upstream `Release/bootstrap.exe` byte-for-byte.
- First lifecycle preflight under the restricted tool account could not enumerate Win32_Process (WMI AccessDenied); it failed before launching the browser. The same unchanged test was rerun with precisely scoped escalated user context and PASSED.
- Local fixture JavaScript executed and emitted `fixture_ready`. Browser PID10936 and renderer PIDs22112/452 were observed. Both renderer primary tokens were restricted, integrity RID0 (untrusted, below low). Views window creation and browser creation were logged, WM_CLOSE completed, browser/window destruction and CefShutdown completed, host exit0, orphan list empty and forcedCleanup=false. Duration2.143s.
- Committed-review evidence copies (not committed by task003): `docs/evidence/AIBROWESE-003-lifecycle.json` and `docs/evidence/AIBROWESE-003-lifecycle.jsonl`. Full raw run: `build/cef-lifecycle/a2e98beb17c44f0d8f0b8c1956714b9f/`.

Remaining acceptance gate: the clean hosted Windows runner has NOT run. `.github/workflows/windows-cef.yml` exists, but no Git remote is configured. Local build/runtime proof does not certify a clean runner. Acceptance remains false pending that actual run. No task004 advancement is justified yet.
