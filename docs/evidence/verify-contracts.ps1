param(
    [Parameter(Mandatory = $true)][string]$SourcePackage
)
$ErrorActionPreference = 'Stop'
$taskDocs = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskMap = Get-Content -Raw -LiteralPath (Join-Path $taskDocs 'module-map.json') | ConvertFrom-Json
$taskErrors = [System.Collections.Generic.List[string]]::new()
$taskExpected = @(1..90 | ForEach-Object { 'AIBROWESE-{0:D3}' -f $_ })
$taskActual = @($taskMap.tasks.id)
if ($taskActual.Count -ne 90 -or (@($taskActual | Select-Object -Unique).Count -ne 90)) { $taskErrors.Add('Expected 90 unique task IDs.') }
if (@(Compare-Object $taskExpected $taskActual).Count) { $taskErrors.Add('Task IDs do not cover 001 through 090.') }
$taskModules = @($taskMap.modules.id)
if (@($taskModules | Select-Object -Unique).Count -ne $taskModules.Count) { $taskErrors.Add('Duplicate module IDs.') }
foreach ($taskModule in $taskMap.modules) {
    if ([string]::IsNullOrWhiteSpace($taskModule.path) -or [System.IO.Path]::IsPathRooted($taskModule.path) -or $taskModule.path -match '(^|/)\.\.(/|$)') { $taskErrors.Add("Invalid reserved module path: $($taskModule.id)") }
}
$taskSourceRows = @{}
Get-Content -LiteralPath (Join-Path $SourcePackage 'approved-roadmap.md') | ForEach-Object {
    if ($_ -match '^\| \*\*\[(AIBROWESE-\d{3})\] (.+?)\*\* \|') { $taskSourceRows[$Matches[1]] = $Matches[2] }
}
if ($taskSourceRows.Count -ne 90) { $taskErrors.Add('Source roadmap does not contain 90 task rows.') }
for ($taskIndex = 0; $taskIndex -lt $taskMap.tasks.Count; $taskIndex++) {
    $taskRow = $taskMap.tasks[$taskIndex]
    if ($taskRow.id -ne $taskExpected[$taskIndex]) { $taskErrors.Add("Task order differs at $taskIndex.") }
    if ($taskRow.title -cne $taskSourceRows[$taskRow.id]) { $taskErrors.Add("Task title differs: $($taskRow.id)") }
    if ($taskRow.owner_module -notin $taskModules) { $taskErrors.Add("Missing primary owner: $($taskRow.id)") }
    foreach ($taskCollaborator in $taskRow.collaborator_modules) {
        if ($taskCollaborator -notin $taskModules) { $taskErrors.Add("Unknown collaborator: $taskCollaborator") }
    }
    $taskMilestone = 'M{0:D2}' -f ([int][Math]::Floor($taskIndex / 5) + 1)
    if ($taskRow.milestone -ne $taskMilestone) { $taskErrors.Add("Wrong milestone: $($taskRow.id)") }
}
foreach ($taskSource in $taskMap.source_sha256) {
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $SourcePackage $taskSource.path)).Hash -ne $taskSource.sha256) { $taskErrors.Add("Source hash changed: $($taskSource.path)") }
}
$taskManifestPath = Join-Path $taskDocs 'contracts/agent-tools.reference.json'
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $taskManifestPath).Hash -ne (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $SourcePackage 'agent-tools.json')).Hash) { $taskErrors.Add('Public input manifest differs from approved source.') }
$taskManifest = Get-Content -Raw -LiteralPath $taskManifestPath | ConvertFrom-Json
$taskToolNames = @('aibrowese_connect','aibrowese_observe','aibrowese_act','aibrowese_wait','aibrowese_disconnect')
if ($taskManifest.tools.Count -ne 5 -or @(Compare-Object $taskToolNames @($taskManifest.tools.name)).Count) { $taskErrors.Add('Five-tool public manifest differs.') }
$taskLinkCount = 0
foreach ($taskDoc in Get-ChildItem -LiteralPath $taskDocs -Filter '*.md' -Recurse -File) {
    foreach ($taskLink in [regex]::Matches((Get-Content -Raw -LiteralPath $taskDoc.FullName), '\[[^\]]+\]\(([^)\s]+)\)')) {
        $taskTarget = $taskLink.Groups[1].Value
        if ($taskTarget -match '^[a-zA-Z][a-zA-Z0-9+.-]*:' -or $taskTarget.StartsWith('#')) { continue }
        $taskLinkCount++
        $taskTargetPath = [System.IO.Path]::GetFullPath((Join-Path $taskDoc.DirectoryName ($taskTarget -split '#')[0]))
        if (-not (Test-Path -LiteralPath $taskTargetPath -PathType Leaf)) { $taskErrors.Add("Broken local link: $($taskDoc.Name) -> $taskTarget") }
    }
}
$taskReport = [ordered]@{
    task = 'AIBROWESE-001'
    checked_at_utc = [DateTime]::UtcNow.ToString('o')
    status = $(if ($taskErrors.Count) { 'failed' } else { 'passed' })
    task_count = $taskMap.tasks.Count
    module_count = $taskMap.modules.Count
    milestone_count = @($taskMap.tasks.milestone | Select-Object -Unique).Count
    local_links_checked = $taskLinkCount
    checks = @('90 unique ordered IDs and source titles', 'one valid primary owner and valid collaborators per task', 'five tasks per each of 18 milestones', 'reserved paths are repository-relative', 'source SHA256 hashes', 'byte-identical five-tool manifest', 'local Markdown file links')
    errors = @($taskErrors.ToArray())
    limitations = @('Documentation validation only; no runtime, build, supported-OS, sandbox, security or performance tests.', 'External links are upstream pointers; availability was not tested.')
}
$taskReport | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 -LiteralPath (Join-Path $PSScriptRoot 'AIBROWESE-001-checks.json')
$taskReport | ConvertTo-Json -Depth 5
if ($taskErrors.Count) { exit 1 }
