$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../../tools/cef/assert-tab-evidence.ps1')
function New-Proof {
    $events = @(1..5 | ForEach-Object { [pscustomobject]@{ event = 'browser_created'; value = $_ } })
    $events += @('tab_fixture_visibility_adjusted','tab_fixture_click_issued','tab_fixture_click_acknowledged','tab_fixture_popup_requested','tab_fixture_visibility_restored','tab_fixture_popup_registered','tab_fixture_order_verified','tab_fixture_move_verified','tab_fixture_cancel_verified','tab_fixture_close_verified','tab_fixture_pending_expired','tab_fixture_resources_released') | ForEach-Object { [pscustomobject]@{ event = $_; value = 0 } }
    $events += @(1..5 | ForEach-Object { [pscustomobject]@{ event = 'browser_closed'; value = $_ } })
    $events
}
function Reject-Proof {
    param([object[]]$Events, [string]$Name)
    $rejected = $false
    try { Assert-TabEvidence -Events $Events } catch { $rejected = $true }
    if (-not $rejected) { throw "Invalid proof accepted: $Name" }
}
function Swap-ProofEvents {
    param([object[]]$Events, [string]$First, [string]$Second)
    $firstIndex = [array]::IndexOf($Events, @($Events | Where-Object event -eq $First)[0])
    $secondIndex = [array]::IndexOf($Events, @($Events | Where-Object event -eq $Second)[0])
    if ($firstIndex -lt 0 -or $secondIndex -lt 0) { throw 'Proof fixture lacks named event' }
    $swap = $Events[$firstIndex]; $Events[$firstIndex] = $Events[$secondIndex]; $Events[$secondIndex] = $swap
}
Assert-TabEvidence -Events @(New-Proof)
Reject-Proof -Events @((New-Proof) | Where-Object event -ne tab_fixture_click_acknowledged) -Name 'input issuance without trusted acknowledgment'
$events = @(New-Proof); Swap-ProofEvents $events 'tab_fixture_click_issued' 'tab_fixture_popup_requested'
Reject-Proof -Events $events -Name 'popup request before input issuance'
$events = @(New-Proof); Swap-ProofEvents $events 'tab_fixture_click_acknowledged' 'tab_fixture_popup_requested'
Assert-TabEvidence -Events $events
$events = @(New-Proof); $events += [pscustomobject]@{ event = 'tab_fixture_click_issued'; value = 0 }
Reject-Proof -Events $events -Name 'duplicate input attempt'
$events = @(New-Proof); $events[1].value = 1; @($events | Where-Object { $_.event -eq 'browser_closed' -and $_.value -eq 2 })[0].value = 1
Reject-Proof -Events $events -Name 'duplicate browser identities with matching multisets'
$events = @(New-Proof); @($events | Where-Object { $_.event -eq 'browser_closed' -and $_.value -eq 5 })[0].value = 6
Reject-Proof -Events $events -Name 'unmatched browser close'
$events = @(New-Proof); $events += [pscustomobject]@{ event = 'tab_fixture_failed_stage'; value = 1 }
Reject-Proof -Events $events -Name 'failure mixed with positive proof'
$events = @(New-Proof); $events += [pscustomobject]@{ event = 'tab_fixture_failed_reason'; value = 13 }
Reject-Proof -Events $events -Name 'native failure reason mixed with positive proof'
$reasonNames = @('lifecycle_state','deadline','window_missing','window_unregistered','window_mismatch','screen_conversion','tab_window_changed','engine_changed','context_changed','retained_window_changed','cursor_unavailable','cursor_target_missing','cursor_root_mismatch')
for ($i = 1; $i -le 13; $i++) {
    $failureEvents = @([pscustomobject]@{ event = 'tab_fixture_failed_stage'; value = 0 }, [pscustomobject]@{ event = 'tab_fixture_failed_reason'; value = $i })
    $failure = Get-TabFixtureFailure -Events $failureEvents
    if ($failure.stage -ne 0 -or $failure.reason -ne $reasonNames[$i-1]) { throw "Closed failure mapping wrong: $i" }
}
foreach ($invalid in @('SENTINEL_PAGE_TEXT', 14)) {
    $failure = Get-TabFixtureFailure -Events @([pscustomobject]@{ event = 'tab_fixture_failed_reason'; value = $invalid })
    if ($failure.reason -ne 'unknown') { throw 'Unknown failure payload escaped closed reason mapping' }
}
if (Get-TabFixtureFailure -Events @()) { throw 'Empty evidence manufactured a failure' }
$failure = Get-TabFixtureFailure -Events @([pscustomobject]@{ event = 'tab_fixture_failed_stage'; value = 99 })
if ($null -ne $failure.stage) { throw 'Unknown failure payload escaped closed stage mapping' }
$events = @(New-Proof); $events += [pscustomobject]@{ event = 'tab_fixture_cursor_relation'; value = 0 }
Reject-Proof -Events $events -Name 'native failure relation mixed with positive proof'
for ($mask = 0; $mask -le 7; $mask++) {
    $failure = Get-TabFixtureFailure -Events @([pscustomobject]@{ event = 'tab_fixture_cursor_relation'; value = $mask })
    $relation = $failure.cursorRelation
    if ($relation.expectedIsRoot -ne [bool]($mask -band 1) -or
        $relation.normalizedRootsMatch -ne [bool]($mask -band 2) -or
        $relation.targetBelongsToExpected -ne [bool]($mask -band 4)) { throw "Closed cursor relation mapping wrong: $mask" }
}
foreach ($invalid in @('SENTINEL_OS_DATA', 8)) {
    $failure = Get-TabFixtureFailure -Events @([pscustomobject]@{ event = 'tab_fixture_cursor_relation'; value = $invalid })
    if ($null -ne $failure.cursorRelation) { throw 'Unknown payload escaped closed cursor relation mapping' }
}
foreach ($diagnostic in @('tab_fixture_cursor_destination','tab_fixture_cursor_destination_unavailable')) {
    $events = @(New-Proof); $events += [pscustomobject]@{ event = $diagnostic; value = 0 }
    Reject-Proof -Events $events -Name 'cursor destination failure diagnostic mixed with positive proof'
}
for ($mask = 0; $mask -le 15; $mask++) {
    $failure = Get-TabFixtureFailure -Events @([pscustomobject]@{ event = 'tab_fixture_cursor_destination'; value = $mask })
    $destination = $failure.cursorDestination
    if ($failure.cursorDestinationAvailability -ne 'observed' -or
        $destination.requestedPointInVirtualDesktop -ne [bool]($mask -band 1) -or
        $destination.requestedPointInExpectedWindow -ne [bool]($mask -band 2) -or
        $destination.requestedPointOwnedByExpectedRoot -ne [bool]($mask -band 4) -or
        $destination.actualCursorAtRequestedPoint -ne [bool]($mask -band 8)) { throw "Closed cursor destination mapping wrong: $mask" }
}
foreach ($invalid in @('SENTINEL_OS_DATA', -1, 16, [uint64]::MaxValue)) {
    $failure = Get-TabFixtureFailure -Events @([pscustomobject]@{ event = 'tab_fixture_cursor_destination'; value = $invalid })
    if ($null -ne $failure.cursorDestination -or $failure.cursorDestinationAvailability -ne 'unknown') { throw 'Unknown payload escaped closed destination mapping' }
}
$failure = Get-TabFixtureFailure -Events @([pscustomobject]@{ event = 'tab_fixture_cursor_destination_unavailable'; value = 0 })
if ($null -ne $failure.cursorDestination -or $failure.cursorDestinationAvailability -ne 'unavailable') { throw 'Unavailable geometry manufactured an observation' }
$failure = Get-TabFixtureFailure -Events @([pscustomobject]@{ event = 'tab_fixture_cursor_destination_unavailable'; value = 1 })
if ($null -ne $failure.cursorDestination -or $failure.cursorDestinationAvailability -ne 'unknown') { throw 'Unavailable marker accepted numeric payload' }
$failure = Get-TabFixtureFailure -Events @([pscustomobject]@{ event = 'tab_fixture_failed_reason'; value = 13 })
if ($null -ne $failure.cursorDestination -or $failure.cursorDestinationAvailability -ne 'unreported') { throw 'Missing destination diagnostic manufactured an observation' }
$failure = Get-TabFixtureFailure -Events @([pscustomobject]@{ event = 'tab_fixture_cursor_destination'; value = 15 }, [pscustomobject]@{ event = 'tab_fixture_cursor_destination_unavailable'; value = 0 })
if ($null -ne $failure.cursorDestination -or $failure.cursorDestinationAvailability -ne 'unknown') { throw 'Contradictory geometry observations were accepted' }
$failure = Get-TabFixtureFailure -Events @([pscustomobject]@{ event = 'tab_fixture_cursor_destination'; value = 15 }, [pscustomobject]@{ event = 'tab_fixture_cursor_destination'; value = 15 })
if ($null -ne $failure.cursorDestination -or $failure.cursorDestinationAvailability -ne 'unknown') { throw 'Duplicate geometry observations were accepted' }
foreach ($visibilityProof in @('tab_fixture_visibility_adjusted','tab_fixture_visibility_restored')) {
    Reject-Proof -Events @((New-Proof) | Where-Object event -ne $visibilityProof) -Name 'missing native fixture visibility proof'
    $events = @(New-Proof); $events += [pscustomobject]@{ event = $visibilityProof; value = 0 }
    Reject-Proof -Events $events -Name 'duplicate native fixture visibility proof'
}
$events = @(New-Proof); Swap-ProofEvents $events 'tab_fixture_visibility_restored' 'tab_fixture_click_acknowledged'
Reject-Proof -Events $events -Name 'visibility restored before trusted handler acknowledgment'
$events = @(New-Proof); Swap-ProofEvents $events 'tab_fixture_visibility_restored' 'tab_fixture_popup_registered'
Reject-Proof -Events $events -Name 'visibility restoration delayed beyond subsequent lifecycle stages'
$nativeStateChecks = 0
function New-NativeFailure {
    param([string]$Event, [object]$Value)
    @([pscustomobject]@{ event = $Event; value = $Value },
      [pscustomobject]@{ event = 'tab_fixture_visibility_restored'; value = 0 },
      [pscustomobject]@{ event = 'tab_fixture_failed_reason'; value = 13 },
      [pscustomobject]@{ event = 'tab_fixture_failed_stage'; value = 0 })
}
foreach ($diagnostic in @('tab_fixture_native_window_state','tab_fixture_native_window_state_unavailable','tab_fixture_native_window_cloak')) {
    Reject-Proof -Events @((New-Proof) + [pscustomobject]@{ event = $diagnostic; value = 0 }) -Name 'native window failure diagnostic mixed with positive proof'
    $nativeStateChecks++
}
for ($mask = 0; $mask -le 63; $mask++) {
    $failure = Get-TabFixtureFailure -Events @(New-NativeFailure 'tab_fixture_native_window_state' $mask)
    $state = $failure.nativeWindowState
    if ($failure.nativeWindowStateAvailability -ne 'observed' -or
        $state.expectedNativeVisible -ne [bool]($mask -band 1) -or $state.expectedNativeEnabled -ne [bool]($mask -band 2) -or
        $state.expectedNativeIconic -ne [bool]($mask -band 4) -or $state.expectedNativeTopmost -ne [bool]($mask -band 8) -or
        $state.targetProcessIsHost -ne [bool]($mask -band 16) -or $state.foregroundRootIsExpected -ne [bool]($mask -band 32)) { throw "Closed native window mapping wrong: $mask" }
    $nativeStateChecks++
}
foreach ($invalid in @('SENTINEL_OS_DATA', -1, 64, [uint64]::MaxValue)) {
    $failure = Get-TabFixtureFailure -Events @(New-NativeFailure 'tab_fixture_native_window_state' $invalid)
    if ($null -ne $failure.nativeWindowState -or $failure.nativeWindowStateAvailability -ne 'unknown') { throw 'Invalid native state fabricated an observation' }
    $nativeStateChecks++
}
$failure = Get-TabFixtureFailure -Events @(New-NativeFailure 'tab_fixture_native_window_state_unavailable' 0)
if ($null -ne $failure.nativeWindowState -or $failure.nativeWindowStateAvailability -ne 'unavailable') { throw 'Unavailable native state fabricated an observation' }
$nativeStateChecks++
$failure = Get-TabFixtureFailure -Events @(New-NativeFailure 'tab_fixture_native_window_state_unavailable' 1)
if ($failure.nativeWindowStateAvailability -ne 'unknown') { throw 'Invalid unavailable marker accepted' }
$nativeStateChecks++
$failure = Get-TabFixtureFailure -Events @(New-NativeFailure 'tab_fixture_native_window_cloak' 0)
if ($null -ne $failure.nativeWindowState -or $failure.nativeWindowStateAvailability -ne 'unreported') { throw 'Missing native state fabricated an observation' }
$nativeStateChecks++
foreach ($extra in @('tab_fixture_native_window_state','tab_fixture_native_window_state_unavailable')) {
    $events = @(New-NativeFailure 'tab_fixture_native_window_state' 63)
    $events = @([pscustomobject]@{ event = $extra; value = 0 }) + $events
    $failure = Get-TabFixtureFailure -Events $events
    if ($null -ne $failure.nativeWindowState -or $failure.nativeWindowStateAvailability -ne 'unknown') { throw 'Duplicate/contradictory native state accepted' }
    $nativeStateChecks++
}
for ($cloak = 0; $cloak -le 2; $cloak++) {
    $failure = Get-TabFixtureFailure -Events @(New-NativeFailure 'tab_fixture_native_window_cloak' $cloak)
    if ($failure.nativeWindowCloak -ne @('unavailable','not_cloaked','cloaked')[$cloak]) { throw 'Closed cloak mapping wrong' }
    $nativeStateChecks++
}
foreach ($invalid in @('SENTINEL_OS_DATA', -1, 3, [uint64]::MaxValue)) {
    $failure = Get-TabFixtureFailure -Events @(New-NativeFailure 'tab_fixture_native_window_cloak' $invalid)
    if ($failure.nativeWindowCloak -ne 'unknown') { throw 'Invalid cloak status accepted' }
    $nativeStateChecks++
}
$events = @(New-NativeFailure 'tab_fixture_native_window_cloak' 1)
$events = @([pscustomobject]@{ event = 'tab_fixture_native_window_cloak'; value = 1 }) + $events
if ((Get-TabFixtureFailure -Events $events).nativeWindowCloak -ne 'unknown') { throw 'Duplicate cloak status accepted' }
$nativeStateChecks++
foreach ($invalidContext in @('wrong_reason','wrong_stage','after_restoration','after_failure')) {
    foreach ($diagnostic in @('tab_fixture_native_window_state','tab_fixture_native_window_cloak')) {
        $events = @(New-NativeFailure $diagnostic 1)
        switch ($invalidContext) {
            wrong_reason { $events[2].value = 1 }
            wrong_stage { $events[3].value = 1 }
            after_restoration { Swap-ProofEvents $events $diagnostic 'tab_fixture_visibility_restored' }
            after_failure { Swap-ProofEvents $events $diagnostic 'tab_fixture_failed_reason' }
        }
        $failure = Get-TabFixtureFailure -Events $events
        if ($null -ne $failure.nativeWindowState -or ($diagnostic -eq 'tab_fixture_native_window_state' -and $failure.nativeWindowStateAvailability -ne 'unknown') -or
            ($diagnostic -eq 'tab_fixture_native_window_cloak' -and $failure.nativeWindowCloak -ne 'unknown')) { throw 'Wrong-context native diagnostic accepted' }
        $nativeStateChecks++
    }
}
Write-Host "PASS: $(70 + $nativeStateChecks) tab fixture evidence checks"
