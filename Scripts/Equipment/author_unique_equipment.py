"""Author missing fixed uniques, then verify in a separate Unreal process.

Requires a rebuilt UE 5.8 project with UniqueEquipmentGameplayEffect. Run once
with the editor closed; then run in another commandlet process with
UNIQUE_EQUIPMENT_VERIFY_ONLY=1. Existing designer values are never reset.
This script does not edit maps, drop tables, the item catalog, or player defaults.
unique_equipment_catalog.json supplies identities, structural fields and missing
row defaults. Designer values in the existing tables are preserved on reruns.
"""
import copy
import hashlib
import json
import math
import os
from datetime import datetime, timezone
from pathlib import Path
import re
import shutil
import traceback

import unreal as u


BASE = "/Game/Equipment/Unique"
CURVE_PATH = BASE + "/CT_UniqueEquipmentStats"
TABLE_PATH = "/Game/Equipment/DT_Equipment"
CATALOG_PATH = "/Game/BluePrints/DA/DA_BackpackItemDefinitions"
MANIFEST_PATH = Path(u.Paths.project_dir()).resolve() / "Scripts/Equipment/unique_equipment_catalog.json"
VERIFY_ONLY = os.environ.get("UNIQUE_EQUIPMENT_VERIFY_ONLY") == "1"
TOOLS = u.AssetToolsHelpers.get_asset_tools()
ASSETS = u.EditorAssetLibrary
TABLES = u.DataTableFunctionLibrary
SAVED = Path(u.Paths.project_saved_dir()).resolve() / "Equipment"
CONTENT = Path(u.Paths.project_content_dir()).resolve()
RUN_ID = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ") + "_" + str(os.getpid())
RUN_DIR = SAVED / "UniqueEquipment" / RUN_ID
RUN_DIR.mkdir(parents=True, exist_ok=True)

ITEMS = []  # Filled only from the validated manifest; existing table values remain authoritative.

REPORT = dict(run_id=RUN_ID, process_id=os.getpid(), verify_only=VERIFY_ONLY,
              started_utc=datetime.now(timezone.utc).isoformat(), success=False,
              created_assets=[], changed_assets=[], added_curve_rows=[], added_item_rows=[],
              backups=[], items=[], verification_scope="asset structure and saved references; not gameplay/PIE")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def unique_json_keys(pairs):
    """Reject duplicate object keys before json.loads silently replaces their values."""
    result = {}
    for name, value in pairs:
        require(name not in result, "Duplicate manifest field: " + name)
        result[name] = value
    return result


def validate_catalog(document):
    """Validate the authoring contract before any package is changed; defaults do not overwrite tuning."""
    require(isinstance(document, dict) and set(document) == {"schema_version", "items"}, "Invalid catalog root fields")
    require(type(document["schema_version"]) is int and document["schema_version"] == 1, "Unsupported catalog schema_version")
    items = document["items"]
    require(isinstance(items, list) and items, "Catalog items must be a nonempty array")
    attributes = {"Strength", "Intelligence", "Resilience", "Vigor", "Armor", "ArmorPene", "BlockChance",
                  "CriticalHitChance", "CriticalHitDamage", "CriticalHitResistence", "HealthRegeneration",
                  "ManaRegeneration", "MaxHealth", "MaxMana", "FireResistance", "ThunderResistance",
                  "WaterResistance", "MagicResistance", "PhysicsResistance", "PureResistance"}
    slots = {"Weapon", "Ring.Left", "Ring.Right", "Chest", "Boots", "Helmet", "MagicStone", "Element.1", "Element.2", "Element.3"}
    damage_types = {"Damage.Fire", "Damage.Thunder", "Damage.Water", "Damage.Magic", "Damage.Physics", "Damage.Pure"}
    required = {"id", "label", "icon", "slots", "stats", "description"}
    optional = {"tag", "set_description", "projectile", "projectile_ability_tag", "disable_homing",
                "required_set_tags", "melee", "melee_ability_tag", "damage_type", "hit_effect", "body_effect",
                "spell", "spell_ability_tag", "spell_family_tag"}
    projectile_fields = {"ExtraProjectiles", "DamageMultiplier", "MinimumSpread", "SetDamageMultiplier"}
    melee_fields = {"DamageMultiplier", "SetDamageMultiplier", "BodyEffectDuration"}
    spell_fields = {"DamageMultiplier", "RadiusMultiplier", "ExtraChainTargets"}
    area_family = "Equipment.AbilityFamily.ArcaneArea"
    chain_family = "Equipment.AbilityFamily.LightningChain"
    spell_members = {"Abilities.Arcane.ArcaneNova": area_family, "Abilities.Arcane.ArcaneShards": area_family,
                     "Abilities.Thunder.Electrocute": chain_family}
    identities, curve_names, piece_tags = set(), set(), {}
    for item in items:
        require(isinstance(item, dict) and required <= set(item) and set(item) <= required | optional,
                "Missing or unknown item fields: " + repr(item))
        identity = item["id"]
        require(isinstance(identity, str) and re.fullmatch(r"Unique[A-Za-z0-9_]+", identity), "Invalid unique ID")
        require(identity.casefold() not in identities, "Duplicate item ID: " + identity)
        identities.add(identity.casefold())
        for field in ("label", "description"):
            require(isinstance(item[field], str) and item[field].strip(), "Missing " + field + ": " + identity)
        require(isinstance(item["icon"], str) and re.fullmatch(r"[A-Za-z0-9_]+", item["icon"]), "Invalid icon name")
        allowed_slots = item["slots"]
        require(isinstance(allowed_slots, list) and allowed_slots and all(isinstance(slot, str) for slot in allowed_slots), "Invalid slots")
        require(len(set(allowed_slots)) == len(allowed_slots) and set(allowed_slots) <= slots, "Unknown or duplicate equipment slot")
        require(isinstance(item["stats"], dict) and set(item["stats"]) <= attributes, "Unknown attribute mapping")
        if "tag" in item:
            tag = item["tag"]
            require(isinstance(tag, str) and re.fullmatch(r"Equipment\.Unique\.[A-Za-z0-9_.]+", tag), "Invalid piece tag")
            require(tag.casefold() not in piece_tags, "Two item identities cannot grant the same set piece tag")
            piece_tags[tag.casefold()] = identity
        if "set_description" in item:
            require(isinstance(item["set_description"], str) and item["set_description"].strip(), "Invalid set description")
        rule_groups = [group for group in ("projectile", "melee", "spell") if group in item]
        require(len(rule_groups) <= 1, "One item may declare at most one skill rule")
        require(item["stats"] or rule_groups, "An item must have attributes or an implemented skill rule")
        require(not ("projectile_ability_tag" in item or "disable_homing" in item) or "projectile" in item,
                "Projectile metadata has no projectile rule")
        require(not any(field in item for field in ("melee_ability_tag", "damage_type", "hit_effect", "body_effect"))
                or "melee" in item,
                "Melee metadata has no melee rule")
        require(not ("spell_ability_tag" in item or "spell_family_tag" in item) or "spell" in item,
                "Spell selector has no rule")
        if "projectile" in item:
            require(isinstance(item["projectile"], dict) and set(item["projectile"]) == projectile_fields, "Invalid projectile numeric fields")
            require(type(item.get("disable_homing")) is bool, "disable_homing must be boolean")
        if "melee" in item:
            require(isinstance(item["melee"], dict) and "DamageMultiplier" in item["melee"] and set(item["melee"]) <= melee_fields,
                    "Invalid melee numeric fields")
            require(isinstance(item.get("damage_type"), str) and item["damage_type"] in damage_types, "Unknown melee damage type")
            for field in ("hit_effect", "body_effect"):
                effect = item.get(field)
                require(effect is None or (isinstance(effect, str) and re.fullmatch(r"/Game/[A-Za-z0-9_/]+", effect)),
                        "Invalid Niagara asset path in " + field)
            require(bool(item.get("body_effect")) == ("BodyEffectDuration" in item["melee"]),
                    "Body effect and duration must be authored together")
        if "spell" in item:
            require(isinstance(item["spell"], dict) and spell_fields <= set(item["spell"]) and
                    set(item["spell"]) <= spell_fields | {"SetDamageMultiplier"}, "Invalid spell numeric fields")
            ability_tag = item.get("spell_ability_tag")
            family_tag = item.get("spell_family_tag")
            require((isinstance(ability_tag, str) and ability_tag in spell_members and family_tag is None) or
                    (ability_tag is None and family_tag in (area_family, chain_family)),
                    "Spell rule requires exactly one registered ability or family selector")
            selected_family = spell_members[ability_tag] if ability_tag else family_tag
            require(selected_family == chain_family or item["spell"]["ExtraChainTargets"] == 0,
                    "Only the LightningChain family accepts extra chain targets")
        for group in rule_groups:
            if group == "spell":
                continue
            tag = item.get(group + "_ability_tag")
            require(isinstance(tag, str) and re.fullmatch(r"Abilities\.[A-Za-z0-9_.]+", tag), "Missing exact ability identity")
        set_tags = item.get("required_set_tags", [])
        require(isinstance(set_tags, list) and all(isinstance(tag, str) for tag in set_tags), "Invalid set tags")
        require(len({tag.casefold() for tag in set_tags}) == len(set_tags), "Repeated tags cannot count as different pieces")
        if set_tags:
            require(len(rule_groups) == 1 and len(set_tags) >= 2 and item.get("tag") in set_tags,
                    "A set reward needs one rule consumer, at least two different tags, and its own piece")
            require("SetDamageMultiplier" in item[rule_groups[0]], "Missing set damage curve")
        else:
            require("required_set_tags" not in item or rule_groups, "Set condition metadata has no rule")
            require(all(item[group].get("SetDamageMultiplier", 1) == 1 for group in rule_groups), "Set multiplier without a set condition")
        for group in ("stats", *rule_groups):
            for field, value in item[group].items():
                require(type(value) in (int, float) and math.isfinite(value), "Nonfinite/non-numeric initial curve value")
                if field == "ExtraProjectiles":
                    valid = 0 <= value <= 16
                elif field == "MinimumSpread":
                    valid = 0 <= value <= 360
                elif field == "RadiusMultiplier":
                    valid = .5 <= value <= 2
                elif field == "ExtraChainTargets":
                    valid = 0 <= value <= 8
                elif field == "BodyEffectDuration":
                    valid = .1 <= value <= 3
                else:
                    valid = 0.05 <= value <= 10
                require(valid, "Initial curve value outside supported range: " + identity + "." + field)
                name = (identity + "_" + field).casefold()
                require(name not in curve_names, "Duplicate curve row across numeric groups: " + name)
                curve_names.add(name)
    consumers = set()
    for item in items:
        set_tags = item.get("required_set_tags", [])
        if set_tags:
            key = frozenset(tag.casefold() for tag in set_tags)
            require(key <= set(piece_tags), "Set references a piece not present in the catalog")
            require(key not in consumers, "The same set reward has multiple consumers")
            consumers.add(key)
    return items


def load_catalog():
    document = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"), object_pairs_hook=unique_json_keys)
    return validate_catalog(document)


def struct(cls, text):
    result = cls()
    result.import_text(text)
    return result


def asset_file(package_path):
    require(package_path.startswith("/Game/"), "Only project content is supported: " + package_path)
    result = (CONTENT / (package_path[6:] + ".uasset")).resolve()
    require(CONTENT in result.parents, "Asset path escaped Content: " + str(result))
    return result


def file_hash(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def backup(package_path):
    """Copy the existing package before its first mutation; never overwrite a backup."""
    require(not VERIFY_ONLY, "Verification mode must not write assets")
    source = asset_file(package_path)
    if not source.exists() or any(row["asset"] == package_path for row in REPORT["backups"]):
        return
    destination = RUN_DIR / "Backups" / source.relative_to(CONTENT)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    require(file_hash(source) == file_hash(destination), "Backup did not match: " + package_path)
    REPORT["backups"].append(dict(asset=package_path, file=str(destination), sha256=file_hash(source)))


def save(asset, package_path):
    require(not VERIFY_ONLY, "Verification mode must not save assets")
    require(ASSETS.save_loaded_asset(asset, False), "Save failed (possibly locked): " + package_path)
    require(asset_file(package_path).is_file(), "Saved package missing: " + package_path)
    if package_path not in REPORT["changed_assets"]:
        REPORT["changed_assets"].append(package_path)


def curve_row(item, field):
    return item["id"] + "_" + field


def scalable(row_name):
    return struct(u.ScalableFloat,
                  '(Value=1.0,Curve=(CurveTable="/Script/Engine.CurveTable\'' + CURVE_PATH +
                  '.CT_UniqueEquipmentStats\'",RowName="' + row_name + '"))')


def curve_keys(curve):
    """Read each row's actual keys; CurveTable JSON export can mislabel uneven key times."""
    return {str(name): [key.export_text() for key in TABLES.get_curve_table_keys(curve, name)]
            for name in TABLES.get_curve_table_row_names(curve)}


def ensure_curve():
    defaults = {curve_row(item, field): value for item in ITEMS
                for group in ("stats", "projectile", "melee", "spell")
                for field, value in item.get(group, {}).items()}
    curve = u.load_asset(CURVE_PATH)
    if curve is None:
        require(not VERIFY_ONLY, "Missing curve table: " + CURVE_PATH)
        # CSVImportFactory deterministically creates a SimpleCurve table; unlike
        # CurveTableFactory it needs no modal interpolation selection.
        csv_path = RUN_DIR / "CT_UniqueEquipmentStats.csv"
        csv_path.write_text("Name,1\n" + "".join(name + "," + str(value) + "\n"
                                               for name, value in defaults.items()), encoding="utf-8")
        settings = u.CSVImportSettings()
        settings.set_editor_property("import_type", u.CSVImportType.ECSV_CURVE_TABLE)
        factory = u.CSVImportFactory()
        factory.set_editor_property("automated_import_settings", settings)
        task = u.AssetImportTask()
        task.filename = str(csv_path)
        task.destination_path = BASE
        task.destination_name = "CT_UniqueEquipmentStats"
        task.factory = factory
        task.automated = True
        task.save = False
        TOOLS.import_asset_tasks([task])
        curve = u.load_asset(CURVE_PATH)
        require(isinstance(curve, u.CurveTable), "CurveTable import failed")
        REPORT["created_assets"].append(CURVE_PATH)
        REPORT["added_curve_rows"].extend(defaults)
        save(curve, CURVE_PATH)
    require(isinstance(curve, u.CurveTable), "Wrong asset type: " + CURVE_PATH)
    before = curve_keys(curve)
    missing = [name for name in defaults if name.casefold() not in {key.casefold() for key in before}]
    if missing:
        require(not VERIFY_ONLY, "Missing curve rows: " + str(missing))
        # AddSimpleCurve asserts on RichCurve tables. At least one nonempty result
        # proves SimpleCurves; otherwise stop safely instead of converting data.
        require(any(before.values()), "Cannot safely append to a rich/empty curve table; add missing rows in Editor")
        backup(CURVE_PATH)
        for name in missing:
            require(TABLES.add_simple_curve_to_table(curve, name), "Cannot add curve: " + name)
            require(TABLES.add_curve_table_key(curve, name, 1.0, defaults[name]), "Cannot add key: " + name)
        after = curve_keys(curve)
        require(all(after[name] == keys for name, keys in before.items()), "Existing curve keys changed")
        REPORT["added_curve_rows"].extend(missing)
        save(curve, CURVE_PATH)
    return curve


def effect_path(item):
    return BASE + "/Effects/GE_" + item["id"]


def make_modifier(item, attribute):
    return struct(u.GameplayModifierInfo,
                  '(Attribute=(AttributeName="' + attribute + '",Attribute=/Script/test_25d.MyAttributeSet:' +
                  attribute + ',AttributeOwner="/Script/CoreUObject.Class\'/Script/test_25d.MyAttributeSet\'"),'
                  'ModifierOp=MultiplyCompound,ModifierMagnitude=(MagnitudeCalculationType=ScalableFloat,'
                  'ScalableFloatMagnitude=' + scalable(curve_row(item, attribute)).export_text() + '),'
                  'EvaluationChannelSettings=(Channel=Channel2))')


def tag_container(tags):
    return struct(u.GameplayTagContainer,
                  '(GameplayTags=(' + ','.join('(TagName="' + tag + '")' for tag in tags) + '))')


def ensure_effect(item, parent):
    path = effect_path(item)
    blueprint = u.load_asset(path)
    fresh = blueprint is None
    if fresh:
        require(not VERIFY_ONLY, "Missing equipment effect: " + path)
        factory = u.BlueprintFactory()
        factory.set_editor_property("parent_class", parent)
        blueprint = TOOLS.create_asset("GE_" + item["id"], BASE + "/Effects", u.Blueprint, factory)
        require(blueprint is not None, "Cannot create effect: " + path)
        u.BlueprintEditorLibrary.compile_blueprint(blueprint)
    require(isinstance(blueprint, u.Blueprint), "Expected effect Blueprint: " + path)
    effect_class = ASSETS.load_blueprint_class(path)
    require(effect_class and u.MathLibrary.class_is_child_of(effect_class, parent), "Wrong effect parent: " + path)
    cdo = u.get_default_object(effect_class)
    if fresh:
        cdo.set_editor_property("modifiers", [make_modifier(item, attr) for attr in item["stats"]])
        cdo.set_editor_property("unique_description", item["description"])
        if "tag" in item:
            tags = tag_container([item["tag"]]).export_text()
            component_class = u.load_class(None, "/Script/GameplayAbilities.TargetTagsGameplayEffectComponent")
            require(component_class is not None, "Missing target tags component class")
            component = u.new_object(component_class, outer=cdo, name="UniqueIdentityTags")
            component.set_editor_property("InheritableGrantedTagsContainer", struct(u.InheritedTagContainer,
                '(CombinedTags=' + tags + ',Added=' + tags + ',Removed=(GameplayTags=()))'))
            cdo.set_editor_property("ge_components", [component])
        if "set_description" in item:
            cdo.set_editor_property("set_description", item["set_description"])
        if "projectile" in item:
            rule = cdo.get_editor_property("projectile_rule")
            rule.set_editor_property("ability_tag", struct(u.GameplayTag, '(TagName="' + item["projectile_ability_tag"] + '")'))
            for field in item["projectile"]:
                rule.set_editor_property(field, scalable(curve_row(item, field)))
            rule.set_editor_property("bDisableHoming", item["disable_homing"])
            rule.set_editor_property("required_set_tags", tag_container(item.get("required_set_tags", [])))
            cdo.set_editor_property("projectile_rule", rule)
        if "melee" in item:
            rule = cdo.get_editor_property("melee_rule")
            rule.set_editor_property("ability_tag", struct(u.GameplayTag, '(TagName="' + item["melee_ability_tag"] + '")'))
            for field in item["melee"]:
                rule.set_editor_property(field, scalable(curve_row(item, field)))
            rule.set_editor_property("required_set_tags", tag_container(item.get("required_set_tags", [])))
            rule.set_editor_property("damage_type_override", struct(u.GameplayTag, '(TagName="' + item["damage_type"] + '")'))
            rule.set_editor_property("hit_effect", u.load_asset(item["hit_effect"]) if item.get("hit_effect") else None)
            if item.get("body_effect"):
                rule.set_editor_property("body_effect", u.load_asset(item["body_effect"]))
            cdo.set_editor_property("melee_rule", rule)
        if "spell" in item:
            rule = cdo.get_editor_property("spell_rule")
            for selector in ("ability_tag", "family_tag"):
                if "spell_" + selector in item:
                    rule.set_editor_property(selector, struct(u.GameplayTag, '(TagName="' + item["spell_" + selector] + '")'))
            for field in item["spell"]:
                rule.set_editor_property(field, scalable(curve_row(item, field)))
            rule.set_editor_property("required_set_tags", tag_container(item.get("required_set_tags", [])))
            cdo.set_editor_property("spell_rule", rule)
        u.BlueprintEditorLibrary.compile_blueprint(blueprint)
        REPORT["created_assets"].append(path)
        save(blueprint, path)
    return u.get_default_object(ASSETS.load_blueprint_class(path))


def canonical_rows(rows, identities):
    """PrimaryAssetType exports an empty JSON object; read its real text column."""
    result = copy.deepcopy(rows)
    for row in result:
        identity = identities[row["Name"]]
        if identity.startswith('"'):
            identity = json.loads(identity)
        row["ItemDefinitionId"] = identity
    return {row["Name"]: row for row in result}


def read_rows(table):
    names = list(map(str, TABLES.get_data_table_row_names(table)))
    identities = list(TABLES.get_data_table_column_as_string(table, "ItemDefinitionId"))
    require(len(names) == len(identities), "Could not export complete item identity column")
    return canonical_rows(json.loads(TABLES.export_data_table_to_json_string(table)), dict(zip(names, identities)))


def ensure_definitions(table):
    before = read_rows(table)
    additions = []
    existing_names = {name.casefold() for name in before}
    existing_ids = {row["ItemDefinitionId"] for row in before.values()}
    for item in ITEMS:
        if item["id"].casefold() in existing_names:
            continue
        require("BackpackItem:" + item["id"] not in existing_ids,
                "Definition ID already belongs to a different row: " + item["id"])
        path = effect_path(item)
        additions.append(dict(Name=item["id"], ItemDefinitionId="BackpackItem:" + item["id"],
                              DisplayName=item["label"], Icon="/Game/Assets/EquipmentIcon/" + item["icon"] + "." + item["icon"],
                              WorldMesh="None", PickupActorClass="/Script/test_25d.EquipmentSoulPickup", PickupMode="Interact",
                              bStackable=False, MaxStack=1, bEquippable=True, bRandomizedEquipment=False,
                              GenerationProfile="None", WeaponSet="None",
                              AllowedEquipmentSlots=dict(GameplayTags=[dict(TagName="Equipment.Slot." + slot)
                                                                      for slot in item["slots"]]),
                              EquipEffect=path + ".GE_" + item["id"] + "_C", EquipEffectLevel=1.0,
                              bUsable=False, UseEffect="None"))
    if additions:
        require(not VERIFY_ONLY, "Missing equipment definitions: " + str([row["Name"] for row in additions]))
        backup(TABLE_PATH)
        (RUN_DIR / "DT_Equipment_before.json").write_text(json.dumps(before, ensure_ascii=False, indent=2), encoding="utf-8")
        require(TABLES.fill_data_table_from_json_string(table,
                json.dumps(list(before.values()) + additions, ensure_ascii=False)), "Equipment table import failed")
        after = read_rows(table)
        require(all(after.get(name) == row for name, row in before.items()), "Existing equipment rows changed; refusing save")
        save(table, TABLE_PATH)
        REPORT["added_item_rows"].extend(row["Name"] for row in additions)
    REPORT["preserved_equipment_rows"] = len(before)


def evaluate(scalable_value, curve, expected_row, level):
    handle = scalable_value.get_editor_property("curve")
    require(handle.get_editor_property("curve_table") == curve, "Unexpected curve reference: " + expected_row)
    require(str(handle.get_editor_property("row_name")) == expected_row, "Unexpected curve row: " + expected_row)
    result, value = TABLES.evaluate_curve_table_row(curve, expected_row, level, "UniqueEquipmentAuthor")
    require(result == u.EvaluateCurveTableResult.ROW_FOUND, "Curve row could not be evaluated: " + expected_row)
    magnitude = float(scalable_value.get_editor_property("value")) * float(value)
    require(math.isfinite(magnitude), "Nonfinite magnitude: " + expected_row)
    return magnitude


def tags_in(value):
    return set(re.findall(r'TagName="([^"]+)"', value.export_text()))


def validate_set_rule(item, group, rule, curve, level):
    """Structural conditions must match the manifest; current table multipliers may differ from defaults."""
    expected_tags = set(item.get("required_set_tags", []))
    require(tags_in(rule.get_editor_property("required_set_tags")) == expected_tags, "Unexpected set condition tags")
    numeric = {field: evaluate(rule.get_editor_property(field), curve, curve_row(item, field), level)
               for field in item[group]}
    if "SetDamageMultiplier" not in numeric:
        value = rule.get_editor_property("set_damage_multiplier")
        handle = value.get_editor_property("curve")
        require(float(value.get_editor_property("value")) == 1 and not handle.get_editor_property("curve_table"),
                "A rule without a set curve must retain the default set multiplier")
    for field, value in numeric.items():
        if field == "ExtraProjectiles":
            require(0 <= value <= 16, "Extra projectile count outside [0, 16]")
        elif field == "MinimumSpread":
            require(0 <= value <= 360, "Invalid spread")
        elif field == "RadiusMultiplier":
            require(.5 <= value <= 2, "Invalid spell radius multiplier")
        elif field == "ExtraChainTargets":
            require(0 <= value <= 8, "Invalid extra chain target count")
        elif field == "BodyEffectDuration":
            require(.1 <= value <= 3, "Invalid body effect duration")
        else:
            require(0.05 <= value <= 10, "Invalid skill damage multiplier")
    if not expected_tags:
        require(numeric.get("SetDamageMultiplier", 1) == 1, "Runtime set multiplier needs a set condition")
    return numeric


def validate_item(item, row, cdo, curve):
    require(row["ItemDefinitionId"] == "BackpackItem:" + item["id"], "Identity mismatch: " + item["id"])
    require(row["bEquippable"] and not row["bStackable"] and row["MaxStack"] == 1, "Invalid equipment flags")
    require(not row["bRandomizedEquipment"] and not row["bUsable"], "Unique must be fixed and nonconsumable")
    require("EquipmentSoulPickup" in row["PickupActorClass"] and row["PickupMode"] == "Interact", "Invalid Soul pickup")
    require(effect_path(item) + ".GE_" + item["id"] + "_C" in row["EquipEffect"], "Unexpected GE reference")
    icon_path = row["Icon"].split("'", 2)[1] if "'" in row["Icon"] else row["Icon"]
    require(isinstance(u.load_asset(icon_path), u.Texture2D), "Invalid icon: " + item["id"])
    slot_tags = row["AllowedEquipmentSlots"]["GameplayTags"]
    require({tag["TagName"] for tag in slot_tags} == {"Equipment.Slot." + slot for slot in item["slots"]}, "Unexpected slots")
    level = float(row["EquipEffectLevel"])
    require(math.isfinite(level) and level >= 1, "Invalid equipment level")
    require(cdo.get_editor_property("duration_policy") == u.GameplayEffectDurationType.INFINITE, "GE must be Infinite")
    require(float(cdo.get_editor_property("period").get_editor_property("value")) == 0, "Periodic GE is forbidden")
    require(cdo.get_editor_property("stacking_type") == u.GameplayEffectStackingType.NONE, "Stacking GE is forbidden")
    require(not cdo.get_editor_property("executions"), "Execution GE is forbidden")
    modifiers = list(cdo.get_editor_property("modifiers"))
    require(len(modifiers) == len(item["stats"]), "Unexpected modifier structure: " + item["id"])
    actual = {}
    for modifier, attribute in zip(modifiers, item["stats"]):
        text = modifier.export_text()
        require("AttributeName=\"" + attribute + "\"" in text and "ModifierOp=MultiplyCompound" in text
                and "Channel=Channel2" in text, "Unexpected modifier: " + text)
        magnitude = modifier.get_editor_property("modifier_magnitude")
        require("MagnitudeCalculationType=ScalableFloat" in magnitude.export_text(), "Expected scalable magnitude")
        value = evaluate(magnitude.get_editor_property("scalable_float_magnitude"), curve, curve_row(item, attribute), level)
        require(0.05 <= value <= 10, "Attribute multiplier outside [0.05, 10]")
        actual[attribute] = value
    granted = set()
    for component in cdo.get_editor_property("ge_components"):
        require(component and component.get_class().get_name() == "TargetTagsGameplayEffectComponent", "Unsupported GE component")
        inherited = component.get_editor_property("InheritableGrantedTagsContainer")
        # Reading text avoids relying on private GameplayTagContainer array reflection.
        granted.update(re.findall(r'TagName="([^"]+)"', inherited.get_editor_property("combined_tags").export_text()))
    require(granted == ({item["tag"]} if "tag" in item else set()), "Unexpected granted unique tags")
    for group in ("projectile", "melee", "spell"):
        if group not in item:
            rule = cdo.get_editor_property(group + "_rule")
            require(rule.export_text() == type(rule)().export_text(),
                    "Undeclared " + group + " rule remains on " + item["id"] +
                    "; use a targeted migration instead of clearing an existing designer asset")
    # Text is designer-owned like display names: omitting an initial description
    # does not authorize deleting edited wording. Behavior fields are checked above.
    details = dict(id=item["id"], label=row["DisplayName"], level=level, multipliers=actual, granted_tags=sorted(granted))
    if "projectile" in item:
        rule = cdo.get_editor_property("projectile_rule")
        require(tags_in(rule.get_editor_property("ability_tag")) == {item["projectile_ability_tag"]}, "Unexpected projectile ability")
        require(rule.get_editor_property("bDisableHoming") == item["disable_homing"], "Unexpected homing behavior")
        details["projectile"] = validate_set_rule(item, "projectile", rule, curve, level)
    if "melee" in item:
        rule = cdo.get_editor_property("melee_rule")
        require(tags_in(rule.get_editor_property("ability_tag")) == {item["melee_ability_tag"]}, "Unexpected melee ability")
        require(tags_in(rule.get_editor_property("damage_type_override")) == {item["damage_type"]}, "Unexpected damage type")
        numeric = validate_set_rule(item, "melee", rule, curve, level)
        hit_effect = rule.get_editor_property("hit_effect")
        if item.get("hit_effect"):
            require(isinstance(hit_effect, u.NiagaraSystem), "Missing melee impact NiagaraSystem")
            require(hit_effect == u.load_asset(item["hit_effect"]), "Unexpected melee hit effect")
        else:
            require(hit_effect is None, "Unexpected melee impact effect")
        body_effect = rule.get_editor_property("body_effect")
        if item.get("body_effect"):
            require(isinstance(body_effect, u.NiagaraSystem) and body_effect == u.load_asset(item["body_effect"]),
                    "Unexpected melee body effect")
        else:
            duration = rule.get_editor_property("body_effect_duration")
            require(body_effect is None and duration.export_text() ==
                    type(rule)().get_editor_property("body_effect_duration").export_text(),
                    "Unexpected body effect or duration")
        details["melee"] = dict(numeric=numeric, damage_type=item["damage_type"],
                                hit_effect=hit_effect.get_path_name() if hit_effect else None,
                                body_effect=body_effect.get_path_name() if body_effect else None)
    if "spell" in item:
        rule = cdo.get_editor_property("spell_rule")
        for selector in ("ability_tag", "family_tag"):
            expected = item.get("spell_" + selector)
            require(tags_in(rule.get_editor_property(selector)) == ({expected} if expected else set()),
                    "Unexpected spell selector: " + selector)
        details["spell"] = validate_set_rule(item, "spell", rule, curve, level)
        if item.get("spell_family_tag"):
            details["spell_family_tag"] = item["spell_family_tag"]
        else:
            details["spell_ability_tag"] = item["spell_ability_tag"]
    return details


def main():
    global ITEMS
    ITEMS = load_catalog()
    REPORT["catalog_schema_version"] = 1
    REPORT["catalog_sha256"] = file_hash(MANIFEST_PATH)
    parent = u.load_class(None, "/Script/test_25d.UniqueEquipmentGameplayEffect")
    require(parent is not None, "Rebuild/restart UE with UniqueEquipmentGameplayEffect before authoring")
    table = u.load_asset(TABLE_PATH)
    require(isinstance(table, u.DataTable), "Existing equipment table is required")
    require(TABLES.get_data_table_row_struct(table).get_path_name() == "/Script/test_25d.BackpackItemDefEntry", "Wrong table schema")
    catalog = u.load_asset(CATALOG_PATH)
    require(catalog and catalog.get_editor_property("equipment_table") == table, "Existing catalog must already reference equipment table")
    referenced_tags = set()
    for item in ITEMS:
        referenced_tags.update(item.get("required_set_tags", []))
        referenced_tags.update("Equipment.Slot." + slot for slot in item["slots"])
        referenced_tags.update(item[field] for field in ("tag", "projectile_ability_tag", "melee_ability_tag", "damage_type",
                                                         "spell_ability_tag", "spell_family_tag") if field in item)
    for tag in sorted(referenced_tags):
        imported = struct(u.GameplayTag, '(TagName="' + tag + '")')
        require(tags_in(imported) == {tag}, "Missing registered gameplay tag: " + tag)
    for item in ITEMS:
        require(isinstance(u.load_asset("/Game/Assets/EquipmentIcon/" + item["icon"]), u.Texture2D), "Missing source icon")
        for field in ("hit_effect", "body_effect"):
            if item.get(field):
                require(isinstance(u.load_asset(item[field]), u.NiagaraSystem), "Missing Niagara system: " + item[field])
    curve = ensure_curve()
    effects = {item["id"]: ensure_effect(item, parent) for item in ITEMS}
    ensure_definitions(table)
    rows = read_rows(table)
    REPORT["items"] = [validate_item(item, rows[item["id"]], effects[item["id"]], curve) for item in ITEMS]
    packages = [TABLE_PATH, CURVE_PATH] + [effect_path(item) for item in ITEMS]
    REPORT["asset_sha256"] = {path: file_hash(asset_file(path)) for path in packages}
    REPORT["idempotent"] = not REPORT["changed_assets"]
    author_report_path = SAVED / "unique_equipment_authored.json"
    if VERIFY_ONLY:
        require(author_report_path.is_file(), "A successful author report is required before independent verification")
        authored = json.loads(author_report_path.read_text(encoding="utf-8"))
        require(authored.get("success"), "Last author run did not succeed")
        require(authored.get("catalog_sha256") == REPORT["catalog_sha256"], "Catalog changed since authoring")
        require(authored["process_id"] != os.getpid(), "Verification must run in a fresh Unreal process")
        require(authored["asset_sha256"] == REPORT["asset_sha256"], "Saved packages changed since authoring; rerun author first")
        require(authored["items"] == REPORT["items"], "Independent reload differs from authored values")
        REPORT["verified_author_run"] = authored["run_id"]
    REPORT["independent_reload_verified"] = VERIFY_ONLY
    REPORT["success"] = True
    REPORT["finished_utc"] = datetime.now(timezone.utc).isoformat()
    result_path = SAVED / ("unique_equipment_verified.json" if VERIFY_ONLY else "unique_equipment_authored.json")
    result_path.write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding="utf-8")
    u.log("UNIQUE_EQUIPMENT_" + ("VERIFY" if VERIFY_ONLY else "AUTHOR") + "_SUCCESS " + str(result_path))


try:
    main()
except Exception:
    REPORT["error"] = traceback.format_exc()
    u.log_error("UNIQUE_EQUIPMENT_FAILED " + REPORT["error"])
    raise
finally:
    (RUN_DIR / "report.json").write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding="utf-8")
