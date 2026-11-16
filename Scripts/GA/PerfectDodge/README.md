# PerfectDodge asset scripts

Run after a complete Editor build and editor restart. These scripts are designed
for separate `UnrealEditor-Cmd.exe` processes; they never build C++ or run PIE.
The inspected Mage and Stander GameModes both use `BP_DungeonRunController`, whose
`player_context` and `mass_input_config` resolve to the declared shared IMC/config.
The 2026-11-15 probe found Space unoccupied. Every author run checks again.

From the project directory in PowerShell:

```powershell
$project = (Resolve-Path '.\test_25d.uproject').Path
$editor = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$scripts = (Resolve-Path '.\Scripts\GA\PerfectDodge').Path
& $editor $project -unattended -nop4 -nosplash -NullRHI -run=pythonscript "-script=$scripts\inspect.py"
Remove-Item Env:PERFECT_DODGE_APPLY -ErrorAction SilentlyContinue
& $editor $project -unattended -nop4 -nosplash -NullRHI -run=pythonscript "-script=$scripts\author.py"
```

The default author run only reads assets and emits a fresh preflight report.
After reviewing it, enable the explicit write mode:

```powershell
$env:PERFECT_DODGE_APPLY = '1'
& $editor $project -unattended -nop4 -nosplash -NullRHI -run=pythonscript "-script=$scripts\author.py"
Remove-Item Env:PERFECT_DODGE_APPLY
```

Use the exact successful `authoring-result.json` path printed by that process:

```powershell
$env:PERFECT_DODGE_VERIFY_AUTHOR_REPORT = 'C:\absolute\project\Saved\GAAuthoring\PerfectDodge\author-RUN-ID\authoring-result.json'
& $editor $project -unattended -nop4 -nosplash -NullRHI -run=pythonscript "-script=$scripts\verify.py"
Remove-Item Env:PERFECT_DODGE_VERIFY_AUTHOR_REPORT
```

Each invocation writes its own unique directory under `Saved/GAAuthoring/PerfectDodge`.
Check process exit code **and** the fresh result's `success`; a console receipt
alone is insufficient. Partial failures retain `failure.json`, `journal.json` and
raw pre-write backups. Inspect saved assets before retrying; never restore backups
over later user edits. Neither author nor verifier saves outside the eight-package
literal whitelist in `common.py`, changes a catalog, or grants WorldSlow.

The configured native WorldSlow GA uses its own three-world-second cooldown;
callers explicitly grant/activate it. The shared `AbilityWorldTimeSubsystem`
request API is separate and does not automatically impose this GA cooldown.
Python readback proves asset configuration, not keyboard dispatch, gameplay,
animation matching or VFX appearance/lifetime; those require automation/PIE.
