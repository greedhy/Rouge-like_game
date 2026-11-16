"""Read-only UE Python probe; run before author.py after the native rebuild.

Writes a fresh Saved/GAAuthoring/PerfectDodge/inspect-*/inspection.json only.
Property failures are evidence, not silently substituted defaults. No assets save.
"""
import datetime as dt
import json
from pathlib import Path
import uuid
import unreal as u


def describe(value):
    if value is None:
        return None
    if isinstance(value, (str, int, float, bool)):
        return value
    if hasattr(value, "get_path_name"):
        return value.get_path_name()
    if hasattr(value, "export_text"):
        return value.export_text()
    try:
        return [describe(item) for item in value]
    except TypeError:
        return str(value)


def properties(obj, names):
    result = {}
    for name in names:
        try:
            result[name] = describe(obj.get_editor_property(name))
        except Exception as error:
            result[name] = {"error": str(error)}
    return result


run = Path(u.Paths.project_dir()).resolve() / "Saved/GAAuthoring/PerfectDodge" / (
    "inspect-" + dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S") + "-" + uuid.uuid4().hex[:8])
run.mkdir(parents=True, exist_ok=False)
report = {"run_id": run.name, "process_id": __import__("os").getpid(), "assets": {}, "controllers": {}}
paths = [
    "/Game/BluePrints/Characters/Player/BP_Player",
    "/Game/RoguelikeGeneration/Gameplay/BP_DungeonPlayer",
    "/Game/BluePrints/Characters/Stander/BP_StanderPlayer",
    "/Game/RoguelikeGeneration/Gameplay/BP_DungeonRunGameMode",
    "/Game/BluePrints/Characters/Stander/BP_StanderGameMode",
    "/Game/BluePrints/Input/DA_MyInputConfig",
    "/Game/BluePrints/Input/IMC_PlayerContext",
    "/Game/BluePrints/Input/InputActions/IA_Pickup",
]
controllers = {"/Game/BluePrints/Player/BP_PlayerController.BP_PlayerController_C"}
for path in paths:
    asset = u.load_asset(path)
    if not asset:
        report["assets"][path] = {"error": "AssetLoadFailed"}
        continue
    obj = u.get_default_object(asset.generated_class()) if isinstance(asset, u.Blueprint) else asset
    if isinstance(obj, u.Character):
        names = ("startup_ability", "startup_passive_ability")
    elif isinstance(obj, u.GameModeBase):
        names = ("default_pawn_class", "player_controller_class", "ability_info")
    elif isinstance(obj, u.InputMappingContext):
        names = ("default_key_mappings", "mapping_profile_overrides")
    elif isinstance(obj, u.InputAction):
        names = ("value_type", "triggers", "modifiers")
    else:
        names = ("ability_input_actions",)
    report["assets"][path] = properties(obj, names)
    if isinstance(obj, u.GameModeBase):
        controller = obj.get_editor_property("player_controller_class")
        if controller:
            controllers.add(controller.get_path_name())
for path in sorted(controllers):
    cls = u.load_class(None, path)
    if not cls:
        report["controllers"][path] = {"error": "ClassLoadFailed"}
        continue
    obj = u.get_default_object(cls)
    names = ["player_context", "input_config", "move_action"]
    if isinstance(obj, u.PureMassPlayerController):
        names.extend(("mass_input_config", "mass_move_action"))
    report["controllers"][path] = properties(obj, names)
for name in ("PerfectDodgeAbility", "WorldSlowAbility"):
    cls = u.load_class(None, "/Script/test_25d." + name)
    names = ["startup_input_tag", "ability_tags", "cost_gameplay_effect_class", "cooldown_gameplay_effect_class"]
    names.extend(("dodge_settings", "cooldown_seconds") if name == "PerfectDodgeAbility" else ("settings",))
    report[name] = properties(u.get_default_object(cls), names) if cls else {"error": "NativeClassNotLoaded"}
report["python_apis"] = {name: bool(getattr(u, name, None)) for name in (
    "MyInputAction", "InputAction", "DataAssetFactory", "InputActionValueType", "Key")}
report["dirty_packages"] = [obj.get_name() for obj in (
    list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
    list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))]
(run / "inspection.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
u.log("PERFECT_DODGE_INSPECTION: " + str(run / "inspection.json"))
