[CmdletBinding()]
param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../../build/windows-cef'),
    [string]$EvidenceDirectory = (Join-Path $PSScriptRoot '../../build/cef-lifecycle'),
    [int]$TimeoutSeconds = 45,
    [string]$FixtureUrl = '',
    [switch]$SecurityProbe,
    [ValidateSet('healthy','expired','corrupt')][string]$TransportStoreFixture = 'healthy'
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$exe = [IO.Path]::GetFullPath((Join-Path $BuildDirectory 'browser/Release/agi-browse-host.exe'))
$brokerExe = Join-Path (Split-Path -Parent $exe) 'agi-browse-broker.exe'
$evidence = [IO.Path]::GetFullPath($EvidenceDirectory)
New-Item -ItemType Directory -Force -Path $evidence | Out-Null
$run = Join-Path $evidence ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
$log = Join-Path $run 'lifecycle.jsonl'
$profile = Join-Path $run 'profile'
if($TransportStoreFixture -ne 'healthy') {
    if($SecurityProbe){throw 'Renderer channel probe requires healthy transport; fallback test cannot claim private channel proof'}
    $fixtureTool=[IO.Path]::GetFullPath((Join-Path $BuildDirectory 'lib/transport/Release/agi-transport-tests.exe'))
    & $fixtureTool --transport-store-fixture $TransportStoreFixture (Join-Path $profile 'Transport/authority.dpapi')
    if($LASTEXITCODE){throw 'Protected transport store fixture creation failed'}
}
$url = ([Uri](Join-Path $root 'tests/fixtures/cef-lifecycle.html')).AbsoluteUri
if ($FixtureUrl) { $url = $FixtureUrl }

if (-not ('CefLifecycle.Native' -as [type])) {
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
namespace CefLifecycle {
 public static class Native {
  [DllImport("user32.dll", SetLastError=true)] public static extern IntPtr SendMessageTimeout(IntPtr hwnd, uint msg, UIntPtr w, IntPtr l, uint flags, uint timeout, out UIntPtr result);
  [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint access, bool inherit, uint id);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
  [DllImport("advapi32.dll", SetLastError=true)] static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
  [DllImport("advapi32.dll", SetLastError=true)] static extern bool GetTokenInformation(IntPtr token, int cls, IntPtr data, int length, out int required);
  [DllImport("advapi32.dll")] static extern bool IsTokenRestricted(IntPtr token);
  [DllImport("advapi32.dll")] static extern IntPtr GetSidSubAuthorityCount(IntPtr sid);
  [DllImport("advapi32.dll")] static extern IntPtr GetSidSubAuthority(IntPtr sid, uint index);
  public static string Inspect(uint id) {
   IntPtr process = OpenProcess(0x1000, false, id);
   if (process == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
   IntPtr token = IntPtr.Zero;
   try {
    if (!OpenProcessToken(process, 8, out token)) throw new Win32Exception(Marshal.GetLastWin32Error());
    int length;
    GetTokenInformation(token, 25, IntPtr.Zero, 0, out length);
    IntPtr data = Marshal.AllocHGlobal(length);
    try {
     if (!GetTokenInformation(token, 25, data, length, out length)) throw new Win32Exception(Marshal.GetLastWin32Error());
     IntPtr sid = Marshal.ReadIntPtr(data);
     uint count = Marshal.ReadByte(GetSidSubAuthorityCount(sid));
     int integrity = Marshal.ReadInt32(GetSidSubAuthority(sid, count - 1));
     return "{\"processId\":" + id + ",\"restricted\":" + (IsTokenRestricted(token) ? "true" : "false") + ",\"integrityRid\":" + integrity + "}";
    } finally { Marshal.FreeHGlobal(data); }
   } finally { if (token != IntPtr.Zero) CloseHandle(token); CloseHandle(process); }
  }
 }
}
'@
}

function Get-OwnedProcesses {
    @(Get-CimInstance Win32_Process -Filter "Name='agi-browse-host.exe' OR Name='agi-browse-broker.exe'" |
        Where-Object { $_.ExecutablePath -eq $exe -or $_.ExecutablePath -eq $brokerExe })
}
function Get-RunProcesses {
    $candidates = Get-OwnedProcesses
    foreach ($candidate in $candidates) {
        $key = [string]$candidate.ProcessId
        $known = $processes.ContainsKey($key) -and $processes[$key].creationDate -eq $candidate.CreationDate
        $parentKey = [string]$candidate.ParentProcessId
        if ($candidate.ProcessId -eq $hostProcess.Id -or $known -or
            $candidate.ParentProcessId -eq $hostProcess.Id -or $processes.ContainsKey($parentKey)) {
            $candidate
        }
    }
}
if ((Get-OwnedProcesses).Count) { throw 'Close the existing host from this build before its isolated lifecycle test' }
$processes = @{}
$rendererTokens = @{}
$success = $false
$cleanupRequired = $false
$failure = $null
$started = Get-Date
$arguments = @(
    "--url=$url", "--profile-dir=`"$profile`"", "--lifecycle-log=`"$log`"", '--require-fixture'
)
if ($SecurityProbe) { $arguments += '--ipc-renderer-test' }
$hostProcess = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru -WindowStyle Hidden
try {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        foreach ($process in (Get-RunProcesses)) {
            $processes[[string]$process.ProcessId] = [ordered]@{
                processId = $process.ProcessId; parentProcessId = $process.ParentProcessId
                commandLine = $process.CommandLine; creationDate = $process.CreationDate
            }
        }
        $events = if (Test-Path -LiteralPath $log) { @(Get-Content -LiteralPath $log | ForEach-Object { $_ | ConvertFrom-Json }) } else { @() }
        $ready = @($events | Where-Object event -eq fixture_ready).Count -gt 0
        if ($hostProcess.HasExited) { throw "Host exited before fixture readiness: $($hostProcess.ExitCode)" }
        if (-not $ready) { Start-Sleep -Milliseconds 250 }
    } until ($ready -or (Get-Date) -gt $deadline)
    if (-not $ready) { throw 'Local fixture did not execute renderer JavaScript before timeout' }
    if ($SecurityProbe) {
        do {
            $events = @(Get-Content -LiteralPath $log | ForEach-Object { $_ | ConvertFrom-Json })
            $probeReady = @($events | Where-Object event -eq renderer_application_escape_blocked).Count -gt 0
            if (-not $probeReady) { Start-Sleep -Milliseconds 100 }
        } until ($probeReady -or (Get-Date) -gt $deadline)
        if (-not $probeReady) { throw 'Real renderer application escape/private-handle probe did not pass' }
    }
    # Probe only after JavaScript ran: before lockdown a newly created process
    # may still carry its temporary startup token.
    foreach ($process in (Get-RunProcesses)) {
        if ($process.CommandLine -match '--type=renderer') {
            $rendererTokens[[string]$process.ProcessId] = [CefLifecycle.Native]::Inspect($process.ProcessId) | ConvertFrom-Json
        }
    }
    if (-not $rendererTokens.Count) { throw 'No renderer process was observed' }
    foreach ($token in $rendererTokens.Values) {
        if (-not $token.restricted -or $token.integrityRid -gt 4096) {
            throw "Renderer $($token.processId) lacks restricted low-integrity sandbox token"
        }
    }
    $window = $events | Where-Object event -eq window_created | Select-Object -First 1
    if (-not $window -or -not $window.value) { throw 'Native Views window handle was not recorded' }
    # Hold the real fixture window open while measuring children, then send a
    # normal OS close request. No forced process termination can count as a pass.
    Start-Sleep -Seconds 1
    $result = [UIntPtr]::Zero
    if ([CefLifecycle.Native]::SendMessageTimeout([IntPtr][long]$window.value, 0x10,
        [UIntPtr]::Zero, [IntPtr]::Zero, 2, 5000, [ref]$result) -eq [IntPtr]::Zero) {
        throw 'Native WM_CLOSE request failed or timed out'
    }
    if (-not $hostProcess.WaitForExit($TimeoutSeconds * 1000)) { throw 'Host did not shut down after WM_CLOSE' }
    if ($hostProcess.ExitCode -ne 0) { throw "Host exit code $($hostProcess.ExitCode)" }
    $deadline = (Get-Date).AddSeconds(10)
    do {
        $remaining = @(Get-RunProcesses)
        if ($remaining.Count) { Start-Sleep -Milliseconds 250 }
    } until (-not $remaining.Count -or (Get-Date) -gt $deadline)
    if ($remaining.Count) { throw 'CEF subprocesses remained after host exit' }
    $events = @(Get-Content -LiteralPath $log | ForEach-Object { $_ | ConvertFrom-Json })
    $requiredEvents=@('sandbox_bootstrap_verified','window_created','browser_created','fixture_ready','browser_closed','window_destroyed','message_loop_exited','shutdown_complete')
    if($TransportStoreFixture -eq 'healthy'){$requiredEvents+=@('private_broker_challenge_verified','private_broker_stopped')}
    else {
        $requiredEvents+='agent_transport_unavailable'
        if($events | Where-Object event -eq private_broker_challenge_verified){throw 'Unavailable transport incorrectly claimed a verified channel'}
    }
    foreach ($required in $requiredEvents) {
        if (-not ($events | Where-Object event -eq $required)) { throw "Missing lifecycle event: $required" }
    }
    if ($SecurityProbe) {
        foreach ($required in @('renderer_application_escape_blocked','renderer_private_handles_absent','page_native_api_absent')) {
            if (-not ($events | Where-Object event -eq $required)) { throw "Missing security event: $required" }
        }
        if (@($events | Where-Object event -eq renderer_privileged_message_rejected).Count -lt 8) { throw 'Renderer negative messages were not all rejected' }
    }
    $success = $true
} catch {
    $failure = $_.Exception.Message
} finally {
    $remaining = @(Get-RunProcesses)
    if ($remaining.Count) {
        $success = $false
        if (-not $failure) { $failure = 'Run-owned processes appeared again after the final exit check' }
        $cleanupRequired = $true
        $remaining | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    }
    [ordered]@{
        passed = $success; startedUtc = $started.ToUniversalTime().ToString('o'); durationSeconds = ((Get-Date) - $started).TotalSeconds
        executable = $exe; fixture = $url; hostExitCode = $(if ($hostProcess.HasExited) { $hostProcess.ExitCode } else { $null })
        observedProcesses = @($processes.Values); rendererTokens = @($rendererTokens.Values)
        orphanProcesses = @($remaining | Select-Object ProcessId, ParentProcessId, CommandLine)
        forcedCleanup = $cleanupRequired; failure = $failure
        transportStoreFixture = $TransportStoreFixture
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $run 'result.json') -Encoding utf8
    $hostProcess.Dispose()
}
if (-not $success) { throw "Lifecycle test failed: $failure. Evidence: $run" }
Write-Host "PASS: local fixture loaded, restricted renderer observed, native window closed, no orphan processes. Evidence: $run"
