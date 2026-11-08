param([string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference = 'Stop'
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$RunId = Get-Date -Format 'yyyyMMdd-HHmmss'
$Report = Join-Path $ProjectRoot "Saved/Automation/AbilityTimeline/$RunId"
$Log = Join-Path $ProjectRoot "Saved/Logs/AbilityTimeline-$RunId.log"
& (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') `
    (Join-Path $ProjectRoot 'test_25d.uproject') -unattended -NullRHI -nosplash -nop4 `
    '-ExecCmds=Automation RunTests test_25d.AbilityTimeline+test_25d.Stander' `
    '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$Report" "-abslog=$Log" *> "$Log.console.txt"
if ($LASTEXITCODE -ne 0) { throw "Unreal test process failed: $Log" }
$Result = Get-Content -LiteralPath (Join-Path $Report 'index.json') -Raw | ConvertFrom-Json
$Result.tests | Select-Object testDisplayName,state
if ($Result.tests.Count -lt 12 -or @($Result.tests | Where-Object state -ne 'Success').Count -gt 0) {
    throw "Incomplete or failed ability timeline regression: $Report"
}
Write-Output "Passed $($Result.tests.Count) tests. Report: $Report"
