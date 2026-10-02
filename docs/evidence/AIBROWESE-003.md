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

## Hosted CI cancellation diagnosis and bounded bootstrap fix (2026-10-02)

Actual hosted job: https://github.com/Willco9960/AGIBROWSE/actions/runs/37020033186/job/110880450675, head `d195180306eca43ca5609113ce04a737149891c6`. Job logs retrieved through authenticated GitHub API; credentials remained in memory and were not printed or stored. Full raw logs and job metadata: ignored `build/ci-37020033186/job.log` and `job.json`. Selected exact runner lines are preserved in `AIBROWESE-003-ci-cancelled.log`.

The build step started `2026-10-02T14:27:55.8758622Z`; its next output was cancellation at `14:52:52.4955508Z`. No `Verified child environment` appeared, which the build script emits immediately before CMake. Therefore the observed stall was inside the original SDK bootstrap, before compiler configuration. That bootstrap had an unbounded Invoke-WebRequest and silent checksum/extraction steps. The original logs cannot distinguish transfer from extraction or establish a specific upstream network cause. Runtime was skipped and no lifecycle artifact existed. The corresponding PR run37020127953 was also cancelled at the same head (coordinator verified).

Scoped fix: the exact pinned archive now downloads through native `%SystemRoot%/System32/curl.exe` with20s connection timeout,300s attempt timeout,32KiB/s minimum transfer guard over45s, one retry and600s retry budget. A parent process watchdog caps total retrieval at630s. Native System32tar extraction is bounded to90s. Timestamped phases and10s progress heartbeats are printed and retained in the SDK cache's `bootstrap.log`. Archive size, SHA256 and upstream SHA1 remain mandatory; the sandbox bootstrap and native application code are unchanged.

The workflow now separates SDK retrieval, native build and lifecycle steps, preserves SDK/configure/build logs even on failure, and uses PR-only plus manual dispatch with concurrency cancellation to avoid duplicate push/PR jobs. The job timeout remains25 minutes; it was not raised.

Validation: PowerShell parser passed; existing validated-cache bootstrap passed. A fresh actual download using `./tools/cef/bootstrap.ps1 -CacheDirectory build/cef-ci-bootstrap-test` passed:172788468 bytes, native curl18s, both checksums verified, native tar12s, SDK ready. Exact timestamped local log is preserved as `AIBROWESE-003-bootstrap.log`. The previously resolved tar command was System32tar, matching the final explicit path. Native compilation/lifecycle were not repeated because their implementation and pinned SDK were unchanged.

Next action: coordinator publishes this scoped fix and waits for an actual fresh hosted Windows job. Git origin and draft PR https://github.com/Willco9960/AGIBROWSE/pull/1 now exist; earlier no-remote notes above describe the historical checkpoint only. Updated CI has not passed yet. Acceptance remains false until its clean native build and sandbox/lifecycle result pass.

## First hosted repair failure: extraction watchdog, not download

Run https://github.com/Willco9960/AGIBROWSE/actions/runs/37057286911 failed in SDK retrieval. Exact job logs and uploaded bootstrap artifact were downloaded and inspected before proposing another fix. Artifact11248884518 uploaded successfully (no build or runtime receipt exists).

Hosted download completed in11s at19:56:06.514Z, and archive size/SHA256/upstreamSHA1 verification passed at19:56:07.303Z. Extraction began19:56:07.310Z. Ten-second heartbeats continued through81s; the parent watchdog terminated System32tar at90s and recorded `extraction exceeded its 90s process limit` at19:57:37.857Z. Therefore this failure is the extraction time budget, with no evidence of CDN restriction or bad archive. The old extraction heartbeat measured elapsed time only, so actual file growth/CPU progress was not captured.

The exact hosted bootstrap artifact is preserved as `AIBROWESE-003-ci-extraction-timeout.log`; its job identity and conclusion are in `AIBROWESE-003-ci-extraction-timeout.json`. Full raw job log/artifact remain under ignored `build/ci-37057286911/`. Build and runtime were skipped. Acceptance remains false. A second scoped extraction repair is being coordinated; no timeout, checksum or sandbox change has been applied at this checkpoint.

## Second scoped hosted repair: extraction budget and actual progress

Coordinator approved retaining native System32tar and allowing300s for extraction. Ten-second extraction heartbeats now report extracted file count, total written bytes and process CPU seconds. These are informational, so buffered writes or active CPU work do not produce an invented no-growth failure. The hard extraction deadline still stops a hung process.

The total download watchdog is reduced to330s, with150s per curl attempt, one retry and300s retry budget. This follows the observed11s hosted download and permits330s download +300s extraction plus checksums/setup inside the unchanged13-minute SDK step. The overall job limit remains25 minutes. Archive pin/size/SHA256/SHA1 and native sandbox/runtime code are unchanged; no alternate CDN or extractor was introduced.

Validation: updated PowerShell parser passed and existing validated-cache bootstrap passed with mandatory checksums. The173MB download and native compile/runtime were not repeated because this repair changes timing/diagnostics only; an actual clean hosted run remains required. This is the second hosted repair proposal following one failed hosted repair run. Parent owns publication and observes the next run; acceptance remains false.
