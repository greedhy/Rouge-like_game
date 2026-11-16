"""Opt in only reviewed RMB Guard configurations; read-only unless GUARD_PARRY_APPLY=1.

GUARD_PARRY_INSPECTION must point to the successful bounded inspection report.
Use a newly started commandlet after a full Editor build. No Blueprint is edited.
"""
import importlib.util
import os
from pathlib import Path
import shutil
import traceback
import unreal as u

spec = importlib.util.spec_from_file_location("guard_parry_assets", Path(__file__).with_name("common.py"))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


def main():
    run = c.new_report("author")
    apply = os.environ.get("GUARD_PARRY_APPLY") == "1"
    report = {"run_id": run.name, "process_id": os.getpid(), "applied": apply, "success": False,
              "whitelist": sorted(c.WHITELIST), "backups": {}, "saved": [], "compiled": {},
              "runtime_verified": False, "visuals_verified": False}

    def journal():
        c.write_json(run / "journal.json", report)

    try:
        c.require(not c.dirty_packages(), "Unsaved packages: " + str(c.dirty_packages()))
        report["inspection_report"] = str(Path(os.environ.get("GUARD_PARRY_INSPECTION", "")).resolve())
        inspected = c.read_inspection(report["inspection_report"])
        before = c.snapshot()
        report["before"] = before
        c.validate_inspection(inspected, before)
        c.settings()
        # Prove the actual nested value setters on detached structs before touching Content.
        probe = u.StanderWeaponActionEvent()
        probe.set_editor_property("parry", c.settings())
        c.require(c.matches(c.parry_fields(c.prop(probe, "parry"))), "Event.Parry assignment failed")
        c.require(not c.dirty_packages(), "Read-only preflight dirtied Content")
        report["before_hashes"] = c.hashes(c.WHITELIST)
        report["planned"] = [name for name in sorted(c.WHITELIST)
                             if any(not c.matches(row) for row in
                                    (before["parry"][name] if isinstance(before["parry"][name], list)
                                     else [before["parry"][name]]))]
        if not apply:
            report["success"] = True
            c.write_json(run / "preflight-result.json", report)
            u.log("GUARD_PARRY_DRY_RUN_OK: " + str(run / "preflight-result.json"))
            return

        # Preserve exact current disk state, including user edits, before any package mutation.
        for name in report["planned"]:
            copies = []
            for suffix in (".uasset", ".uexp", ".ubulk"):
                source = c.disk_path(name).with_suffix(suffix)
                if not source.exists():
                    continue
                destination = run / "backup" / source.relative_to(c.ROOT)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)
                expected = c.sha256(source)
                c.require(c.sha256(destination) == expected, "Backup mismatch: " + str(source))
                copies.append({"source": str(source), "backup": str(destination), "sha256": expected})
            c.require(copies, "No backup for existing asset: " + name)
            report["backups"][name] = copies
        journal()
        for name in report["planned"]:
            asset = c.load(name)
            if name == c.FALLBACK:
                asset.set_editor_property("guard_parry", c.settings())
            else:
                events = list(c.prop(asset, "events"))
                for event in events:
                    c.require(c.prop(event, "kind") == u.StanderWeaponActionKind.GUARD,
                              "Unexpected non-Guard event before mutation")
                    event.set_editor_property("parry", c.settings())
                asset.set_editor_property("events", events)
            c.require(not set(c.dirty_packages()) - c.WHITELIST,
                      "Unrelated package became dirty; refusing save: " + str(c.dirty_packages()))
            c.require(c.package(c.path(asset)) in c.WHITELIST, "Save outside whitelist")
            c.require(u.EditorAssetLibrary.save_loaded_asset(asset, False), "Save failed: " + name)
            report["saved"].append(name)
            journal()
        after = c.snapshot()
        c.validate_authored(after)
        c.validate_preserved(before, after)
        c.require(not c.dirty_packages(), "Packages remain dirty after saving")
        report["after"] = after
        report["after_hashes"] = c.hashes(c.WHITELIST)
        report["success"] = True
        c.write_json(run / "authoring-result.json", report)
        u.log("GUARD_PARRY_AUTHOR_OK: " + str(run / "authoring-result.json"))
    except Exception:
        report["error"] = traceback.format_exc()
        report["dirty_packages"] = c.dirty_packages()
        c.write_json(run / "failure.json", report)
        u.log_error("GUARD_PARRY_AUTHOR_FAILED: " + str(run / "failure.json"))
        raise


main()
