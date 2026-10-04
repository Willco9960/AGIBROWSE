# AIBROWESE-012 browser navigation controls

Status: **native UI implementation ready; clean Windows GUI acceptance pending**.

Each Alloy window now owns a native CEF Views root panel with a clickable tab strip, a navigation toolbar and a browser content panel. Selecting or closing a tab uses the existing host lifecycle; `+` creates a tab, the address field navigates on Enter, and Back, Forward and Reload/Stop call the active tab's real `CefBrowser` state. Ctrl+L focuses and selects the address field. The existing Ctrl+T, Ctrl+W and Ctrl+Tab handling remains in the browser keyboard handler.

Back and Forward enablement follows `CanGoBack()` and `CanGoForward()`. The address field follows the active browser's committed URL after a load completes. Reload changes to Stop while that active browser is loading. UI actions are bound to the native window identity and resolve its current active tab when invoked, so selecting a tab does not leave stale toolbar targets.

The browser Views are children of the content panel. Reorder, move, close and popup attachment preserve the existing browser View ownership and `GetWindow()` checks while the toolbar remains outside the website's renderer. No page API, CDP, agent action or permission grant was added.

The separate `-NavigationProbe` run uses two static local fixture pages. It enters page B through the address field and Enter, clicks the native Back, Forward and Reload controls, uses native Ctrl+L and Enter, creates a tab with Ctrl+T, clicks the source tab and closes the other tab. Each success event has one closed stage number from1 to9. The probe verifies the engine's current URL and history state internally, confirms a main-frame load after Reload, and requires actual active-tab loading and idle callbacks to match the Stop and Reload labels. It never writes page URLs or titles to the receipt. The probe does not exercise a slow-load Stop click. Its CI workflow runs after and separately from AIBROWESE-011 TabProbe, whose proof gates are preserved.

Local verification is limited to the pinned CEF host `ClCompile` target with warnings treated as errors, PowerShell parser validation, and the focused privacy diagnostic contract test. The quarantined launcher prevents local full packaging or GUI execution, so the new navigation probe still requires a clean Windows GUI run before acceptance.