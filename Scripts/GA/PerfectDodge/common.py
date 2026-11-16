"""Read-only contract shared by PerfectDodge authoring and disk verification."""
import datetime as dt
import hashlib
import json
from pathlib import Path
import re
import uuid
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
GA_DODGE = "/Game/UnifiedAbilities/Defense/PerfectDodge/GA_PerfectDodge"
GA_SLOW = "/Game/UnifiedAbilities/Time/WorldSlow/GA_WorldSlow"
INPUT = "/Game/BluePrints/Input/DA_MyInputConfig"
CONTEXT = "/Game/BluePrints/Input/IMC_PlayerContext"
ACTION = "/Game/BluePrints/Input/InputActions/IA_Dodge"
PAWNS = (
    "/Game/BluePrints/Characters/Player/BP_Player",
    "/Game/RoguelikeGeneration/Gameplay/BP_DungeonPlayer",
    "/Game/BluePrints/Characters/Stander/BP_StanderPlayer",
)
MODES = {
    "/Game/RoguelikeGeneration/Gameplay/BP_DungeonRunGameMode": PAWNS[1],
    "/Game/BluePrints/Characters/Stander/BP_StanderGameMode": PAWNS[2],
}
PARENTS = {GA_DODGE: "/Script/test_25d.PerfectDodgeAbility", GA_SLOW: "/Script/test_25d.WorldSlowAbility"}
IDENTITIES = {GA_DODGE: "Abilities.Common.PerfectDodge", GA_SLOW: "Abilities.Time.WorldSlow"}
DODGE_TAG = "InputTag.Dodge"
KEY = "SpaceBar"
AIR = "/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Aura_Air"
MANA = "/Game/Free_Spells/VFX_Niagara/NS_Free_Spells_Aura_Mana"
SLOW_VALUES = {"scale": .2, "blend_in": .05, "hold": 1.2, "blend_out": .15, "compensate_source": True}
DODGE_VALUES = {"distance": 420., "move_duration": .30, "perfect_window": .12,
                "invulnerability": .24, "recovery": .42, "reward_lock": 3.}
WHITELIST = frozenset((GA_DODGE, GA_SLOW, INPUT, CONTEXT, ACTION, *PAWNS))


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def prop(obj, name):
    require(obj is not None, "Null object reading " + name)
    try:
        return obj.get_editor_property(name)
    except Exception as error:
        raise RuntimeError(f"Cannot read {type(obj).__name__}.{name}: {error}") from error


def path(obj):
    return obj.get_path_name() if obj else None


def package(obj):
    return path(obj).split(".")[0] if obj else None


def load(name):
    obj = u.load_asset(name)
    require(obj, "AssetLoadFailed: " + name)
    return obj


def native(name):
    cls = u.load_class(None, name)
    require(cls, "Native class missing; build/restart Editor first: " + name)
    return cls


def cdo(name):
    cls = u.EditorAssetLibrary.load_blueprint_class(name)
    require(cls, "BlueprintClassLoadFailed: " + name)
    return u.get_default_object(cls)


def tag(name):
    result = u.GameplayTag()
    result.import_text('(TagName="' + name + '")')
    require(tag_name(result) == name, "Tag did not survive import: " + name)
    return result


def tag_name(value):
    matches = re.findall(r'TagName="([^"]*)"', value.export_text())
    require(len(matches) == 1, "Unexpected GameplayTag export: " + value.export_text())
    return matches[0]


def ability_tags(obj):
    return re.findall(r'TagName="([^"]*)"', prop(obj, "ability_tags").export_text())


def class_path(asset):
    return asset + "." + asset.rsplit("/", 1)[1] + "_C"


def disk_path(asset):
    require(asset.startswith("/Game/"), "Only /Game packages supported: " + asset)
    return ROOT / "Content" / (asset[len("/Game/"):] + ".uasset")


def sha256(file):
    return hashlib.sha256(file.read_bytes()).hexdigest() if file.exists() else None


def new_report(kind):
    name = kind + "-" + dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S") + "-" + uuid.uuid4().hex[:8]
    directory = ROOT / "Saved/GAAuthoring/PerfectDodge" / name
    directory.mkdir(parents=True, exist_ok=False)
    return directory


def write_json(file, value):
    file.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def dirty_packages():
    return [obj.get_name() for obj in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
            list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())]


def mapping_rows(context):
    return list(prop(prop(context, "default_key_mappings"), "mappings"))


def mapping_snapshot(context):
    result = []
    for row in mapping_rows(context):
        text = prop(row, "key").export_text()
        # FKey has a custom text exporter: the inspected UE 5.8 assets export D/SpaceBar directly.
        require(re.fullmatch(r"[A-Za-z0-9_]+", text), "Cannot resolve physical mapping key: " + text)
        result.append({"action": package(prop(row, "action")), "key": text, "export": row.export_text()})
    return result


def input_snapshot():
    return [{"action": package(prop(row, "input_action")), "tag": tag_name(prop(row, "input_tag")),
             "export": row.export_text()} for row in prop(load(INPUT), "ability_input_actions")]


def make_input_row(action):
    """Import a detached EditDefaultsOnly struct, then validate before assigning the owning array."""
    require(isinstance(action, u.InputAction), "Input row needs an InputAction")
    row = u.MyInputAction()
    row.import_text('(InputAction="' + path(action) + '",InputTag=(TagName="' + DODGE_TAG + '"))')
    require(prop(row, "input_action") == action and tag_name(prop(row, "input_tag")) == DODGE_TAG,
            "MyInputAction text import failed: " + row.export_text())
    return row


def blueprint_status(blueprint):
    status = str(prop(blueprint, "status"))
    normalized = status.upper().replace("_", "").replace(" ", "")
    require("UPTODATE" in normalized and "ERROR" not in normalized,
            "Blueprint did not compile: " + path(blueprint) + ": " + status)
    return status


def routes():
    """Native SetupInputComponent selects MassInputConfig for PureMass descendants."""
    result = {}
    candidates = [("base_player_controller", native(
        "/Game/BluePrints/Player/BP_PlayerController.BP_PlayerController_C"), None)]
    for name, pawn in MODES.items():
        mode = cdo(name)
        require(path(prop(mode, "default_pawn_class")) == class_path(pawn), "GameMode Pawn changed: " + name)
        candidates.append((name, prop(mode, "player_controller_class"), pawn))
    pure_mass = getattr(u, "PureMassPlayerController", None)
    require(pure_mass, "PureMassPlayerController Python class missing")
    for name, controller_class, pawn in candidates:
        require(controller_class, "Missing controller: " + name)
        controller = u.get_default_object(controller_class)
        config_property = "mass_input_config" if isinstance(controller, pure_mass) else "input_config"
        context = package(prop(controller, "player_context"))
        config = package(prop(controller, config_property))
        result[name] = {"pawn": pawn, "controller": path(controller_class), "context": context,
                        "config_property": config_property, "config": config}
        require(context == CONTEXT and config == INPUT, "Unexpected effective input route: " + str(result[name]))
    return result


def catalogs():
    """Inventory all AbilityInfo assets, including the actual GameMode catalogs."""
    names = {"/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo", "/Game/PureMass/DA_MassAbilityInfo",
             "/Game/BluePrints/Characters/Stander/DA_SwordAbilityInfo"}
    registry = u.AssetRegistryHelpers.get_asset_registry()
    # TopLevelAssetPath.__str__ is a struct display, not a canonical class path.
    # Use the class query already exercised by Scripts/UI/inspect_dungeon_skills.py.
    registry.search_all_assets(True)
    for data in registry.get_assets_by_class(u.TopLevelAssetPath("/Script/test_25d", "AbilityInfo"), True):
        if str(data.package_name).startswith("/Game/"):
            names.add(str(data.package_name))
    for mode in MODES:
        names.add(package(prop(cdo(mode), "ability_info")))
    require(None not in names, "GameMode is missing its AbilityInfo")
    return {name: {"rows": [row.export_text() for row in prop(load(name), "ability_information")],
                   "sha256": sha256(disk_path(name))} for name in sorted(names)}


def snapshot():
    return {
        "routes": routes(),
        "startup": {name: [path(cls) for cls in prop(cdo(name), "startup_ability")] for name in PAWNS},
        "passive": {name: [path(cls) for cls in prop(cdo(name), "startup_passive_ability")] for name in PAWNS},
        "input": input_snapshot(), "mappings": mapping_snapshot(load(CONTEXT)),
        "mapping_profiles": prop(load(CONTEXT), "mapping_profile_overrides").export_text()
            if hasattr(prop(load(CONTEXT), "mapping_profile_overrides"), "export_text")
            else str(prop(load(CONTEXT), "mapping_profile_overrides")),
        "catalogs": catalogs(),
    }


def preflight(before):
    require(not prop(load(CONTEXT), "mapping_profile_overrides"),
            "IMC has profile overrides; inspect their effective keys before authoring")
    conflicts = [row for row in before["mappings"] if row["key"] == KEY and row["action"] != ACTION]
    require(not conflicts, "Space is already occupied; no existing mapping will be replaced: " + str(conflicts))
    dodge_mappings = [row for row in before["mappings"] if row["action"] == ACTION]
    require(len(dodge_mappings) <= 1 and all(row["key"] == KEY for row in dodge_mappings),
            "Conflicting or duplicate IA_Dodge mapping: " + str(dodge_mappings))
    matching = [row for row in before["input"] if row["tag"] == DODGE_TAG or row["action"] == ACTION]
    require(len(matching) <= 1 and all(row["tag"] == DODGE_TAG and row["action"] == ACTION for row in matching),
            "InputTag.Dodge / IA_Dodge already has a conflicting binding")
    for name, identity in IDENTITIES.items():
        parent = native(PARENTS[name])
        if u.EditorAssetLibrary.does_asset_exist(name):
            require(isinstance(load(name), u.Blueprint), "Existing asset is not a Blueprint: " + name)
            require(u.BlueprintEditorLibrary.get_blueprint_parent_class(load(name)) == parent,
                    "Existing Blueprint has a different parent: " + name)
            require(ability_tags(cdo(name)) == [identity], "Existing GA has a different identity: " + name)
    native("/Script/test_25d.PerfectDodgeCooldownEffect")
    native("/Script/test_25d.WorldSlowCooldownEffect")
    # Validate the reflection/export APIs before any apply run can save a package.
    validate_cooldown("/Script/test_25d.PerfectDodgeCooldownEffect", .8, "Cooldown.Common.PerfectDodge")
    validate_cooldown("/Script/test_25d.WorldSlowCooldownEffect", 3., "Cooldown.Time.WorldSlow")
    require(before["input"] and before["input"][0]["action"], "No existing input action for struct import preflight")
    # This temporary row is never assigned; fail early if the engine rejects the struct import API.
    make_input_row(load(before["input"][0]["action"]))
    for asset in (AIR, MANA):
        require(isinstance(load(asset), u.NiagaraSystem), "Effect must be NiagaraSystem: " + asset)
    if u.EditorAssetLibrary.does_asset_exist(ACTION):
        action = load(ACTION)
        require(isinstance(action, u.InputAction), "Existing IA_Dodge has a different class")
        require(prop(action, "value_type") == u.InputActionValueType.BOOLEAN and
                not prop(action, "triggers") and not prop(action, "modifiers"),
                "Existing IA_Dodge has different semantics; inspect before updating")
    for name, item in before["catalogs"].items():
        for row in item["rows"]:
            require(not any(identity in row or asset in row for asset, identity in IDENTITIES.items()),
                    "Unexpected learnable registration (will not edit catalog): " + name)
    for pawn in PAWNS:
        require(not any(cls in before["passive"][pawn] for cls in (class_path(GA_DODGE), class_path(GA_SLOW))),
                "New active GA already appears in passive grants: " + pawn)
        for ability in prop(cdo(pawn), "startup_ability"):
            if not ability:
                continue
            tags = ability_tags(u.get_default_object(ability))
            require(IDENTITIES[GA_SLOW] not in tags, "WorldSlow must not be automatically granted: " + pawn)
            require(IDENTITIES[GA_DODGE] not in tags or path(ability) == class_path(GA_DODGE),
                    "Another startup class already uses the PerfectDodge identity: " + pawn)
            require(tag_name(prop(u.get_default_object(ability), "startup_input_tag")) != DODGE_TAG or
                    path(ability) == class_path(GA_DODGE), "Another startup GA occupies InputTag.Dodge: " + pawn)


def validate_values(settings, expected):
    for key, value in expected.items():
        actual = prop(settings, key)
        require(actual == value if isinstance(value, bool) else abs(float(actual) - value) < 1e-5,
                f"Incorrect {key}: {actual}; expected {value}")


def validate_cooldown(name, seconds, granted_tag=None):
    effect = u.get_default_object(native(name))
    require(prop(effect, "duration_policy") == u.GameplayEffectDurationType.HAS_DURATION,
            "Cooldown must have duration: " + name)
    magnitude = prop(effect, "duration_magnitude").export_text()
    values = re.findall(r"ScalableFloatMagnitude=\(Value=([-+0-9.eE]+)", magnitude)
    require(len(values) == 1 and abs(float(values[0]) - seconds) < 1e-5,
            "Incorrect cooldown duration: " + magnitude)
    require("MagnitudeCalculationType=ScalableFloat" in magnitude, "Cooldown uses non-scalable magnitude")
    if granted_tag:
        components = [item for item in prop(effect, "ge_components") if item and
                      item.get_class().get_name() == "TargetTagsGameplayEffectComponent"]
        require(len(components) == 1, "Expected native cooldown TargetTags component: " + name)
        tags = prop(components[0], "InheritableGrantedTagsContainer")
        require(set(re.findall(r'TagName="([^"]*)"', tags.export_text())) == {granted_tag},
                "Incorrect native cooldown granted tags: " + tags.export_text())


def validate_authored():
    for asset, parent in PARENTS.items():
        blueprint_status(load(asset))
        require(u.BlueprintEditorLibrary.get_blueprint_parent_class(load(asset)) == native(parent),
                "Wrong native parent: " + asset)
        ability = cdo(asset)
        require(ability_tags(ability) == [IDENTITIES[asset]], "Wrong identity: " + asset)
        require(prop(ability, "cost_gameplay_effect_class") is None, "Unexpected mana cost: " + asset)
        require(prop(ability, "net_execution_policy") == u.GameplayAbilityNetExecutionPolicy.SERVER_ONLY,
                "Wrong network policy: " + asset)
    dodge = cdo(GA_DODGE)
    require(tag_name(prop(dodge, "startup_input_tag")) == DODGE_TAG, "Wrong fixed Dodge input tag")
    validate_values(dodge, {"cooldown_seconds": .8})
    require(path(prop(dodge, "cooldown_gameplay_effect_class")) == "/Script/test_25d.PerfectDodgeCooldownEffect",
            "Dodge must use its native dedicated cooldown")
    validate_cooldown("/Script/test_25d.PerfectDodgeCooldownEffect", .8, "Cooldown.Common.PerfectDodge")
    settings = prop(dodge, "dodge_settings")
    validate_values(settings, DODGE_VALUES)
    validate_values(prop(settings, "reward"), SLOW_VALUES)
    require(package(prop(settings, "dodge_effect")) == AIR, "Missing ordinary dodge effect")
    require(package(prop(settings, "perfect_effect")) == MANA, "Missing perfect success effect")
    slow = cdo(GA_SLOW)
    validate_values(prop(slow, "settings"), SLOW_VALUES)
    require(tag_name(prop(slow, "startup_input_tag")) == "", "WorldSlow has an automatic input binding")
    require(path(prop(slow, "cooldown_gameplay_effect_class")) == "/Script/test_25d.WorldSlowCooldownEffect",
            "WorldSlow must use its native dedicated cooldown")
    validate_cooldown("/Script/test_25d.WorldSlowCooldownEffect", 3., "Cooldown.Time.WorldSlow")
    action = load(ACTION)
    require(isinstance(action, u.InputAction), "IA_Dodge is not an InputAction")
    require(prop(action, "value_type") == u.InputActionValueType.BOOLEAN, "IA_Dodge must be Boolean")
    require(not prop(action, "triggers") and not prop(action, "modifiers"), "IA_Dodge has unexpected processing")
    current = snapshot()
    preflight(current)
    for pawn, abilities in current["startup"].items():
        blueprint_status(load(pawn))
        require(abilities.count(class_path(GA_DODGE)) == 1, "Expected one effective Dodge startup class: " + pawn)
        require(class_path(GA_SLOW) not in abilities, "WorldSlow auto-granted: " + pawn)
        require(not any(cls in current["passive"][pawn] for cls in (class_path(GA_DODGE), class_path(GA_SLOW))),
                "New active GA inserted into passive grants: " + pawn)
    require(sum(row["tag"] == DODGE_TAG and row["action"] == ACTION for row in current["input"]) == 1,
            "Expected one InputConfig Dodge row")
    matches = [row for row in mapping_rows(load(CONTEXT)) if package(prop(row, "action")) == ACTION]
    require(len(matches) == 1 and not prop(matches[0], "triggers") and not prop(matches[0], "modifiers"),
            "Expected one unmodified Space mapping for Dodge")
    return current


def validate_preserved(before, after):
    require(before["routes"] == after["routes"], "Effective controllers/input routes changed")
    require(before["catalogs"] == after["catalogs"], "Learnable catalogs changed")
    require(before["passive"] == after["passive"], "Passive startup grants changed")
    require(before["mapping_profiles"] == after["mapping_profiles"], "Input profile overrides changed")
    for pawn in PAWNS:
        old = [value for value in before["startup"][pawn] if value != class_path(GA_DODGE)]
        new = [value for value in after["startup"][pawn] if value != class_path(GA_DODGE)]
        require(old == new, "Other effective startup grants changed: " + pawn)
    require([row for row in before["input"] if row["tag"] != DODGE_TAG] ==
            [row for row in after["input"] if row["tag"] != DODGE_TAG], "Existing InputConfig rows changed")
    require([row for row in before["mappings"] if row["action"] != ACTION] ==
            [row for row in after["mappings"] if row["action"] != ACTION], "Existing IMC mappings changed")
