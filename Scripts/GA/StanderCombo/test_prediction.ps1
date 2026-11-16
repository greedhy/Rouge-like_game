param(
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8',
    [int]$Port = 17986,
    [switch]$IncludeProduction
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$project = Join-Path $root 'test_25d.uproject'
$editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$out = Join-Path $root ('Saved/GAAuthoring/StanderCombo/net-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$serverLog = Join-Path $out 'server.log'
$clientLog = Join-Path $out 'client.log'
$common = @('"' + $project + '"', '-game', '-unattended', '-NullRHI', '-nosplash', '-nosound', '-nop4', '-nosteam')
$server = $null
$client = $null

function Read-ProbeEvents([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    foreach ($line in Select-String -LiteralPath $Path -Pattern 'COMBO_PREDICTION') {
        if ($line.Line -match 'case=(\d+) side=(\w+) event=(\w+) stage=(\d+) key=(-?\d+) base=(-?\d+) cancelled=(\d+)') {
            [pscustomobject]@{
                scenario = [int]$Matches[1]; side = $Matches[2]; event = $Matches[3]; stage = [int]$Matches[4]
                key = [int]$Matches[5]; base = [int]$Matches[6]; cancelled = [bool][int]$Matches[7]
            }
        }
    }
}

try {
    $productionArgs = @()
    if ($IncludeProduction) { $productionArgs += '-StanderComboProductionProbe' }
    $server = Start-Process -FilePath $editor -ArgumentList ($common + $productionArgs + @(
        '/Engine/Maps/Entry?game=/Script/test_25d.StanderComboPredictionGameMode', '-server',
        "-port=$Port", '-MULTIHOME=127.0.0.1', '-StanderComboPredictionProbe', ('-abslog="' + $serverLog + '"')
    )) -WindowStyle Hidden -PassThru
    $until = [DateTime]::UtcNow.AddSeconds(60)
    do {
        if ($server.HasExited) { throw "Server exited. Read $serverLog" }
        $listening = (Test-Path -LiteralPath $serverLog) -and
            (Select-String -LiteralPath $serverLog -Pattern "listening on port $Port" -Quiet)
        if (-not $listening) { Start-Sleep -Milliseconds 300 }
    } until ($listening -or [DateTime]::UtcNow -ge $until)
    if (-not $listening) { throw "Server did not listen. Read $serverLog" }

    $client = Start-Process -FilePath $editor -ArgumentList ($common + @(
        "127.0.0.1:$Port", ('-abslog="' + $clientLog + '"')
    )) -WindowStyle Hidden -PassThru
    $until = [DateTime]::UtcNow.AddSeconds(60)
    do {
        if ($client.HasExited) { throw "Client exited. Read $clientLog" }
        if ($server.HasExited -or (Select-String -LiteralPath $serverLog -Pattern 'Join failure|Fatal error!' -Quiet)) {
            throw "Server could not run the network fixture. Read $serverLog"
        }
        $events = @(Read-ProbeEvents $serverLog) + @(Read-ProbeEvents $clientLog)
        $ends = @($events | Where-Object { $_.side -eq 'client' -and $_.event -eq 'end' })
        $productionMontages = @($events | Where-Object { $_.scenario -eq 5 -and $_.event -eq 'montage' })
        $done = $ends.Count -ge 7 -and (-not $IncludeProduction -or $productionMontages.Count -ge 8)
        if (-not $done) { Start-Sleep -Milliseconds 300 }
    } until ($done -or [DateTime]::UtcNow -ge $until)

    $rows = @()
    $expectedServer = @('1,2,3', '2,3', '', '1', '1,3')
    $expectedEnd = @('', '1', '1,2,3', '2,3', '2')
    $expectedRejectedKeys = @('', '1,2,3', '1,2,3', '2,3', '2,3')
    for ($scenario = 0; $scenario -lt 5; $scenario++) {
        $case = @($events | Where-Object scenario -eq $scenario)
        $predicted = @($case | Where-Object { $_.side -eq 'client' -and $_.event -eq 'activate' })
        $authority = @($case | Where-Object { $_.side -eq 'server' -and $_.event -eq 'activate' })
        $ended = @($case | Where-Object { $_.side -eq 'client' -and $_.event -eq 'end' })
        $rejected = @($case | Where-Object { $_.side -eq 'client' -and $_.event -eq 'key_rejected' })
        $chain = $predicted.Count -eq 3 -and $predicted[0].key -gt 0 -and
            $predicted[1].base -eq $predicted[0].key -and $predicted[2].base -eq $predicted[0].key -and
            @($predicted.key | Sort-Object -Unique).Count -eq 3
        $rows += [pscustomobject]@{
            scenario = $scenario
            required_tags = $scenario -ne 1 -and $scenario -ne 4
            forced_rejected_stage = @(0, 1, 1, 2, 2)[$scenario]
            client_predicted = @($predicted | ForEach-Object stage); server_activated = @($authority | ForEach-Object stage)
            client_ended = @($ended | ForEach-Object stage)
            rejected_key_callbacks = @($rejected | ForEach-Object stage); dependency_keys = $chain
            passed = $chain -and ($authority.stage -join ',') -eq $expectedServer[$scenario] -and
                (($ended.stage | Sort-Object) -join ',') -eq $expectedEnd[$scenario] -and
                (($rejected.stage | Sort-Object) -join ',') -eq $expectedRejectedKeys[$scenario]
        }
    }
    $passed = @($rows | Where-Object { -not $_.passed }).Count -eq 0
    $production = $null
    if ($IncludeProduction) {
        $clientStages = @($productionMontages | Where-Object side -eq 'client' | ForEach-Object stage)
        $serverStages = @($productionMontages | Where-Object side -eq 'server' | ForEach-Object stage)
        $production = @{
            client = $clientStages; server = $serverStages
            passed = ($clientStages -join ',') -eq '1,2,3,1' -and ($serverStages -join ',') -eq '1,2,3,1'
        }
        $passed = $passed -and $production.passed
    }
    @{ passed = $passed; cases = $rows; production = $production; events = $events } |
        ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $out 'result.json') -Encoding utf8
    $rows | Format-Table scenario, required_tags, forced_rejected_stage, server_activated, client_ended, passed
    Write-Output $out
    if (-not $passed) { throw "Prediction comparison differs from expected outcome. Read $out" }
}
finally {
    foreach ($process in @($client, $server)) {
        if ($process -and -not $process.HasExited) {
            Stop-Process -InputObject $process
        }
    }
}
