"""Idempotent UE asset authoring; default is a read-only preflight.

After a full native Editor build, run in a new UnrealEditor-Cmd process.
Only PERFECT_DODGE_APPLY=1 enables writes. The literal whitelist is in common.py.
Every existing output package and sidecar is backed up before the first mutation.
Run verify.py in another process with PERFECT_DODGE_VERIFY_AUTHOR_REPORT pointing
to this run's successful authoring-result.json. Neither script runs PIE.
"""
import importlib.util
import os
from pathlib import Path
import shutil
import traceback
import unreal as u

spec = importlib.util.spec_from_file_location("perfect_dodge_assets", Path(__file__).with_name("common.py"))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


def main():
    run = c.new_report("author")
    apply = os.environ.get("PERFECT_DODGE_APPLY") == "1"
    report = {"run_id": run.name, "process_id": os.getpid(), "applied": apply, "success": False,
              "whitelist": sorted(c.WHITELIST), "created": [], "saved": [], "compiled": {}, "backups": {},
              "physical_key": c.KEY, "runtime_verified": False, "visuals_verified": False}

    def journal():
        c.write_json(run / "journal.json", report)

    def save(asset):
        name = c.package(asset)
        c.require(name in c.WHITELIST, "Attempt to save outside whitelist: " + str(name))
        c.require(not set(c.dirty_packages()) - c.WHITELIST,
                  "Unrelated package became dirty; refusing save: " + str(c.dirty_packages()))
        if isinstance(asset, u.Blueprint):
            u.BlueprintEditorLibrary.compile_blueprint(asset)
            report["compiled"][name] = c.blueprint_status(asset)
        c.require(not set(c.dirty_packages()) - c.WHITELIST,
                  "Compilation dirtied an unrelated package; refusing save: " + str(c.dirty_packages()))
        c.require(u.EditorAssetLibrary.save_loaded_asset(asset, False), "Save failed: " + name)
        if name not in report["saved"]:
            report["saved"].append(name)
        journal()

    def blueprint(name):
        if u.EditorAssetLibrary.does_asset_exist(name):
            return c.load(name)
        c.require(name in c.PARENTS, "Unexpected Blueprint creation: " + name)
        factory = u.BlueprintFactory()
        factory.set_editor_property("parent_class", c.native(c.PARENTS[name]))
        folder, leaf = name.rsplit("/", 1)
        result = u.AssetToolsHelpers.get_asset_tools().create_asset(leaf, folder, u.Blueprint, factory)
        c.require(result, "Blueprint creation failed: " + name)
        report["created"].append(name)
        u.BlueprintEditorLibrary.compile_blueprint(result)
        report["compiled"][name] = c.blueprint_status(result)
        return result

    def set_values(obj, values):
        for key, value in values.items():
            obj.set_editor_property(key, value)

    try:
        c.require(not c.dirty_packages(), "Unsaved packages: " + str(c.dirty_packages()))
        before = c.snapshot()
        report["before"] = before
        c.preflight(before)
        # Require every authoring property before touching any assets.
        dodge_defaults = u.get_default_object(c.native(c.PARENTS[c.GA_DODGE]))
        c.validate_values(c.prop(dodge_defaults, "dodge_settings"), c.DODGE_VALUES)
        c.validate_values(c.prop(u.get_default_object(c.native(c.PARENTS[c.GA_SLOW])), "settings"), c.SLOW_VALUES)
        for name in ("dodge_effect", "perfect_effect", "reward"):
            c.prop(c.prop(dodge_defaults, "dodge_settings"), name)
        c.require(getattr(u, "MyInputAction", None), "MyInputAction is unavailable in loaded Python module")
        c.require(not c.dirty_packages(), "Assets became dirty while loading: " + str(c.dirty_packages()))
        report["before_hashes"] = {name: c.sha256(c.disk_path(name)) for name in sorted(c.WHITELIST)}
        if not apply:
            report["success"] = True
            c.write_json(run / "preflight-result.json", report)
            u.log("PERFECT_DODGE_DRY_RUN_OK: " + str(run / "preflight-result.json"))
            return

        # Fresh raw backups preserve the current disk state, including pre-existing user edits.
        for name in sorted(c.WHITELIST):
            source = c.disk_path(name)
            copies = []
            for suffix in (".uasset", ".uexp", ".ubulk"):
                file = source.with_suffix(suffix)
                if not file.exists():
                    continue
                destination = run / "backup" / file.relative_to(c.ROOT)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(file, destination)
                c.require(c.sha256(destination) == c.sha256(file), "Backup hash mismatch: " + str(file))
                copies.append({"source": str(file), "backup": str(destination), "sha256": c.sha256(file)})
            report["backups"][name] = copies
        journal()
        for asset_name in (c.GA_DODGE, c.GA_SLOW):
            bp = blueprint(asset_name)
            defaults = u.get_default_object(bp.generated_class())
            if asset_name == c.GA_DODGE:
                settings = c.prop(defaults, "dodge_settings")
                set_values(settings, c.DODGE_VALUES)
                reward = c.prop(settings, "reward")
                set_values(reward, c.SLOW_VALUES)
                set_values(settings, {"reward": reward, "dodge_effect": c.load(c.AIR), "perfect_effect": c.load(c.MANA)})
                set_values(defaults, {"dodge_settings": settings, "cooldown_seconds": .8,
                                     "startup_input_tag": c.tag(c.DODGE_TAG),
                                     "cost_gameplay_effect_class": None,
                                     "cooldown_gameplay_effect_class": c.native("/Script/test_25d.PerfectDodgeCooldownEffect")})
            else:
                settings = c.prop(defaults, "settings")
                set_values(settings, c.SLOW_VALUES)
                set_values(defaults, {"settings": settings, "startup_input_tag": c.tag(""),
                                     "cost_gameplay_effect_class": None,
                                     "cooldown_gameplay_effect_class": c.native("/Script/test_25d.WorldSlowCooldownEffect")})
            save(bp)

        if u.EditorAssetLibrary.does_asset_exist(c.ACTION):
            action = c.load(c.ACTION)
        else:
            factory = u.DataAssetFactory()
            factory.set_editor_property("data_asset_class", u.InputAction)
            folder, leaf = c.ACTION.rsplit("/", 1)
            action = u.AssetToolsHelpers.get_asset_tools().create_asset(leaf, folder, u.InputAction, factory)
            c.require(action, "InputAction creation failed")
            action.set_editor_property("value_type", u.InputActionValueType.BOOLEAN)
            report["created"].append(c.ACTION)
            save(action)
        config = c.load(c.INPUT)
        rows = list(c.prop(config, "ability_input_actions"))
        if not any(c.tag_name(c.prop(row, "input_tag")) == c.DODGE_TAG for row in rows):
            rows.append(c.make_input_row(action))
            config.set_editor_property("ability_input_actions", rows)
            save(config)
        context = c.load(c.CONTEXT)
        if not any(c.package(c.prop(row, "action")) == c.ACTION for row in c.mapping_rows(context)):
            key = u.Key()
            key.import_text(c.KEY)
            c.require(key.export_text() == c.KEY, "SpaceBar FKey import failed")
            context.map_key(action, key)
            save(context)
        dodge_class = u.EditorAssetLibrary.load_blueprint_class(c.GA_DODGE)
        for pawn in c.PAWNS:
            # Use each effective pre-write array; never copy the parent's loadout into its child.
            names = [name for name in before["startup"][pawn] if name != c.class_path(c.GA_DODGE)]
            abilities = [c.native(name) if name else None for name in names] + [dodge_class]
            defaults = c.cdo(pawn)
            if [c.path(value) for value in c.prop(defaults, "startup_ability")] != [c.path(value) for value in abilities]:
                defaults.set_editor_property("startup_ability", abilities)
                save(c.load(pawn))
        # Blueprint recompilation can dirty an explicitly whitelisted child through inheritance.
        for name in c.dirty_packages():
            c.require(name in c.WHITELIST, "Foreign package became dirty: " + name)
            save(c.load(name))
        after = c.validate_authored()
        c.validate_preserved(before, after)
        report["after"] = after
        report["after_hashes"] = {name: c.sha256(c.disk_path(name)) for name in sorted(c.WHITELIST)}
        report["success"] = True
        c.write_json(run / "authoring-result.json", report)
        u.log("PERFECT_DODGE_AUTHOR_OK: " + str(run / "authoring-result.json"))
    except Exception:
        report["error"] = traceback.format_exc()
        report["dirty_packages"] = c.dirty_packages()
        c.write_json(run / "failure.json", report)
        u.log_error("PERFECT_DODGE_AUTHOR_FAILED: " + str(run / "failure.json"))
        raise


main()
