function Get-TabFixtureFailure {
    param([AllowEmptyCollection()][object[]]$Events)
    if (-not ($Events | Where-Object { $_.event -in @('tab_fixture_failed','tab_fixture_failed_stage','tab_fixture_failed_reason','tab_fixture_cursor_relation','tab_fixture_cursor_destination','tab_fixture_cursor_destination_unavailable','tab_fixture_native_window_state','tab_fixture_native_window_state_unavailable','tab_fixture_native_window_cloak') })) { return $null }
    $stage = $null
    $stages = @($Events | Where-Object event -eq tab_fixture_failed_stage)
    $parsedStage = 0
    if ($stages.Count -eq 1 -and [int]::TryParse([string]$stages[0].value, [ref]$parsedStage) -and $parsedStage -ge 0 -and $parsedStage -le 7) { $stage = $parsedStage }
    $names = @('unknown','lifecycle_state','deadline','window_missing','window_unregistered','window_mismatch','screen_conversion','tab_window_changed','engine_changed','context_changed','retained_window_changed','cursor_unavailable','cursor_target_missing','cursor_root_mismatch')
    $reason = 'unknown'
    $reasons = @($Events | Where-Object event -eq tab_fixture_failed_reason)
    $parsedReason = 0
    if ($reasons.Count -eq 1 -and [int]::TryParse([string]$reasons[0].value, [ref]$parsedReason) -and $parsedReason -ge 1 -and $parsedReason -le 13) { $reason = $names[$parsedReason] }
    $cursorRelation = $null
    $relations = @($Events | Where-Object event -eq tab_fixture_cursor_relation)
    $parsedRelation = 0
    if ($relations.Count -eq 1 -and [int]::TryParse([string]$relations[0].value, [ref]$parsedRelation) -and $parsedRelation -ge 0 -and $parsedRelation -le 7) {
        $cursorRelation = [pscustomobject]@{
            expectedIsRoot = [bool]($parsedRelation -band 1)
            normalizedRootsMatch = [bool]($parsedRelation -band 2)
            targetBelongsToExpected = [bool]($parsedRelation -band 4)
        }
    }
    $cursorDestination = $null
    $destinationAvailability = 'unreported'
    $destinations = @($Events | Where-Object event -eq tab_fixture_cursor_destination)
    $unavailable = @($Events | Where-Object event -eq tab_fixture_cursor_destination_unavailable)
    $parsedDestination = 0
    if ($destinations.Count -or $unavailable.Count) { $destinationAvailability = 'unknown' }
    if ($destinations.Count -eq 1 -and $unavailable.Count -eq 0 -and
        [int]::TryParse([string]$destinations[0].value, [ref]$parsedDestination) -and $parsedDestination -ge 0 -and $parsedDestination -le 15) {
        $destinationAvailability = 'observed'
        $cursorDestination = [pscustomobject]@{
            requestedPointInVirtualDesktop = [bool]($parsedDestination -band 1)
            requestedPointInExpectedWindow = [bool]($parsedDestination -band 2)
            requestedPointOwnedByExpectedRoot = [bool]($parsedDestination -band 4)
            actualCursorAtRequestedPoint = [bool]($parsedDestination -band 8)
        }
    } elseif ($destinations.Count -eq 0 -and $unavailable.Count -eq 1 -and
        [int]::TryParse([string]$unavailable[0].value, [ref]$parsedDestination) -and $parsedDestination -eq 0) {
        $destinationAvailability = 'unavailable'
    }
    $nativeState = $null
    $nativeStateAvailability = 'unreported'
    $cloakStatus = 'unreported'
    $stateEvents = @($Events | Where-Object event -eq tab_fixture_native_window_state)
    $stateUnavailable = @($Events | Where-Object event -eq tab_fixture_native_window_state_unavailable)
    $cloakEvents = @($Events | Where-Object event -eq tab_fixture_native_window_cloak)
    # Observations belong only to failure13 at stage0, before its restoration
    # and failure records. Invalid context/order cannot fabricate observations.
    $diagnostics = @($Events | Where-Object { $_.event -in @('tab_fixture_native_window_state','tab_fixture_native_window_state_unavailable','tab_fixture_native_window_cloak') })
    $contextValid = $stage -eq 0 -and $reason -eq 'cursor_root_mismatch'
    if ($contextValid) {
        $reasonIndex = [array]::IndexOf($Events, $reasons[0])
        $restorations = @($Events | Where-Object event -eq tab_fixture_visibility_restored)
        if ($restorations.Count -gt 1) { $contextValid = $false }
        foreach ($diagnostic in $diagnostics) {
            $index = [array]::IndexOf($Events, $diagnostic)
            if ($index -ge $reasonIndex -or ($restorations.Count -eq 1 -and $index -ge [array]::IndexOf($Events, $restorations[0]))) { $contextValid = $false }
        }
    }
    $parsedState = 0
    if ($stateEvents.Count -or $stateUnavailable.Count) { $nativeStateAvailability = 'unknown' }
    if ($contextValid -and $stateEvents.Count -eq 1 -and $stateUnavailable.Count -eq 0 -and
        [int]::TryParse([string]$stateEvents[0].value, [ref]$parsedState) -and $parsedState -ge 0 -and $parsedState -le 63) {
        $nativeStateAvailability = 'observed'
        $nativeState = [pscustomobject]@{
            expectedNativeVisible = [bool]($parsedState -band 1)
            expectedNativeEnabled = [bool]($parsedState -band 2)
            expectedNativeIconic = [bool]($parsedState -band 4)
            expectedNativeTopmost = [bool]($parsedState -band 8)
            targetProcessIsHost = [bool]($parsedState -band 16)
            foregroundRootIsExpected = [bool]($parsedState -band 32)
        }
    } elseif ($contextValid -and $stateEvents.Count -eq 0 -and $stateUnavailable.Count -eq 1 -and
        [int]::TryParse([string]$stateUnavailable[0].value, [ref]$parsedState) -and $parsedState -eq 0) {
        $nativeStateAvailability = 'unavailable'
    }
    $parsedCloak = 0
    if ($cloakEvents.Count) { $cloakStatus = 'unknown' }
    if ($contextValid -and $cloakEvents.Count -eq 1 -and
        [int]::TryParse([string]$cloakEvents[0].value, [ref]$parsedCloak) -and $parsedCloak -ge 0 -and $parsedCloak -le 2) {
        $cloakStatus = @('unavailable','not_cloaked','cloaked')[$parsedCloak]
    }
    [pscustomobject]@{ stage = $stage; reason = $reason; cursorRelation = $cursorRelation;
        cursorDestination = $cursorDestination; cursorDestinationAvailability = $destinationAvailability;
        nativeWindowState = $nativeState; nativeWindowStateAvailability = $nativeStateAvailability; nativeWindowCloak = $cloakStatus }
}

function Assert-TabEvidence {
    param([Parameter(Mandatory)][object[]]$Events)
    if (Get-TabFixtureFailure -Events $Events) { throw 'Host-native tab lifecycle fixture failed' }
    $indices = @{}
    foreach ($required in @('tab_fixture_visibility_adjusted','tab_fixture_click_issued','tab_fixture_click_acknowledged','tab_fixture_visibility_restored','tab_fixture_popup_requested','tab_fixture_popup_registered','tab_fixture_order_verified','tab_fixture_move_verified','tab_fixture_cancel_verified','tab_fixture_close_verified','tab_fixture_pending_expired','tab_fixture_resources_released')) {
        $matches = @(for ($i = 0; $i -lt $Events.Count; $i++) { if ($Events[$i].event -eq $required) { $i } })
        if ($matches.Count -ne 1) { throw "Missing or duplicate tab fixture proof: $required" }
        $indices[$required] = $matches[0]
    }
    # Input issuance cannot stand in for a trusted handler, and that handler
    # cannot stand in for the actual CEF popup request/registration callbacks.
    # Title and popup notifications may arrive on different engine channels;
    # both must follow issuance and precede fixture registration, without
    # assuming a relative callback order between those two notifications.
    $sequence = @('tab_fixture_visibility_adjusted','tab_fixture_click_issued','tab_fixture_click_acknowledged','tab_fixture_visibility_restored','tab_fixture_popup_registered','tab_fixture_order_verified','tab_fixture_move_verified','tab_fixture_cancel_verified','tab_fixture_close_verified','tab_fixture_pending_expired','tab_fixture_resources_released')
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
