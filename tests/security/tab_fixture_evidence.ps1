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
Write-Host 'PASS: 70 tab fixture evidence checks'
