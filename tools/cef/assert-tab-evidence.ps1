function Get-TabFixtureFailure {
    param([AllowEmptyCollection()][object[]]$Events)
    if (-not ($Events | Where-Object { $_.event -in @('tab_fixture_failed','tab_fixture_failed_stage','tab_fixture_failed_reason') })) { return $null }
    $stage = $null
    $stages = @($Events | Where-Object event -eq tab_fixture_failed_stage)
    $parsedStage = 0
    if ($stages.Count -eq 1 -and [int]::TryParse([string]$stages[0].value, [ref]$parsedStage) -and $parsedStage -ge 0 -and $parsedStage -le 7) { $stage = $parsedStage }
    $names = @('unknown','lifecycle_state','deadline','window_missing','window_unregistered','window_mismatch','screen_conversion','tab_window_changed','engine_changed','context_changed','retained_window_changed','cursor_unavailable','cursor_target_missing','cursor_root_mismatch')
    $reason = 'unknown'
    $reasons = @($Events | Where-Object event -eq tab_fixture_failed_reason)
    $parsedReason = 0
    if ($reasons.Count -eq 1 -and [int]::TryParse([string]$reasons[0].value, [ref]$parsedReason) -and $parsedReason -ge 1 -and $parsedReason -le 13) { $reason = $names[$parsedReason] }
    [pscustomobject]@{ stage = $stage; reason = $reason }
}

function Assert-TabEvidence {
    param([Parameter(Mandatory)][object[]]$Events)
    if (Get-TabFixtureFailure -Events $Events) { throw 'Host-native tab lifecycle fixture failed' }
    $indices = @{}
    foreach ($required in @('tab_fixture_click_issued','tab_fixture_click_acknowledged','tab_fixture_popup_requested','tab_fixture_popup_registered','tab_fixture_order_verified','tab_fixture_move_verified','tab_fixture_cancel_verified','tab_fixture_close_verified','tab_fixture_pending_expired','tab_fixture_resources_released')) {
        $matches = @(for ($i = 0; $i -lt $Events.Count; $i++) { if ($Events[$i].event -eq $required) { $i } })
        if ($matches.Count -ne 1) { throw "Missing or duplicate tab fixture proof: $required" }
        $indices[$required] = $matches[0]
    }
    # Input issuance cannot stand in for a trusted handler, and that handler
    # cannot stand in for the actual CEF popup request/registration callbacks.
    # Title and popup notifications may arrive on different engine channels;
    # both must follow issuance and precede fixture registration, without
    # assuming a relative callback order between those two notifications.
    $sequence = @('tab_fixture_click_issued','tab_fixture_click_acknowledged','tab_fixture_popup_registered','tab_fixture_order_verified','tab_fixture_move_verified','tab_fixture_cancel_verified','tab_fixture_close_verified','tab_fixture_pending_expired','tab_fixture_resources_released')
    for ($i = 1; $i -lt $sequence.Count; $i++) {
        if ($indices[$sequence[$i-1]] -ge $indices[$sequence[$i]]) { throw "Out-of-order tab fixture proof: $($sequence[$i])" }
    }
    if ($indices['tab_fixture_popup_requested'] -le $indices['tab_fixture_click_issued'] -or
        $indices['tab_fixture_popup_requested'] -ge $indices['tab_fixture_popup_registered']) { throw 'Out-of-order native popup request' }
    $created = @($Events | Where-Object event -eq browser_created | ForEach-Object value)
    $closed = @($Events | Where-Object event -eq browser_closed | ForEach-Object value)
    if ($created.Count -ne 5 -or $closed.Count -ne 5 -or
        @($created | Sort-Object -Unique).Count -ne 5 -or @($closed | Sort-Object -Unique).Count -ne 5 -or
        (Compare-Object ($created | Sort-Object) ($closed | Sort-Object))) { throw 'Five distinct actual CEF browser creations must match five graceful browser closes' }
}
