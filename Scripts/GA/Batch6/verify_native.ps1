param([string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8', [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$ProjectFile = Join-Path $ProjectRoot 'test_25d.uproject'
$RunId = [guid]::NewGuid().ToString('N')
$ReportPath = Join-Path $ProjectRoot "Saved/GAAuthoring/CompositionBatch6/$RunId/native"
$LogPath = Join-Path $ProjectRoot "Saved/Logs/Batch6-$RunId.log"
if (-not $SkipBuild) {
    & (Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat') test_25dEditor Win64 Development "-Project=$ProjectFile" -WaitMutex -NoHotReloadFromIDE
    if ($LASTEXITCODE -ne 0) { throw 'Build failed; refusing to test an old binary.' }
}
$Started = Get-Date
& (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') $ProjectFile -unattended -NullRHI -nosplash -nop4 `
    '-ExecCmds=Automation RunTests test_25d.Batch6' '-TestExit=Automation Test Queue Empty' `
    "-ReportExportPath=$ReportPath" "-abslog=$LogPath"
if ($LASTEXITCODE -ne 0) { throw "Test process failed: $LogPath" }
$File = Get-Item -LiteralPath (Join-Path $ReportPath 'index.json')
if ($File.LastWriteTime -lt $Started) { throw 'Stale test report.' }
$Report = Get-Content -Raw -LiteralPath $File.FullName | ConvertFrom-Json
$Tests = @($Report.tests | Where-Object fullTestPath -like 'test_25d.Batch6.*')
if ($Tests.Count -eq 0 -or $Report.failed -ne 0 -or $Report.notRun -ne 0) {
    throw "Missing or failed Batch6 tests: $ReportPath"
}
$Tests | Select-Object fullTestPath, state
Write-Output "Fresh native verification: $ReportPath"
Write-Output 'Native tests do not establish visual, menu, or remote-input acceptance.'
