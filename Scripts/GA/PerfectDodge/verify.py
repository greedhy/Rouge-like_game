"""Independent disk readback; never compiles, mutates or saves an asset.

PERFECT_DODGE_VERIFY_AUTHOR_REPORT must name a successful apply run's
authoring-result.json. Run in a DIFFERENT UnrealEditor-Cmd process, not via
ReloadPackages or inside the authoring editor. Reports do not claim PIE coverage.
"""
import importlib.util
import json
import os
from pathlib import Path
import traceback
import unreal as u

spec = importlib.util.spec_from_file_location("perfect_dodge_assets", Path(__file__).with_name("common.py"))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


def main():
    run = c.new_report("verify")
    result = {"run_id": run.name, "process_id": os.getpid(), "success": False,
              "runtime_verified": False, "visuals_verified": False, "checks": []}
    try:
        report_path = Path(os.environ.get("PERFECT_DODGE_VERIFY_AUTHOR_REPORT", ""))
        c.require(report_path.is_file() and report_path.name == "authoring-result.json",
                  "Set PERFECT_DODGE_VERIFY_AUTHOR_REPORT to the fresh successful apply report")
        author = json.loads(report_path.read_text(encoding="utf-8"))
        result["author_report"] = str(report_path.resolve())
        result["author_run_id"] = author["run_id"]
        c.require(author["success"] and author["applied"], "Author report did not complete a write run")
        c.require(author["process_id"] != os.getpid(), "Verification requires a separate UE process")
        c.require(set(author["whitelist"]) == c.WHITELIST, "Author whitelist differs from this verifier")
        c.require(not c.dirty_packages(), "Verification process has unsaved packages")
        for name, expected in author["after_hashes"].items():
            c.require(expected and c.sha256(c.disk_path(name)) == expected,
                      "Asset changed after authoring or was not saved: " + name)
        result["checks"].append("independent process and all written package hashes match")
        actual = c.validate_authored()
        result["checks"].extend([
            "exact native parents, ability identity, zero cost and server-only configuration",
            "Dodge settings, native 0.8-second cooldown, fixed InputTag.Dodge and effect references",
            "WorldSlow settings, native 3-second cooldown and no automatic grant or input binding",
            "exactly one effective Dodge startup class on BP_Player, BP_DungeonPlayer and BP_StanderPlayer",
            "actual GameMode controller CDO routes reach IMC_PlayerContext and DA_MyInputConfig",
            "one Boolean IA_Dodge, InputConfig tag row and unmodified SpaceBar mapping",
            "new abilities absent from every discovered AbilityInfo catalog",
        ])
        c.validate_preserved(author["before"], actual)
        result["checks"].append("pre-existing startup/passive arrays, input rows, mappings and catalog content preserved")
        c.require(not c.dirty_packages(), "Readback unexpectedly dirtied assets: " + str(c.dirty_packages()))
        result["observed"] = actual
        result["limitations"] = [
            "Native intrinsic/PressOnly methods are not reflected; tested by C++ automation, not Python CDO inspection.",
            "Asset links do not verify real keyboard dispatch, cooldown execution, runtime VFX lifetime or visual appearance.",
            "WorldSlow has no automatic loadout/input slot; callers explicitly grant/activate the GA or request the shared service.",
        ]
        result["success"] = True
    except Exception:
        result["error"] = traceback.format_exc()
        c.write_json(run / "verification-result.json", result)
        u.log_error("PERFECT_DODGE_VERIFY_FAILED: " + str(run / "verification-result.json"))
        raise
    c.write_json(run / "verification-result.json", result)
    u.log("PERFECT_DODGE_VERIFY_OK: " + str(run / "verification-result.json"))


main()
