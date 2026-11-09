param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$Filter = 'test_25d.UnifiedGA+test_25d.Batch6.PetrifyLeaseIsolation+test_25d.Batch6.MassPetrifyLifecycle+test_25d.Batch6.ActorPetrifyLifecycle',
    [string[]]$ExpectedTests = @(),
    [switch]$SkipBuild,
    [switch]$SelfTest,
    [ValidateRange(10, 7200)][int]$TimeoutSeconds = 900
)
# Build (unless already built by the coordinator) -> unique UE automation report ->
# assert every expected test executed successfully. An empty selection always fails.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$ProjectFile = Join-Path $ProjectRoot 'test_25d.uproject'

# Bind the run to its actual inputs, not merely the age of its test declarations.
# Matrix membership selects project packages; the matrix bytes are intentionally
# excluded so publishing the reviewed candidate does not invalidate this run.
function Get-ValidationInputManifest {
    param([string]$Root)
    $Root = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $Prefix = $Root + [System.IO.Path]::DirectorySeparatorChar
    $Paths = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($Folder in @('Source', 'Config')) {
        $Directory = Join-Path $Root $Folder
        if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
            throw "Missing validation input directory: $Directory"
        }
        foreach ($File in Get-ChildItem -LiteralPath $Directory -File -Recurse) {
            $null = $Paths.Add($File.FullName)
        }
    }
    $Binaries = Join-Path $Root 'Binaries/Win64'
    $Libraries = @(Get-ChildItem -LiteralPath $Binaries -File -Recurse -Filter '*.dll')
    if ($Libraries.Count -eq 0) { throw 'No project binaries available for validation.' }
    foreach ($Library in $Libraries) { $null = $Paths.Add($Library.FullName) }
    $null = $Paths.Add((Join-Path $Root 'test_25d.uproject'))
    $Matrix = Get-Content -LiteralPath (Join-Path $Root 'docs/ga-unified-migration-matrix.json') -Raw | ConvertFrom-Json
    $Packages = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($Row in $Matrix.rows) {
        $Asset = [string]$Row.assetPath
        if ($Asset.StartsWith('/Script/')) { continue }
        if (-not $Asset.StartsWith('/Game/') -or $Asset.Contains('\') -or $Asset -match '(^|/)\.\.(/|$)') {
            throw "Invalid matrix package input: $Asset"
        }
        $Package = $Asset.Split('.')[0]
        $null = $Packages.Add($Package)
        $PackageFile = [System.IO.Path]::GetFullPath((Join-Path $Root ('Content/' + $Package.Substring(6) + '.uasset')))
        if (-not $PackageFile.StartsWith((Join-Path $Root 'Content') + [System.IO.Path]::DirectorySeparatorChar,
                                        [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Matrix package escaped Content: $Asset"
        }
        if (-not (Test-Path -LiteralPath $PackageFile -PathType Leaf)) {
            throw "Missing matrix package input: $PackageFile"
        }
        $null = $Paths.Add($PackageFile)
        foreach ($Extension in @('.uexp', '.ubulk')) {
            $Sidecar = [System.IO.Path]::ChangeExtension($PackageFile, $Extension)
            if (Test-Path -LiteralPath $Sidecar -PathType Leaf) { $null = $Paths.Add($Sidecar) }
        }
    }
    if ($Packages.Count -eq 0) { throw 'Matrix contains no project asset inputs.' }
    $Files = [ordered]@{}
    foreach ($Path in @($Paths | Sort-Object)) {
        $Absolute = [System.IO.Path]::GetFullPath($Path)
        if (-not $Absolute.StartsWith($Prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Validation input escaped project: $Absolute"
        }
        $Relative = $Absolute.Substring($Prefix.Length).Replace('\', '/')
        $Files[$Relative] = (Get-FileHash -LiteralPath $Absolute -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    return [ordered]@{ capturedUtc = [datetime]::UtcNow.ToString('o'); matrixAssetCount = $Packages.Count; files = $Files }
}

function Assert-ValidationInputsUnchanged {
    param($Before, $After)
    if ($Before.matrixAssetCount -ne $After.matrixAssetCount -or $Before.files.Count -ne $After.files.Count) {
        throw 'Validation input membership changed during automation.'
    }
    foreach ($Path in $Before.files.Keys) {
        if (-not $After.files.Contains($Path) -or $Before.files[$Path] -ne $After.files[$Path]) {
            throw "Validation input changed during automation: $Path"
        }
    }
}

# SkipBuild means the coordinator already built this source. Reject obvious stale
# module binaries before launching UE; pre/post hashes then protect the actual run.
function Assert-CompiledSourceCurrent {
    param([string]$Root)
    $Latest = Get-ChildItem -LiteralPath (Join-Path $Root 'Source') -File -Recurse |
        Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    $Project = Get-Content -LiteralPath (Join-Path $Root 'test_25d.uproject') -Raw | ConvertFrom-Json
    foreach ($Module in $Project.Modules) {
        $Library = Get-Item -LiteralPath (Join-Path $Root ('Binaries/Win64/UnrealEditor-' + $Module.Name + '.dll'))
        if ($Library.LastWriteTimeUtc -lt $Latest.LastWriteTimeUtc) {
            throw "Project DLL predates source; rebuild before automation: $($Library.Name), $($Latest.FullName)"
        }
    }
}

function Assert-AutomationReport {
    param([string]$ReportFile, [datetime]$StartedUtc, [string[]]$Expected)
    if (-not (Test-Path -LiteralPath $ReportFile -PathType Leaf)) {
        throw "Missing fresh automation report: $ReportFile"
    }
    $Item = Get-Item -LiteralPath $ReportFile
    if ($Item.LastWriteTimeUtc -lt $StartedUtc) {
        throw "Stale automation report: $ReportFile"
    }
    $Report = Get-Content -LiteralPath $ReportFile -Raw | ConvertFrom-Json
    foreach ($Counter in @('failed', 'notRun', 'inProcess')) {
        if (-not $Report.PSObject.Properties[$Counter] -or $Report.$Counter -ne 0) {
            throw "Report counter $Counter is missing/nonzero: $ReportFile"
        }
    }
    if (-not $Report.PSObject.Properties['tests']) {
        throw "Report has no tests field: $ReportFile"
    }
    $Tests = @($Report.tests)
    if ($Tests.Count -eq 0) {
        throw "Zero tests executed: $ReportFile"
    }
    $Names = @{}
    foreach ($Test in $Tests) {
        if (-not $Test.PSObject.Properties['fullTestPath'] -or [string]::IsNullOrWhiteSpace($Test.fullTestPath)) {
            throw 'Test is missing fullTestPath.'
        }
        if ($Names.ContainsKey($Test.fullTestPath)) {
            throw "Duplicate test result: $($Test.fullTestPath)"
        }
        $Names[$Test.fullTestPath] = $true
        if (-not $Test.PSObject.Properties['state'] -or $Test.state -ne 'Success') {
            throw "Test did not succeed: $($Test.fullTestPath)"
        }
    }
    foreach ($Name in $Expected) {
        if (-not $Names.ContainsKey($Name)) {
            throw "Expected test was not executed: $Name"
        }
    }
    return $Tests
}

function Invoke-ParserSelfTest {
    $Directory = Join-Path $ProjectRoot ('Saved/GAAuthoring/UnifiedGA/parser-selftest-' + [guid]::NewGuid().ToString('N'))
    $null = New-Item -ItemType Directory -Path $Directory
    $ReportFile = Join-Path $Directory 'index.json'
    $Start = [datetime]::UtcNow.AddSeconds(-1)
    $Good = @{ failed = 0; notRun = 0; inProcess = 0; tests = @(
        @{ fullTestPath = 'Fixture.Known'; state = 'Success' }
    ) }
    $Cases = @(
        @{ name = 'zero'; report = @{ failed = 0; notRun = 0; inProcess = 0; tests = @() }; expected = @() },
        @{ name = 'missing expected'; report = $Good; expected = @('Fixture.Missing') },
        @{ name = 'failed state'; report = @{ failed = 0; notRun = 0; inProcess = 0; tests = @(
            @{ fullTestPath = 'Fixture.Known'; state = 'Fail' }
        ) }; expected = @() },
        @{ name = 'duplicate'; report = @{ failed = 0; notRun = 0; inProcess = 0; tests = @(
            @{ fullTestPath = 'Fixture.Known'; state = 'Success' },
            @{ fullTestPath = 'Fixture.Known'; state = 'Success' }
        ) }; expected = @() }
    )
    foreach ($Case in $Cases) {
        $Case.report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ReportFile -Encoding utf8
        $Rejected = $false
        try { $null = Assert-AutomationReport $ReportFile $Start $Case.expected }
        catch { $Rejected = $true }
        if (-not $Rejected) { throw "Parser failed to reject: $($Case.name)" }
    }
    $Good | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ReportFile -Encoding utf8
    $null = Assert-AutomationReport $ReportFile $Start @('Fixture.Known')
    $Rejected = $false
    try { $null = Assert-AutomationReport $ReportFile ([datetime]::UtcNow.AddMinutes(1)) @('Fixture.Known') }
    catch { $Rejected = $true }
    if (-not $Rejected) { throw 'Parser accepted a stale report.' }
    $Rejected = $false
    try { $null = Assert-AutomationReport (Join-Path $Directory 'never-run.json') $Start @('Fixture.Known') }
    catch { $Rejected = $true }
    if (-not $Rejected) { throw 'Parser accepted evidence for an unexecuted run.' }

    $Fixture = Join-Path $Directory 'inputs'
    foreach ($Folder in @('Source', 'Config', 'Binaries/Win64', 'Content/Fixture', 'docs')) {
        $null = New-Item -ItemType Directory -Path (Join-Path $Fixture $Folder) -Force
    }
    foreach ($Relative in @('Source/Ability.h', 'Source/Ability.cpp', 'Source/Module.Build.cs', 'Config/Default.ini',
                            'Binaries/Win64/UnrealEditor-Fixture.dll', 'Content/Fixture/GA_Test.uasset', 'Content/Fixture/GA_Test.uexp')) {
        Set-Content -LiteralPath (Join-Path $Fixture $Relative) -Value 'original fixture bytes' -Encoding utf8
    }
    @{ Modules = @(@{ Name = 'Fixture' }) } | ConvertTo-Json -Depth 4 |
        Set-Content -LiteralPath (Join-Path $Fixture 'test_25d.uproject') -Encoding utf8
    $FixtureMatrix = Join-Path $Fixture 'docs/ga-unified-migration-matrix.json'
    @{ rows = @(@{ assetPath = '/Game/Fixture/GA_Test.GA_Test' }, @{ assetPath = '/Script/test_25d.NativeFixture' }) } |
        ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $FixtureMatrix -Encoding utf8
    $Before = Get-ValidationInputManifest $Fixture
    if ($Before.files.Count -ne 8 -or $Before.matrixAssetCount -ne 1) { throw 'Input manifest omitted a required category or sidecar.' }
    $null = Assert-ValidationInputsUnchanged $Before (Get-ValidationInputManifest $Fixture)
    # Candidate matrix publication changes metadata only, not the tested input set.
    $Metadata = Get-Content -LiteralPath $FixtureMatrix -Raw | ConvertFrom-Json
    $Metadata | Add-Member -NotePropertyName generatedAtUtc -NotePropertyValue ([datetime]::UtcNow.ToString('o'))
    $Metadata | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $FixtureMatrix -Encoding utf8
    $null = Assert-ValidationInputsUnchanged $Before (Get-ValidationInputManifest $Fixture)
    foreach ($Relative in @('Source/Ability.h', 'Source/Module.Build.cs', 'Config/Default.ini',
                            'Binaries/Win64/UnrealEditor-Fixture.dll', 'Content/Fixture/GA_Test.uasset')) {
        $Path = Join-Path $Fixture $Relative
        $Original = [System.IO.File]::ReadAllBytes($Path)
        [System.IO.File]::WriteAllText($Path, 'changed fixture bytes')
        $Rejected = $false
        try { $null = Assert-ValidationInputsUnchanged $Before (Get-ValidationInputManifest $Fixture) }
        catch { $Rejected = $true }
        [System.IO.File]::WriteAllBytes($Path, $Original)
        if (-not $Rejected) { throw "Input manifest accepted changed bytes: $Relative" }
    }
    Set-Content -LiteralPath (Join-Path $Fixture 'Source/NewAbility.h') -Value 'new class' -Encoding utf8
    $Rejected = $false
    try { $null = Assert-ValidationInputsUnchanged $Before (Get-ValidationInputManifest $Fixture) }
    catch { $Rejected = $true }
    if (-not $Rejected) { throw 'Input manifest accepted an added source class.' }
    (Get-Item -LiteralPath (Join-Path $Fixture 'Source/NewAbility.h')).LastWriteTimeUtc = [datetime]::UtcNow.AddSeconds(2)
    $Rejected = $false
    try { Assert-CompiledSourceCurrent $Fixture }
    catch { $Rejected = $true }
    if (-not $Rejected) { throw 'Preflight accepted an old DLL after source changed.' }
    Write-Output "PASS: report freshness/absence and complete input fingerprints, changed bytes/new class/stale DLL rejection; synthetic fixtures only: $Directory"
}

if ($SelfTest) {
    Invoke-ParserSelfTest
    exit 0
}

# Default expectations are derived from test declarations, not report contents. This
# prevents a misspelled/partially-discovered filter from silently becoming green.
if ($ExpectedTests.Count -eq 0) {
    $Prefixes = @($Filter.Split('+') | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    $Discovered = [System.Collections.Generic.HashSet[string]]::new()
    foreach ($File in Get-ChildItem -LiteralPath (Join-Path $ProjectRoot 'Source') -Filter '*.cpp' -Recurse) {
        $SourceText = Get-Content -LiteralPath $File.FullName -Raw
        foreach ($Match in [regex]::Matches($SourceText, 'IMPLEMENT_\w*AUTOMATION_TEST\s*\(\s*\w+\s*,\s*"([^"]+)"')) {
            $Name = $Match.Groups[1].Value
            foreach ($Prefix in $Prefixes) {
                if ($Name.StartsWith($Prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                    $null = $Discovered.Add($Name)
                }
            }
        }
    }
    $ExpectedTests = @($Discovered | Sort-Object)
}

if (-not $SkipBuild) {
    & (Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat') test_25dEditor Win64 Development "-Project=$ProjectFile" -WaitMutex -NoHotReloadFromIDE -NoUBA
    if ($LASTEXITCODE -ne 0) { throw 'Build failed; refusing to test an old DLL.' }
}
$RunId = ([datetime]::UtcNow.ToString('yyyyMMddTHHmmss')) + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$RunDirectory = Join-Path $ProjectRoot "Saved/GAAuthoring/UnifiedGA/$RunId"
$ReportDirectory = Join-Path $ProjectRoot "Saved/Automation/UnifiedGA/$RunId"
if (Test-Path -LiteralPath $RunDirectory) { throw "Run directory already exists: $RunDirectory" }
if (Test-Path -LiteralPath $ReportDirectory) { throw "Report directory already exists: $ReportDirectory" }
$null = New-Item -ItemType Directory -Path $RunDirectory
$Log = Join-Path $ProjectRoot "Saved/Logs/UnifiedGA-$RunId.log"
$Summary = [ordered]@{
    schemaVersion = 2; runId = $RunId; filter = $Filter; expectedTests = $ExpectedTests; success = $false
    reportDirectory = $ReportDirectory; log = $Log; startedUtc = [datetime]::UtcNow.ToString('o')
    inputManifest = [ordered]@{
        schemaVersion = 1; runId = $RunId; scope = 'Source+Config+ProjectDLL+MatrixContentPackages+ProjectFile'
        before = $null; after = $null; unchanged = $false
    }
}
$Arguments = @(
    ('"' + $ProjectFile + '"'), '-unattended', '-NullRHI', '-nosplash', '-nop4',
    ('"-ExecCmds=Automation RunTests ' + $Filter + '"'), '"-TestExit=Automation Test Queue Empty"',
    ('"-ReportExportPath=' + $ReportDirectory + '"'), ('"-abslog=' + $Log + '"')
)
$StartedUtc = [datetime]::UtcNow
try {
    Assert-CompiledSourceCurrent $ProjectRoot
    $Summary.inputManifest.before = Get-ValidationInputManifest $ProjectRoot
    $StartedUtc = [datetime]::UtcNow
    $Summary.startedUtc = $StartedUtc.ToString('o')
    $Process = Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') `
        -ArgumentList $Arguments -WorkingDirectory $ProjectRoot -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $RunDirectory 'stdout.txt') -RedirectStandardError (Join-Path $RunDirectory 'stderr.txt')
    $Deadline = $StartedUtc.AddSeconds($TimeoutSeconds)
    while (-not $Process.WaitForExit(1000)) {
        if ([datetime]::UtcNow -gt $Deadline) {
            Stop-Process -Id $Process.Id -Force
            throw "Automation timed out after $TimeoutSeconds seconds."
        }
    }
    # Refresh after WaitForExit so ExitCode is available on all supported PowerShell versions.
    $Process.Refresh()
    $Summary['exitCode'] = $Process.ExitCode
    if ($Process.ExitCode -ne 0) { throw "Unreal process failed with exit code $($Process.ExitCode): $Log" }
    $Tests = @(Assert-AutomationReport (Join-Path $ReportDirectory 'index.json') $StartedUtc $ExpectedTests)
    if ($ExpectedTests.Count -eq 0) { throw 'No expected test declarations matched; use -ExpectedTests for dynamic tests.' }
    $Summary['reportFile'] = Join-Path $ReportDirectory 'index.json'
    $Summary['reportSha256'] = (Get-FileHash -LiteralPath $Summary.reportFile -Algorithm SHA256).Hash.ToLowerInvariant()
    $Summary['executedTests'] = @($Tests | ForEach-Object { $_.fullTestPath })
    $Summary.success = $true
}
catch {
    $Summary['error'] = $_.Exception.Message
    throw
}
finally {
    $ManifestFailure = $null
    if ($Summary.inputManifest.before) {
        try {
            $Summary.inputManifest.after = Get-ValidationInputManifest $ProjectRoot
            Assert-ValidationInputsUnchanged $Summary.inputManifest.before $Summary.inputManifest.after
            $Summary.inputManifest.unchanged = $true
        }
        catch {
            $Summary.success = $false
            $Summary['inputManifestError'] = $_.Exception.Message
            $ManifestFailure = $_
        }
    }
    $Summary['finishedUtc'] = [datetime]::UtcNow.ToString('o')
    $Summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $RunDirectory 'validation.json') -Encoding utf8
    Write-Output "Validation record: $RunDirectory"
    if ($ManifestFailure) { throw $ManifestFailure }
}
$Tests | Select-Object fullTestPath, state
Write-Output "Fresh automation and unchanged inputs passed: $ReportDirectory"
