param(
    [string]$SourcePath = (Join-Path $PSScriptRoot '../../src/platform/windows/coralpinyin/src/ui/settingswindow.cpp')
)

# Static guard for the native settings page. It deliberately checks wiring only:
# it cannot prove that a handler writes the right value or that TSF observes it.
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $SourcePath -PathType Leaf)) {
    Write-Error "Settings source not found: $SourcePath"
    exit 2
}
$source = [System.IO.File]::ReadAllText((Resolve-Path -LiteralPath $SourcePath).Path)

function Get-Section([string]$start, [string]$end) {
    $from = $source.IndexOf($start, [StringComparison]::Ordinal)
    $to = $source.IndexOf($end, [StringComparison]::Ordinal)
    if ($from -lt 0 -or $to -le $from) {
        throw "Cannot locate section $start .. $end"
    }
    return $source.Substring($from, $to - $from)
}

$create = Get-Section 'void SettingsWindow::createRoutinePage()' 'void SettingsWindow::switchPage('
$load = Get-Section 'void SettingsWindow::loadRoutinePage()' 'void SettingsWindow::onCommand('
$handle = Get-Section 'void SettingsWindow::onCommand(' 'void SettingsWindow::exportLexicon()'

$controls = [regex]::Matches($create, '\bb\.(check|radio|combo|button|toggle|slider|password)\s*\([^;]*?\b(ID_[A-Z0-9_]+)\b', 'Singleline')
if ($controls.Count -eq 0) { throw 'No settings controls found; check the parser.' }
$seen = @{}
$errors = [System.Collections.Generic.List[string]]::new()
foreach ($control in $controls) {
    $kind = $control.Groups[1].Value
    $id = $control.Groups[2].Value
    if ($seen.ContainsKey($id)) {
        $errors.Add("Duplicate control ID: $id")
        continue
    }
    $seen[$id] = $kind
    $handlerPattern = if ($kind -eq 'password') { "GetDlgItem\([^;]+\b$id\b" } elseif ($id -eq 'ID_S_SAVEKEY') { "\bid\s*==\s*$id\b" } elseif ($kind -eq 'slider') { "GetDlgCtrlID\(control\)\s*[!=]=\s*$id\b" } else { "\bcase\s+$id\s*:" }
    if ($handle -notmatch $handlerPattern) {
        $errors.Add("No onCommand case: $id")
    }
    if ($kind -ne 'Button' -and $load -notmatch "\b$id\b") {
        $errors.Add("No load/readback reference: $id")
    }
}

if ($errors.Count -gt 0) {
    foreach ($errorText in $errors) { Write-Output "[FAIL] $errorText" }
    exit 1
}
Write-Output "[PASS] $($seen.Count) settings controls have handlers; stateful controls have readback references."
exit 0
