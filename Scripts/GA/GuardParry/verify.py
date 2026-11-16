"""Read saved assets in a separate UE process; no compile, mutation or save.

GUARD_PARRY_VERIFY_AUTHOR_REPORT names the successful authoring-result.json.
This checks wiring/configuration only, not runtime behavior or visual acceptance.
"""
import importlib.util
import json
import os
from pathlib import Path
import traceback
import unreal as u

spec = importlib.util.spec_from_file_location("guard_parry_assets", Path(__file__).with_name("common.py"))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


def main():
    run = c.new_report("verify")
    result = {"run_id": run.name, "process_id": os.getpid(), "success": False,
              "runtime_verified": False, "visuals_verified": False, "checks": []}
    try:
        file = Path(os.environ.get("GUARD_PARRY_VERIFY_AUTHOR_REPORT", ""))
        c.require(file.is_file() and file.name == "authoring-result.json",
                  "Set GUARD_PARRY_VERIFY_AUTHOR_REPORT to the successful apply report")
        author = json.loads(file.read_text(encoding="utf-8"))
        result["author_report"] = str(file.resolve())
        result["author_run_id"] = author["run_id"]
        c.require(author["success"] and author["applied"], "Author did not finish an apply run")
        c.require(author["process_id"] != os.getpid(), "Verification requires a different UE process")
        c.require(set(author["whitelist"]) == c.WHITELIST, "Author scope differs from verifier")
        c.require(not c.dirty_packages(), "Verification has unsaved packages")
        c.require(c.hashes(c.WHITELIST) == author["after_hashes"], "Authored files changed or were not saved")
        actual = c.snapshot()
        c.validate_authored(actual)
        c.validate_preserved(author["before"], actual)
        c.validate_inspection(c.read_inspection(author["inspection_report"]), actual)
        c.require(not c.dirty_packages(), "Readback unexpectedly dirtied packages")
        result["checks"] = [
            "separate process and exact written package hashes",
            "11 actual RMB Guard events and fallback GuardParry enabled at 0.15 / 1.0 seconds",
            "world-slow reward explicitly enabled: scale 0.2, blend 0.05 / 1.2 / 0.15, source compensation, 3-second lock",
            "all original event fields, fees, cooldowns, montage references and timeline phases preserved",
            "weapon grants, startup arrays, ability classes/tags and RightMouseButton routing preserved",
            "protected package fingerprints unchanged and zero dirty packages",
        ]
        result["observed"] = actual
        result["limitations"] = ["No live mouse dispatch, projectile reflection, melee stun, earned world-slow lifecycle, VFX or multiplayer verification."]
        result["success"] = True
    except Exception:
        result["error"] = traceback.format_exc()
        c.write_json(run / "verification-result.json", result)
        u.log_error("GUARD_PARRY_VERIFY_FAILED: " + str(run / "verification-result.json"))
        raise
    c.write_json(run / "verification-result.json", result)
    u.log("GUARD_PARRY_VERIFY_OK: " + str(run / "verification-result.json"))


main()
