"""Read-only Stander RMB -> weapon action / fallback guard asset inspection.

Run in UnrealEditor-Cmd; writes only a fresh Saved/GAAuthoring/GuardParry report.
Never compiles, dirties or saves Content packages.
"""
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import traceback
import uuid
import unreal as u


ROOT = Path(u.Paths.project_dir()).resolve()
RUN = ROOT / "Saved/GAAuthoring/GuardParry" / (
    "inspect-" + dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S") + "-" + uuid.uuid4().hex[:8])
RUN.mkdir(parents=True, exist_ok=False)


def path(obj):
    return obj.get_path_name() if obj else None


def describe(value):
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    if hasattr(value, "get_path_name"):
        return value.get_path_name()
    if hasattr(value, "export_text"):
        return value.export_text()
    try:
        return [describe(item) for item in value]
    except TypeError:
        return str(value)


def fields(obj, names):
    return {name: describe(obj.get_editor_property(name)) for name in names}


def load(name):
    asset = u.load_asset(name)
    if not asset:
        raise RuntimeError("Asset not found: " + name)
    return asset


def cdo(name):
    cls = u.EditorAssetLibrary.load_blueprint_class(name)
    if not cls:
        raise RuntimeError("Blueprint class not found: " + name)
    return u.get_default_object(cls)


def fingerprint(obj):
    name = obj.get_path_name().split(".")[0]
    file = ROOT / "Content" / (name.removeprefix("/Game/") + ".uasset")
    return hashlib.sha256(file.read_bytes()).hexdigest()


def dirty():
    return [obj.get_name() for obj in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
            list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())]


report = {"run_id": RUN.name, "process_id": os.getpid(), "success": False,
          "dirty_before": dirty(), "weapon_sets": {}, "errors": []}
try:
    registry = u.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    for data in registry.get_assets_by_path("/Game/BluePrints/Characters/Stander/Weapons", True):
        if str(data.asset_name) != "DA_WeaponSet":
            continue
        asset = load(str(data.package_name))
        row = fields(asset, ("family", "abilities"))
        row["sha256"] = fingerprint(asset)
        row["rmb"] = []
        for entry in asset.get_editor_property("abilities"):
            if 'TagName="InputTag.RMB"' not in entry.get_editor_property("input_tag").export_text():
                continue
            ability_class = entry.get_editor_property("ability")
            if not ability_class:
                raise RuntimeError("Null RMB class: " + asset.get_path_name())
            defaults = u.get_default_object(ability_class)
            action = defaults.get_editor_property("action_config")
            details = {"ability_class": path(ability_class),
                       "ability": fields(defaults, ("startup_input_tag", "ability_tags", "action_config",
                                                     "cost_gameplay_effect_class", "cooldown_gameplay_effect_class"))}
            if action:
                details["action"] = fields(action, ("display_name", "weapon_family", "action_tag", "cooldown_tag",
                                                    "montage", "play_rate", "timeline", "mana_cost", "cooldown", "events"))
                details["action_sha256"] = fingerprint(action)
                details["event_summary"] = [fields(event, ("kind", "time", "duration"))
                                             for event in action.get_editor_property("events")]
                timeline = action.get_editor_property("timeline")
                if timeline:
                    details["timeline"] = fields(timeline, ("events", "phases"))
            row["rmb"].append(details)
        report["weapon_sets"][path(asset)] = row
    if not report["weapon_sets"]:
        raise RuntimeError("No Stander weapon sets discovered")
    pawn = cdo("/Game/BluePrints/Characters/Stander/BP_StanderPlayer")
    report["pawn"] = fields(pawn, ("startup_ability", "startup_passive_ability", "sword_config"))
    sword = pawn.get_editor_property("sword_config")
    report["fallback_config"] = fields(sword, ("guard_montage", "guard_duration", "guard_cooldown", "counter"))
    report["fallback_config_sha256"] = fingerprint(sword)
    fallback = cdo("/Game/BluePrints/Characters/Stander/GA_Stander_Guard")
    report["fallback_ability"] = fields(fallback, ("startup_input_tag", "ability_tags", "timeline",
                                                  "cost_gameplay_effect_class", "cooldown_gameplay_effect_class"))
    input_config = load("/Game/BluePrints/Input/DA_MyInputConfig")
    report["rmb_input_rows"] = [describe(row) for row in input_config.get_editor_property("ability_input_actions")
                                if 'TagName="InputTag.RMB"' in row.get_editor_property("input_tag").export_text()]
    context = load("/Game/BluePrints/Input/IMC_PlayerContext")
    report["right_mouse_mappings"] = [describe(row) for row in context.get_editor_property("default_key_mappings").get_editor_property("mappings")
                                      if row.get_editor_property("key").export_text() == "RightMouseButton"]
    report["dirty_after"] = dirty()
    if report["dirty_after"] != report["dirty_before"]:
        raise RuntimeError("Read-only loading changed dirty package set")
    report["success"] = True
except Exception:
    report["errors"].append(traceback.format_exc())
    raise
finally:
    report["dirty_after"] = dirty()
    output = RUN / "inspection.json"
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    u.log("GUARD_PARRY_INSPECTION: " + str(output))
