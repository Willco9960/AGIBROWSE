[CmdletBinding()]
param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../../build/windows-cef'),
    [string]$EvidenceDirectory = (Join-Path $PSScriptRoot '../../build/cef-lifecycle'),
    [int]$TimeoutSeconds = 45,
    [string]$FixtureUrl = '',
    [switch]$SecurityProbe,
    [switch]$PrivacyProbe,
    [switch]$TabProbe,
    [switch]$NavigationProbe,
    [switch]$ProfileProbe,
    [switch]$ProfileRestart,
    [string]$ProfileFixtureRoot = '',
    [ValidateSet('healthy','expired','corrupt')][string]$TransportStoreFixture = 'healthy'
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $PSScriptRoot 'assert-tab-evidence.ps1')
$exe = [IO.Path]::GetFullPath((Join-Path $BuildDirectory 'browser/Release/agi-browse-host.exe'))
$brokerExe = Join-Path (Split-Path -Parent $exe) 'agi-browse-broker.exe'
$evidence = [IO.Path]::GetFullPath($EvidenceDirectory)
New-Item -ItemType Directory -Force -Path $evidence | Out-Null
$run = Join-Path $evidence ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
$log = Join-Path $run 'lifecycle.jsonl'
$stdout = Join-Path $run 'stdout.txt'
$stderr = Join-Path $run 'stderr.txt'
$profile = Join-Path $run 'profile'
if ($ProfileProbe) {
    $isolatedRoot=[IO.Path]::GetFullPath($ProfileFixtureRoot)
    $marker=Join-Path $evidence 'profile-fixture.marker'
    if ((Split-Path -Parent $isolatedRoot) -ne $evidence -or (Split-Path -Leaf $isolatedRoot) -ne 'profiles' -or
        -not (Test-Path -LiteralPath $marker) -or (Get-Content -LiteralPath $marker -Raw).Trim() -ne 'AGI-BROWSE isolated profile fixture v1') {
        throw 'Profile probe requires the narrowly created test-profiles fixture root'
    }
    $profile=$isolatedRoot
}
if($TransportStoreFixture -ne 'healthy') {
    if($SecurityProbe){throw 'Renderer channel probe requires healthy transport; fallback test cannot claim private channel proof'}
    $fixtureTool=[IO.Path]::GetFullPath((Join-Path $BuildDirectory 'lib/transport/Release/agi-transport-tests.exe'))
    & $fixtureTool --transport-store-fixture $TransportStoreFixture (Join-Path $profile 'Transport/authority.dpapi')
    if($LASTEXITCODE){throw 'Protected transport store fixture creation failed'}
}
$url = ([Uri](Join-Path $root 'tests/fixtures/cef-lifecycle.html')).AbsoluteUri
if ($FixtureUrl) { $url = $FixtureUrl }
if ($PrivacyProbe) { $url = ([Uri](Join-Path $root 'tests/fixtures/privacy.html')).AbsoluteUri }
if ($TabProbe) { $url = ([Uri](Join-Path $root 'tests/fixtures/tabs.html')).AbsoluteUri }
if ($NavigationProbe) { $url = ([Uri](Join-Path $root 'tests/fixtures/browser-ui-a.html')).AbsoluteUri }
if ($TabProbe -and ($SecurityProbe -or $PrivacyProbe -or $TransportStoreFixture -ne 'healthy')) { throw 'Tab lifecycle probe requires its own healthy isolated fixture run' }
if ($NavigationProbe -and ($TabProbe -or $SecurityProbe -or $PrivacyProbe -or $TransportStoreFixture -ne 'healthy')) { throw 'Navigation UI probe requires its own healthy isolated fixture run' }
if ($ProfileProbe -and ($TabProbe -or $NavigationProbe -or $SecurityProbe -or $PrivacyProbe -or $TransportStoreFixture -ne 'healthy')) { throw 'Profile probe requires its own healthy isolated fixture run' }
if ($ProfileRestart -and -not $ProfileProbe) { throw 'Restart is only supported by the isolated profile fixture' }

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

if ($ProfileProbe -and -not ('CefProfileMenu.Native' -as [type])) {
Add-Type -TypeDefinition @'
using System;using System.Runtime.InteropServices;
namespace CefProfileMenu {
 public static class Native {
  [StructLayout(LayoutKind.Sequential)] struct Rect {public int left,top,right,bottom;}
  [StructLayout(LayoutKind.Sequential)] struct Gui {public uint size,flags;public IntPtr active,focus,capture,menuOwner,moveSize,caret;public Rect rect;}
  [StructLayout(LayoutKind.Sequential)] struct Key {public ushort vk,scan;public uint flags,time;public UIntPtr extra;}
  [StructLayout(LayoutKind.Sequential)] struct Mouse {public int x,y;public uint data,flags,time;public UIntPtr extra;}
  [StructLayout(LayoutKind.Explicit)] struct Union {[FieldOffset(0)] public Key key;[FieldOffset(0)] public Mouse mouse;}
  [StructLayout(LayoutKind.Sequential)] struct Input {public uint type;public Union value;}
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint process);
  [DllImport("user32.dll")] static extern bool GetGUIThreadInfo(uint thread,ref Gui info);
  [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] static extern IntPtr GetAncestor(IntPtr hwnd,uint flags);
  [DllImport("user32.dll")] static extern uint SendInput(uint count,Input[] input,int size);
  delegate bool WindowVisitor(IntPtr hwnd,IntPtr parameter);
  [DllImport("user32.dll")] static extern bool EnumWindows(WindowVisitor visitor,IntPtr parameter);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr hwnd,System.Text.StringBuilder name,int maximum);
  public static bool OwnedDialogPresent(uint expectedProcess) {
   bool found=false;int visited=0;
   bool completed=EnumWindows((hwnd,parameter)=>{
    if(++visited>1024)return false;
    uint process;if(GetWindowThreadProcessId(hwnd,out process)==0)return false;
    if(process==expectedProcess){var name=new System.Text.StringBuilder(16);if(GetClassName(hwnd,name,16)==0)return false;if(name.ToString()=="#32770"){found=true;return false;}}
    return true;
   },IntPtr.Zero);
   if(!completed&&!found)throw new InvalidOperationException("Native dialog observation unavailable");
   return found;
  }
  public static bool MenuReady(IntPtr expected) {
   uint process;uint thread=GetWindowThreadProcessId(expected,out process);Gui gui=new Gui();gui.size=(uint)Marshal.SizeOf<Gui>();
   return thread!=0&&GetGUIThreadInfo(thread,ref gui)&&(gui.flags&4)!=0&&gui.menuOwner==expected&&GetAncestor(GetForegroundWindow(),2)==expected;
  }
  public static int SelectCreate(IntPtr expected) {
   if(!MenuReady(expected))return 0;
   // The menu exposes one explicit &Create access key. Alphabetical access
   // keys choose the command directly, without assuming a highlighted item.
   Input[] input=new Input[2];
   for(int i=0;i<2;i++){input[i].type=1;input[i].value.key.vk=0x43;input[i].value.key.flags=(uint)(i==1?2:0);}
   return SendInput(2,input,Marshal.SizeOf<Input>())==2?1:-1;
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
$tabFixtureFailure = $null
$navigationUiFailureStage = $null
$profileFailureStage=$null;$profileFailureReason=$null;$profileSettingState=$null
$profileCookieFlushMask=$null
$profileStorageMatch=$null
$profileMenuInputDelivered=$false
$profileOwnedDialogPresent=$null;$profileHostAliveBeforeCleanup=$null
$failure = $null
$started = Get-Date
$arguments = @(
    "--url=$url", "--profile-dir=`"$profile`"", "--lifecycle-log=`"$log`"", '--require-fixture'
)
if ($SecurityProbe) { $arguments += '--ipc-renderer-test' }
if ($PrivacyProbe) { $arguments += '--privacy-renderer-test' }
if ($TabProbe) { $arguments += '--tab-lifecycle-test' }
if ($NavigationProbe) { $arguments += '--browser-ui-test' }
if ($ProfileProbe) { $arguments += '--profile-isolation-test' }
if ($ProfileRestart) { $arguments += '--profile-restart-test' }
# TabProbe requires a visible native surface for trusted physical mouse input.
# Hidden startup can override CEF's first ShowWindow(SW_SHOWNORMAL) request.
[System.Diagnostics.ProcessWindowStyle]$launchWindowStyle = if ($TabProbe -or $NavigationProbe -or $ProfileProbe) { 'Normal' } else { 'Hidden' }
$hostProcess = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru -WindowStyle $launchWindowStyle -WorkingDirectory $run -RedirectStandardOutput $stdout -RedirectStandardError $stderr
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
        if ($TabProbe -and (Get-TabFixtureFailure -Events $events)) { throw 'Host-native tab lifecycle fixture failed' }
        if ($NavigationProbe -and ($events | Where-Object event -eq browser_ui_probe_failed_stage)) { throw 'Native browser UI fixture failed at a closed stage' }
        if ($ProfileProbe -and ($events | Where-Object event -eq profile_probe_failed_stage)) { throw 'Native profile fixture failed at a closed stage' }
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
    if ($PrivacyProbe) {
        do {
            $events = @(Get-Content -LiteralPath $log | ForEach-Object { $_ | ConvertFrom-Json })
            $privacyReady = @($events | Where-Object event -eq privacy_fixture_paths_exercised).Count -gt 0
            if (-not $privacyReady) { Start-Sleep -Milliseconds 100 }
        } until ($privacyReady -or (Get-Date) -gt $deadline)
        if (-not $privacyReady) { throw 'Privacy fixture did not positively exercise all renderer paths' }
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
    if ($NavigationProbe) {
        $expectedSteps = @(1..9)
        do {
            $events = @(Get-Content -LiteralPath $log | ForEach-Object { $_ | ConvertFrom-Json })
            $failedStep = @($events | Where-Object event -eq browser_ui_probe_failed_stage)
            if ($failedStep.Count) { throw 'Native browser UI probe failed at a closed stage' }
            $steps = @($events | Where-Object event -eq browser_ui_probe_step | ForEach-Object { [int]$_.value })
            if ($steps.Count -lt 9 -and -not $hostProcess.HasExited) { Start-Sleep -Milliseconds 150 }
        } until ($steps.Count -ge 9 -or $hostProcess.HasExited -or (Get-Date) -gt $deadline)
        $orderedSteps=$steps.Count -eq $expectedSteps.Count
        if($orderedSteps){for($i=0;$i -lt $expectedSteps.Count;$i++){if($steps[$i] -ne $expectedSteps[$i]){$orderedSteps=$false;break}}}
        if (-not $orderedSteps) { throw 'Native browser UI proof steps were missing, duplicated, or out of order' }
    }
    if ($ProfileProbe) {
        $expectedProfileSteps=if($ProfileRestart){@(7..9)}else{@(1..6)}
        $nativeMenuInputSent=$false
        do {
            foreach($process in (Get-RunProcesses)) {
                $key=[string]$process.ProcessId
                $processes[$key]=[ordered]@{processId=$process.ProcessId;parentProcessId=$process.ParentProcessId;commandLine=$process.CommandLine;creationDate=$process.CreationDate}
                if($process.CommandLine -match '--type=renderer' -and -not $rendererTokens.ContainsKey($key)) {
                    $token=[CefLifecycle.Native]::Inspect($process.ProcessId) | ConvertFrom-Json
                    if($token.restricted -and $token.integrityRid -le 4096){$rendererTokens[$key]=$token}
                }
            }
            $events=@(Get-Content -LiteralPath $log | ForEach-Object { $_ | ConvertFrom-Json })
            if($events | Where-Object event -eq profile_probe_failed_stage){throw 'Native profile fixture failed at a closed stage'}
            if(-not $ProfileRestart -and -not $nativeMenuInputSent -and ($events | Where-Object event -eq profile_menu_requested)) {
                $owner=$events | Where-Object event -eq window_created | Select-Object -First 1
                if(-not $owner){throw 'Native profile menu owner missing'}
                $menuOwner=[IntPtr][long]$owner.value;$menuDeadline=(Get-Date).AddSeconds(3)
                while(-not [CefProfileMenu.Native]::MenuReady($menuOwner)) {if($hostProcess.HasExited -or (Get-Date) -gt $menuDeadline){throw 'Native profile menu owner did not become ready'};Start-Sleep -Milliseconds 20}
                # Dispatch exactly once. A partial/uncertain SendInput result
                # cannot be retried because the mnemonic may have had effect.
                if([CefProfileMenu.Native]::SelectCreate($menuOwner) -ne 1){throw 'Native profile keyboard delivery failed or became uncertain'}
                $nativeMenuInputSent=$true;$profileMenuInputDelivered=$true
            }
            $profileSteps=@($events | Where-Object event -eq profile_probe_step | ForEach-Object {[int]$_.value})
            if($profileSteps.Count -lt $expectedProfileSteps.Count -and -not $hostProcess.HasExited){Start-Sleep -Milliseconds 100}
        } until($profileSteps.Count -ge $expectedProfileSteps.Count -or $hostProcess.HasExited -or (Get-Date) -gt $deadline)
        if(($profileSteps -join ',') -ne ($expectedProfileSteps -join ',')){throw 'Profile proof steps missing, duplicated or out of order'}
        if(-not $ProfileRestart -and (-not $nativeMenuInputSent -or @($events | Where-Object event -eq profile_menu_requested).Count -ne 1 -or @($events | Where-Object event -eq profile_native_create_selected).Count -ne 1 -or @($events | Where-Object { $_.event -eq 'profile_native_menu_return' -and $_.value -eq 1 }).Count -ne 1 -or @($events | Where-Object event -eq profile_native_menu_return).Count -ne 1)){throw 'Actual native profile creation selection proof missing'}
        if($rendererTokens.Count -lt 3){throw 'Three isolated profile renderers were not positively observed after sandbox lockdown'}
    }
    $window = $events | Where-Object event -eq window_created | Select-Object -First 1
    if (-not $window -or -not $window.value) { throw 'Native Views window handle was not recorded' }
    # Hold the real fixture window open while measuring children, then send a
    # normal OS close request. No forced process termination can count as a pass.
    if (-not $TabProbe -and -not $NavigationProbe -and -not $ProfileProbe) {
        Start-Sleep -Seconds 1
        $result = [UIntPtr]::Zero
        if ([CefLifecycle.Native]::SendMessageTimeout([IntPtr][long]$window.value, 0x10,
            [UIntPtr]::Zero, [IntPtr]::Zero, 2, 5000, [ref]$result) -eq [IntPtr]::Zero) {
            throw 'Native WM_CLOSE request failed or timed out'
        }
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
    if($ProfileProbe) {
        $flushMasks=@($events | Where-Object event -eq profile_cookie_flush | ForEach-Object {[int]$_.value})
        if($flushMasks.Count -ne 4 -or $flushMasks[0] -ne 0 -or $flushMasks[-1] -ne 7){throw 'All three exact profile cookie flush completions are required'}
        for($flushIndex=1;$flushIndex -lt $flushMasks.Count;$flushIndex++) {
            $previousMask=$flushMasks[$flushIndex-1];$currentMask=$flushMasks[$flushIndex]
            if(($currentMask -band $previousMask) -ne $previousMask -or ($currentMask-$previousMask) -notin @(1,2,4)){throw 'Cookie flush completion mask duplicated or invalid'}
        }
        $flushComplete=$false
        foreach($event in $events) {
            if($event.event -eq 'profile_cookie_flush' -and $event.value -eq 7){$flushComplete=$true}
            if($event.event -eq 'browser_closed' -and -not $flushComplete){throw 'Browser closed before profile cookie backing-store completion'}
        }
    }
    if ($TabProbe) {
        Assert-TabEvidence -Events $events
    }
    if ($SecurityProbe) {
        foreach ($required in @('renderer_application_escape_blocked','renderer_private_handles_absent','page_native_api_absent')) {
            if (-not ($events | Where-Object event -eq $required)) { throw "Missing security event: $required" }
        }
        if (@($events | Where-Object event -eq renderer_privileged_message_rejected).Count -lt 8) { throw 'Renderer negative messages were not all rejected' }
    }
    if ($PrivacyProbe) {
        foreach ($required in @('page_console_suppressed','privacy_dialog_suppressed','privacy_fixture_paths_exercised','privacy_human_title_preserved')) {
            if (-not ($events | Where-Object event -eq $required)) { throw "Missing privacy event: $required" }
        }
    }
    $success = $true
} catch {
    $failure = $_.Exception.Message
    if($ProfileProbe -and (Test-Path -LiteralPath $log)) {
        $events=@(Get-Content -LiteralPath $log | ForEach-Object {$_ | ConvertFrom-Json})
        $stages=@($events | Where-Object event -eq profile_probe_failed_stage);$reasons=@($events | Where-Object event -eq profile_probe_failed_reason)
        $settingStates=@($events | Where-Object event -eq profile_setting_state)
        $parsedSettingState=0
        if($settingStates.Count -and [int]::TryParse([string]$settingStates[-1].value,[ref]$parsedSettingState) -and $parsedSettingState -ge 0 -and $parsedSettingState -le 511){$profileSettingState=$parsedSettingState}
        if($stages.Count -eq 1 -and $reasons.Count -eq 1 -and [int]$stages[0].value -ge 1 -and [int]$stages[0].value -le 9 -and [int]$reasons[0].value -ge 1 -and [int]$reasons[0].value -le 8) {
            $profileFailureStage=[int]$stages[0].value;$profileFailureReason=[int]$reasons[0].value
            [void]$hostProcess.WaitForExit(10000)
        }
    }
    if ($NavigationProbe -and (Test-Path -LiteralPath $log)) {
        $events = @(Get-Content -LiteralPath $log | ForEach-Object { $_ | ConvertFrom-Json })
        $failedSteps = @($events | Where-Object event -eq browser_ui_probe_failed_stage)
        $parsedUiStage = 0
        if ($failedSteps.Count -eq 1 -and [int]::TryParse([string]$failedSteps[0].value,[ref]$parsedUiStage) -and $parsedUiStage -ge 1 -and $parsedUiStage -le 9) {
            $navigationUiFailureStage=$parsedUiStage
            [void]$hostProcess.WaitForExit(10000)
            $uiFailureDeadline=(Get-Date).AddSeconds(10)
            do { $uiFailureRemaining=@(Get-RunProcesses); if($uiFailureRemaining.Count){Start-Sleep -Milliseconds 250} }
            until (-not $uiFailureRemaining.Count -or (Get-Date) -gt $uiFailureDeadline)
        }
    }
    if ($TabProbe -and (Test-Path -LiteralPath $log)) {
        $events = @(Get-Content -LiteralPath $log | ForEach-Object { $_ | ConvertFrom-Json })
        $tabFixtureFailure = Get-TabFixtureFailure -Events $events
        if ($tabFixtureFailure) {
            $failure = "Host-native tab lifecycle fixture failed: stage=$($tabFixtureFailure.stage); reason=$($tabFixtureFailure.reason)"
            # A rapid native fixture failure can outlive the renderer before
            # token sampling. Keep it failed and allow its existing Close path
            # to finish; absent renderer evidence never supplies a pass.
            [void]$hostProcess.WaitForExit(10000)
            $failureExitDeadline = (Get-Date).AddSeconds(10)
            do {
                $failureRemaining = @(Get-RunProcesses)
                if ($failureRemaining.Count) { Start-Sleep -Milliseconds 250 }
            } until (-not $failureRemaining.Count -or (Get-Date) -gt $failureExitDeadline)
        }
    }
} finally {
    if($ProfileProbe) {
        if(Test-Path -LiteralPath $log) {
            $flushEvents=@(Get-Content -LiteralPath $log | ForEach-Object {$_ | ConvertFrom-Json} | Where-Object event -eq profile_cookie_flush)
            $parsedFlushMask=0
            if($flushEvents.Count -and [int]::TryParse([string]$flushEvents[-1].value,[ref]$parsedFlushMask) -and $parsedFlushMask -ge 0 -and $parsedFlushMask -le 7){$profileCookieFlushMask=$parsedFlushMask}
            $storageEvents=@(Get-Content -LiteralPath $log | ForEach-Object {$_ | ConvertFrom-Json} | Where-Object event -eq profile_storage_match)
            $parsedStorageMatch=0
            if($storageEvents.Count -and [int]::TryParse([string]$storageEvents[-1].value,[ref]$parsedStorageMatch) -and ($parsedStorageMatch -shr 3) -in @(1..9)){$profileStorageMatch=$parsedStorageMatch}
        }
        $profileHostAliveBeforeCleanup=-not $hostProcess.HasExited
        if($profileHostAliveBeforeCleanup){try{$profileOwnedDialogPresent=[CefProfileMenu.Native]::OwnedDialogPresent([uint32]$hostProcess.Id)}catch{$profileOwnedDialogPresent=$null}}
    }
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
        tabLifecycleProbe = [bool]$TabProbe
        navigationUiProbe = [bool]$NavigationProbe
        profileIsolationProbe = [bool]$ProfileProbe
        profileRestartProbe = [bool]$ProfileRestart
        profileFailureStage=$profileFailureStage
        profileFailureReason=$profileFailureReason
        profileSettingState=$profileSettingState
        profileCookieFlushMask=$profileCookieFlushMask
        profileStorageMatch=$profileStorageMatch
        profileMenuInputDelivered=$profileMenuInputDelivered
        profileHostAliveBeforeCleanup=$profileHostAliveBeforeCleanup
        profileOwnedDialogPresent=$profileOwnedDialogPresent
        launchWindowStyle = $launchWindowStyle.ToString()
        tabFailureStage = $(if ($tabFixtureFailure) { $tabFixtureFailure.stage } else { $null })
        navigationUiFailureStage = $navigationUiFailureStage
        tabFailureReason = $(if ($tabFixtureFailure) { $tabFixtureFailure.reason } else { $null })
        tabCursorRelation = $(if ($tabFixtureFailure) { $tabFixtureFailure.cursorRelation } else { $null })
        tabCursorDestination = $(if ($tabFixtureFailure) { $tabFixtureFailure.cursorDestination } else { $null })
        tabCursorDestinationAvailability = $(if ($tabFixtureFailure) { $tabFixtureFailure.cursorDestinationAvailability } else { $null })
        tabNativeWindowState = $(if ($tabFixtureFailure) { $tabFixtureFailure.nativeWindowState } else { $null })
        tabNativeWindowStateAvailability = $(if ($tabFixtureFailure) { $tabFixtureFailure.nativeWindowStateAvailability } else { $null })
        tabNativeWindowCloak = $(if ($tabFixtureFailure) { $tabFixtureFailure.nativeWindowCloak } else { $null })
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $run 'result.json') -Encoding utf8
    $hostProcess.Dispose()
}
if (-not $success) { throw "Lifecycle test failed: $failure. Evidence: $run" }
Write-Host "PASS: local fixture loaded, restricted renderer observed, native window closed, no orphan processes. Evidence: $run"
