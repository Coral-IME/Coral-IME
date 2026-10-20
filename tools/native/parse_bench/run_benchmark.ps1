param(
    [Parameter(Mandatory = $true)][string]$RuntimeDir,
    [Parameter(Mandatory = $true)][string]$InputDir,
    [Parameter(Mandatory = $true)][string]$ResultDir,
    [string]$Executable = (Join-Path $PSScriptRoot '..\..\..\build\win-x64-release\coralpinyin\Release\coral_parse_bench.exe'),
    [ValidateRange(1, 1000)][int]$Size = 8,
    [ValidateRange(1, 20)][int]$Rounds = 3
)
$ErrorActionPreference = 'Stop'
$benchExecutable = (Resolve-Path -LiteralPath $Executable).Path
$benchRuntime = (Resolve-Path -LiteralPath $RuntimeDir).Path
$benchInput = (Resolve-Path -LiteralPath $InputDir).Path
$benchOutput = [System.IO.Path]::GetFullPath($ResultDir)
if ($benchOutput -eq $benchInput) { throw 'ResultDir must differ from InputDir.' }
New-Item -ItemType Directory -Path $benchOutput -Force | Out-Null
$benchFiles = @(Get-ChildItem -LiteralPath $benchInput -File |
    Where-Object { $_.Extension -in '.txt', '.csv' } |
    Sort-Object Name | ForEach-Object { $_.FullName })
if ($benchFiles.Count -eq 0) { throw 'No .txt or .csv input files found.' }
for ($benchRound = 1; $benchRound -le $Rounds; ++$benchRound) {
    $benchPrefix = Join-Path $benchOutput "run$benchRound"
    foreach ($benchSuffix in '.txt', '-progress.txt', '-details.csv', '-summary.csv') {
        if (Test-Path -LiteralPath ($benchPrefix + $benchSuffix)) {
            throw "Output already exists: $benchPrefix$benchSuffix. Choose a new ResultDir."
        }
    }
    & $benchExecutable $benchRuntime @benchFiles --size $Size --out ($benchPrefix + '-details.csv') --summary ($benchPrefix + '-summary.csv') 1> ($benchPrefix + '.txt') 2> ($benchPrefix + '-progress.txt')
    if ($LASTEXITCODE -ne 0) { throw "Benchmark round $benchRound failed. Read $benchPrefix-progress.txt." }
    Get-Content -LiteralPath ($benchPrefix + '.txt')
}
