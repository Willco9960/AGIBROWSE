$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../../tools/cef/assert-tab-evidence.ps1')
function New-Proof {
    $events = @(1..5 | ForEach-Object { [pscustomobject]@{ event = 'browser_created'; value = $_ } })
    $events += @('tab_fixture_click_issued','tab_fixture_click_acknowledged','tab_fixture_popup_requested','tab_fixture_popup_registered','tab_fixture_order_verified','tab_fixture_move_verified','tab_fixture_cancel_verified','tab_fixture_close_verified','tab_fixture_pending_expired','tab_fixture_resources_released') | ForEach-Object { [pscustomobject]@{ event = $_; value = 0 } }
    $events += @(1..5 | ForEach-Object { [pscustomobject]@{ event = 'browser_closed'; value = $_ } })
    $events
}
function Reject-Proof {
    param([object[]]$Events, [string]$Name)
    $rejected = $false
    try { Assert-TabEvidence -Events $Events } catch { $rejected = $true }
    if (-not $rejected) { throw "Invalid proof accepted: $Name" }
}
Assert-TabEvidence -Events @(New-Proof)
Reject-Proof -Events @((New-Proof) | Where-Object event -ne tab_fixture_click_acknowledged) -Name 'input issuance without trusted acknowledgment'
$events = @(New-Proof); $swap = $events[5]; $events[5] = $events[7]; $events[7] = $swap
Reject-Proof -Events $events -Name 'popup request before input issuance'
$events = @(New-Proof); $swap = $events[6]; $events[6] = $events[7]; $events[7] = $swap
Assert-TabEvidence -Events $events
$events = @(New-Proof); $events += [pscustomobject]@{ event = 'tab_fixture_click_issued'; value = 0 }
Reject-Proof -Events $events -Name 'duplicate input attempt'
$events = @(New-Proof); $events[1].value = 1; $events[16].value = 1
Reject-Proof -Events $events -Name 'duplicate browser identities with matching multisets'
$events = @(New-Proof); $events[19].value = 6
Reject-Proof -Events $events -Name 'unmatched browser close'
$events = @(New-Proof); $events += [pscustomobject]@{ event = 'tab_fixture_failed_stage'; value = 1 }
Reject-Proof -Events $events -Name 'failure mixed with positive proof'
Write-Host 'PASS: 8 tab fixture evidence checks'
