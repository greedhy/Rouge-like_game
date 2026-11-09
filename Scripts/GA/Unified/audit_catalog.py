"""Read-only UE AssetRegistry inventory; standalone --self-test needs no Unreal.

UE commandlet -> discover all project Blueprint descendants/configs -> validate
references/coverage -> merge manual fields into a NEW candidate + diff. Never saves
assets or overwrites the reviewed matrix. See docs/ga-unified-authoring-guide.md.
"""
import copy
import datetime as dt
import json
import os
from pathlib import Path
import re
import sys
import uuid

SCHEMA_VERSION = 1
FAMILIES = {"ActorPrototype", "MixedPrototype", "Extension", "Batch6Special",
            "TimelineWeapon", "Traditional", "Unknown"}
STATES = {"Inventoried", "AdapterRequired", "Adapting", "Verified", "Retired"}
FIELDS = {"assetPath", "generatedClass", "parentClass", "family", "configPaths",
          "targetSupport", "lifecycle", "compatibilityProfile", "migrationStatus",
          "replacementPath", "evidence", "issues"}
MANUAL_FIELDS = ("migrationStatus", "replacementPath", "targetSupport", "lifecycle",
                 "compatibilityProfile")
GA_BASE = "/Script/GameplayAbilities.GameplayAbility"
CONFIG_BASES = {"AbilityPrototype", "AbilityTimelineDefinition", "SwordShieldConfig",
                "StanderWeaponActionConfig", "StoneGolemAttackConfig", "MonsterCombatConfig",
                "OrbitingFireBoltConfig", "ShamanSummonConfig", "AbilityGraphDefinition"}
CONFIG_PROPERTIES = {"prototype", "config", "attack_config", "action_config", "timeline",
                     "graph", "definition", "on_hit", "on_period", "on_expire", "orbit_settings",
                     "hit_child", "period_child", "complete_child", "combat_config",
                     "stander_config", "sword_config", "sword_shield_config", "summon_config",
                     "cost_gameplay_effect_class", "cooldown_gameplay_effect_class", "ability_tags",
                     "activation_owned_tags", "activation_required_tags", "activation_blocked_tags",
                     "net_execution_policy", "instancing_policy", "damage_effect_class"}


def native_source_catalog(root):
    """Source cross-check identifies project classes and explicitly excludes test fixtures.

    Reflection remains the authority for the actual class/ancestry/CDO. The source
    table is a second coverage check so directly granted native GAs cannot disappear
    merely because no Blueprint asset refers to them.
    """
    classes = {}
    for header in sorted((root / "Source").rglob("*.h")):
        source = header.read_text(encoding="utf-8-sig")
        for match in re.finditer(r"\bclass\s+(?:\w+_API\s+)?(U\w+)\s*:\s*public\s+(U\w+)", source):
            name, parent = match.groups()
            relative = header.relative_to(root).as_posix()
            metadata = source[:match.start()].rsplit("UCLASS(", 1)[-1]
            classes[name] = {"parent": parent, "sourceHeader": relative,
                             "declaredAbstract": "Abstract" in metadata,
                             "testOnly": "Tests" in header.relative_to(root).parts}

    def is_ga(name, visited=None):
        if name == "UGameplayAbility":
            return True
        visited = set() if visited is None else visited
        if name in visited or name not in classes:
            return False
        return is_ga(classes[name]["parent"], visited | {name})

    result = {"/Script/test_25d." + name[1:]: info for name, info in classes.items() if is_ga(name)}
    for info in result.values():
        info["sourceReferences"] = []
    for cpp in sorted((root / "Source/test_25d/Private").rglob("*.cpp")):
        if "Tests" in cpp.relative_to(root).parts:
            continue
        for line_no, line in enumerate(cpp.read_text(encoding="utf-8-sig").splitlines(), 1):
            for name in re.findall(r"\b(U\w+)::StaticClass\s*\(", line):
                key = "/Script/test_25d." + name[1:]
                if key in result:
                    result[key]["sourceReferences"].append({"path": cpp.relative_to(root).as_posix(),
                                                            "line": line_no, "text": line.strip()})
    return result


def object_path(value):
    """Normalize AssetRegistry export-text references, never infer a new identity."""
    if hasattr(value, "package_name") and hasattr(value, "asset_name"):
        package, name = str(value.package_name), str(value.asset_name)
        return package + ("." + name if name != "None" else "")
    value = str(value or "")
    if "'" in value:
        value = value.split("'", 1)[1].rsplit("'", 1)[0]
    return "" if value in ("None", "null") else value


def family_for(ancestry):
    names = {path.rsplit(".", 1)[-1] for path in ancestry}
    if any(name.startswith("Batch6") for name in names):
        return "Batch6Special"
    if names & {"ExtensionAbility", "ExtensionNode"}:
        return "Extension"
    if "PureMassAbility" in names:
        return "MixedPrototype"
    if names & {"StanderAbility", "StanderWeaponActionAbility", "StanderWeaponActionConfig",
                "SwordShieldAbility", "SwordShieldConfig", "AbilityTimelineDefinition"}:
        return "TimelineWeapon"
    if names & {"PrototypeAbility", "AbilityPrototype"}:
        return "ActorPrototype"
    if GA_BASE in ancestry or names & CONFIG_BASES or "StoneGolemAttackConfig" in names:
        return "Traditional"
    return "Unknown"


def new_row(path, generated, parent, family, audit):
    return {"assetPath": path, "generatedClass": generated, "parentClass": parent,
            "family": family, "configPaths": [], "targetSupport": ["NeedsReview"],
            "lifecycle": "NeedsReview", "compatibilityProfile": "LegacyNeedsReview",
            "migrationStatus": "AdapterRequired" if family == "Unknown" else "Inventoried",
            "replacementPath": None, "evidence": {"manual": [], "audit": audit},
            "issues": ["Unrecognized ancestry; review required"] if family == "Unknown" else []}


def indexed(rows):
    result = {}
    for row in rows:
        path = row.get("assetPath")
        if not isinstance(path, str) or not path.startswith("/"):
            raise ValueError("Invalid assetPath: " + repr(path))
        if path in result:
            raise ValueError("Duplicate inventory row: " + path)
        result[path] = row
    return result


def validate(rows, expected_paths, valid_paths):
    by_path = indexed(rows)
    missing = set(expected_paths) - set(by_path)
    if missing:
        raise ValueError("Omitted assets (including unknown parents): " + ", ".join(sorted(missing)))
    for path, row in by_path.items():
        if FIELDS - row.keys():
            raise ValueError("Incomplete row: " + path)
        if row["family"] not in FAMILIES or row["migrationStatus"] not in STATES:
            raise ValueError("Invalid family/status: " + path)
        if row["family"] == "Unknown" and row["migrationStatus"] == "Verified":
            raise ValueError("Unknown cannot be Verified: " + path)
        if not isinstance(row["evidence"], dict) or "manual" not in row["evidence"]:
            raise ValueError("Evidence must contain a manual field: " + path)
        if row["migrationStatus"] == "Verified" and not row["evidence"]["manual"]:
            raise ValueError("Verified needs explicit manual verification evidence: " + path)
        for ref in row["configPaths"]:
            if ref not in valid_paths or ref not in by_path:
                raise ValueError("Invalid/uninventoried config reference: " + path + " -> " + ref)
        replacement = row["replacementPath"]
        if replacement and replacement not in valid_paths:
            raise ValueError("Invalid replacement reference: " + path + " -> " + replacement)
    return by_path


def assert_manual_preserved(old_rows, new_rows):
    new = indexed(new_rows)
    for path, old in indexed(old_rows).items():
        if path not in new:
            raise ValueError("Removed existing manual record: " + path)
        for field in MANUAL_FIELDS:
            if old[field] != new[path][field]:
                raise ValueError("Overwritten manual field: " + path + "." + field)
        before = {k: v for k, v in old["evidence"].items() if k != "audit"}
        after = {k: v for k, v in new[path]["evidence"].items() if k != "audit"}
        if before != after:
            raise ValueError("Overwritten manual evidence: " + path)


def merge(previous, observed):
    """Keep reviewer-owned state/evidence verbatim; disappearance requires review."""
    old = indexed(previous)
    fresh = indexed(observed)
    rows, changes = [], []
    for path, observation in fresh.items():
        row = copy.deepcopy(observation)
        if path in old:
            for field in MANUAL_FIELDS:
                row[field] = copy.deepcopy(old[path][field])
            row["evidence"] = copy.deepcopy(old[path]["evidence"])
            row["evidence"]["audit"] = copy.deepcopy(observation["evidence"]["audit"])
            row["issues"] = list(dict.fromkeys(old[path]["issues"] + row["issues"]))
            differences = {key: {"before": old[path].get(key), "observed": observation.get(key)}
                           for key in ("generatedClass", "parentClass", "family", "configPaths")
                           if old[path].get(key) != observation.get(key)}
            old_properties = old[path]["evidence"].get("audit", {}).get("properties")
            new_properties = observation["evidence"]["audit"].get("properties")
            if old_properties != new_properties:
                differences["properties"] = {"before": old_properties, "observed": new_properties}
            if differences:
                changes.append({"assetPath": path, "change": "changed", "fields": differences})
                if row["migrationStatus"] == "Verified":
                    row["issues"].append("Verified evidence needs review after observed asset changes")
        else:
            changes.append({"assetPath": path, "change": "added"})
        rows.append(row)
    for path in old.keys() - fresh.keys():
        row = copy.deepcopy(old[path])
        row["issues"] = list(dict.fromkeys(row["issues"] + ["Previously inventoried asset missing from scan"]))
        rows.append(row)
        changes.append({"assetPath": path, "change": "missing", "requiresReview": True})
    rows.sort(key=lambda row: row["assetPath"])
    assert_manual_preserved(previous, rows)
    return rows, changes


def self_test():
    a = new_row("/Game/GA_A.GA_A", "ClassA", "UnknownParent", "Unknown", {})
    b = new_row("/Game/DA_B.DA_B", "Config", "Base", "ActorPrototype", {})
    paths = {a["assetPath"], b["assetPath"]}
    failures = []

    def rejects(name, callback):
        try:
            callback()
        except ValueError:
            return
        failures.append(name)

    rejects("duplicate", lambda: validate([a, a], paths, paths))
    rejects("unknown omission", lambda: validate([b], paths, paths))
    broken = copy.deepcopy(a)
    broken["configPaths"] = ["/Game/Missing.Missing"]
    rejects("broken config", lambda: validate([broken, b], paths, paths))
    previous = copy.deepcopy(a)
    previous["migrationStatus"] = "Adapting"
    previous["evidence"]["manual"] = [{"report": "manual-report", "pie": "pending"}]
    rejects("overwrite manual", lambda: assert_manual_preserved([previous], [a]))
    rows, _ = merge([previous], [a, b])
    assert_manual_preserved([previous], rows)
    validate(rows, paths, paths)
    forbidden = copy.deepcopy(a)
    forbidden["migrationStatus"] = "Verified"
    rejects("unknown verified", lambda: validate([forbidden, b], paths, paths))
    indirect = ["/Game/BP_Child.BP_Child_C", "/Game/BP_Parent.BP_Parent_C",
                "/Script/test_25d.PureMassAbility", "/Script/test_25d.PrototypeAbility", GA_BASE]
    if family_for(indirect) != "MixedPrototype":
        failures.append("indirect family")
    missing_rows, changes = merge([previous], [b])
    assert_manual_preserved([previous], missing_rows)
    if not any(change["change"] == "missing" for change in changes):
        failures.append("missing evidence")
    native = new_row("/Script/test_25d.NativeDirectAbility", "/Script/test_25d.NativeDirectAbility",
                     GA_BASE, "Traditional", {"kind": "NativeGameplayAbility"})
    rejects("native omission", lambda: validate([a, b], paths | {native["assetPath"]}, paths))
    if failures:
        raise AssertionError(failures)
    print("PASS: 9 inventory invariants (duplicates, unknown omissions, references, manual evidence, "
          "state preservation, unknown verification, indirect family, disappearance, native omission)")


def scan_unreal(u):
    registry = u.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    all_assets = list(registry.get_all_assets(True))
    assets = {str(a.package_name) + "." + str(a.asset_name): a for a in all_assets
              if not str(a.package_name).startswith(("/Engine/", "/Script/", "/Temp/"))}
    valid_paths = set(assets)
    errors, rows, expected, ledger = [], [], set(), []
    source_classes = native_source_catalog(Path(u.Paths.project_dir()).resolve())
    native_ledger = []
    cache = {}

    def top(path):
        package, name = path.rsplit(".", 1)
        return u.TopLevelAssetPath(package, name)

    def ancestors(path):
        if not path:
            return []
        if path not in cache:
            found = registry.get_ancestor_class_names(top(path))
            cache[path] = [path] + [object_path(item) for item in (found or [])]
        return cache[path]

    derived = {object_path(p) for p in registry.get_derived_class_names([top(GA_BASE)], set())}
    blueprint_types = {"/Script/Engine.Blueprint"} | {
        object_path(p) for p in registry.get_derived_class_names([top("/Script/Engine.Blueprint")], set())}
    ga_objects, config_objects, runtime_owners = {}, {}, []

    def config_class(path):
        return bool({p.rsplit(".", 1)[-1] for p in ancestors(path)} & CONFIG_BASES)

    def tag(asset, name):
        return object_path(asset.get_tag_value(name))

    # AssetRegistry descendants, not a name prefix or direct-parent equality, select GAs.
    for path, asset in sorted(assets.items()):
        class_path = object_path(asset.asset_class_path)
        if class_path in blueprint_types:
            generated = tag(asset, "GeneratedClass")
            parent = tag(asset, "ParentClass")
            native = tag(asset, "NativeParentClass")
            lineage = list(dict.fromkeys(ancestors(generated) + ancestors(parent) + ancestors(native)))
            is_ga = generated in derived or GA_BASE in lineage
            suspicious = str(asset.asset_name).startswith(("GA_", "GA-")) or not parent
            ledger.append({"assetPath": path, "generatedClass": generated, "parentClass": parent,
                           "nativeParentClass": native, "isGameplayAbility": is_ga,
                           "unresolvedCandidate": suspicious and not is_ga})
            if any(p.rsplit(".", 1)[-1] in {"StanderPlayer", "SwordShieldPlayer", "TelegraphedEnemyCharacter"}
                   for p in lineage):
                runtime_owners.append((path, generated, lineage))
            if not is_ga and not suspicious:
                continue
            expected.add(path)
            family = family_for(lineage) if is_ga else "Unknown"
            row = new_row(path, generated, parent, family,
                          {"kind": "GameplayAbilityBlueprint", "ancestry": lineage,
                           "selection": "AssetRegistryDerivedClasses" if is_ga else "UnresolvedCandidate"})
            rows.append(row)
            try:
                cls = u.load_class(None, generated) if generated else None
                if not cls:
                    raise ValueError("Generated class failed to load")
                ga_objects[path] = u.get_default_object(cls)
            except Exception as error:
                errors.append({"assetPath": path, "error": str(error)})
                row["issues"].append("Cannot inspect generated class: " + str(error))
        elif config_class(class_path):
            expected.add(path)
            obj = asset.get_asset()
            if obj is None:
                errors.append({"assetPath": path, "error": "Configuration failed to load"})
            else:
                config_objects[path] = obj

    # Native abilities are distinct entry records, including base classes and classes
    # granted directly in C++. Test-only fixture types stay in an exclusion ledger.
    for class_path in sorted({p for p in derived if p.startswith("/Script/")} | set(source_classes)):
        source_info = source_classes.get(class_path)
        if source_info and source_info["testOnly"]:
            native_ledger.append({"classPath": class_path, "included": False,
                                  "reason": "Test-only fixture declared under Source/.../Tests; not game content",
                                  **source_info})
            continue
        if not source_info and not class_path.startswith("/Script/test_25d."):
            native_ledger.append({"classPath": class_path, "included": False,
                                  "reason": "External engine/plugin native class outside project module"})
            continue
        expected.add(class_path)
        lineage = ancestors(class_path)
        row = new_row(class_path, class_path, lineage[1] if len(lineage) > 1 else "",
                      family_for(lineage), {"kind": "NativeGameplayAbility", "ancestry": lineage,
                                           "source": source_info or {"sourceHeader": "Unresolved"}})
        rows.append(row)
        native_ledger.append({"classPath": class_path, "included": True, "source": source_info})
        try:
            if class_path not in derived or GA_BASE not in lineage:
                raise ValueError("Native source GA missing from reflected GameplayAbility descendants")
            cls = u.load_class(None, class_path)
            if not cls:
                raise ValueError("Native GameplayAbility class failed to load")
            valid_paths.add(class_path)
            ga_objects[class_path] = u.get_default_object(cls)
            if not source_info:
                row["issues"].append("Reflected project native GA has no matching source declaration; inspect module")
        except Exception as error:
            errors.append({"assetPath": class_path, "error": str(error)})
            row["issues"].append(str(error))

    def snapshot(obj):
        """Read reflected Python properties and nested structs; never call asset mutators."""
        references, properties = {}, {}

        def serialize(value, location, depth=0):
            if value is None or isinstance(value, (str, bool, int, float)):
                return value
            if isinstance(value, u.Object):
                ref = value.get_path_name()
                references[location] = ref
                if isinstance(value, u.DataAsset) and ref in assets:
                    config_objects.setdefault(ref, value)
                if ref.startswith(obj.get_path_name() + ":"):
                    if depth >= 8:
                        raise ValueError("Instanced config nesting exceeds audit limit: " + location)
                    fields = {"objectPath": ref, "class": value.get_class().get_path_name()}
                    for name in dir(value):
                        if name.startswith("_"):
                            continue
                        try:
                            field = value.get_editor_property(name)
                        except Exception:
                            continue
                        fields[name] = serialize(field, location + "." + name, depth + 1)
                    return fields
                return ref
            if depth >= 8:
                raise ValueError("Configuration nesting exceeds audit limit: " + location)
            if isinstance(value, (list, tuple, u.Array)):
                return [serialize(item, location + "[" + str(i) + "]", depth + 1)
                        for i, item in enumerate(value)]
            if isinstance(value, u.StructBase):
                result = {}
                for name in dir(value):
                    if name.startswith("_"):
                        continue
                    try:
                        field = value.get_editor_property(name)
                    except Exception:
                        continue
                    result[name] = serialize(field, location + "." + name, depth + 1)
                return result if result else {"exportText": value.export_text()}
            return str(value)

        for name in sorted(set(dir(obj)) | CONFIG_PROPERTIES):
            if name.startswith("_"):
                continue
            try:
                value = obj.get_editor_property(name)
            except Exception:
                continue
            properties[name] = serialize(value, name)
        return properties, references

    # These legacy GAs resolve SwordConfig from the Avatar at runtime. Record each
    # concrete owner/property pair; configPaths here means a possible runtime binding,
    # not proof that a particular loadout grants or activates the GA.
    runtime_bindings = []
    for owner_path, generated, lineage in runtime_owners:
        try:
            owner = u.get_default_object(u.load_class(None, generated))
            owner_names = {p.rsplit(".", 1)[-1] for p in lineage}
            property_name = "sword_config"
            if "TelegraphedEnemyCharacter" in owner_names:
                property_name = "monster_config" if "StoneGolemCharacter" in owner_names else "combat_config"
            config = owner.get_editor_property(property_name)
            if config:
                config_path = config.get_path_name()
                config_objects.setdefault(config_path, config)
                binding = {"ownerAsset": owner_path, "property": property_name,
                           "configPath": config_path, "ownerAncestry": lineage}
                if "TelegraphedEnemyCharacter" in owner_names:
                    binding["grantedAbilityClasses"] = [cls.get_path_name() for cls in
                                                        config.get_editor_property("attacks") if cls]
                runtime_bindings.append(binding)
        except Exception as error:
            errors.append({"assetPath": owner_path, "error": "Runtime config read failed: " + str(error)})

    by_path = indexed(rows)
    pending = dict(ga_objects)
    inspected = set()
    while pending or set(config_objects) - inspected:
        pending.update({p: o for p, o in config_objects.items() if p not in inspected})
        path = sorted(pending)[0]
        obj = pending.pop(path)
        if path in inspected:
            continue
        inspected.add(path)
        if path not in by_path:
            cls = obj.get_class().get_path_name()
            lineage = ancestors(cls)
            row = new_row(path, cls, lineage[1] if len(lineage) > 1 else "", family_for(lineage),
                          {"kind": "Configuration", "ancestry": lineage})
            rows.append(row)
            by_path[path] = row
            expected.add(path)
        row = by_path[path]
        try:
            properties, references = snapshot(obj)
            row["evidence"]["audit"]["properties"] = properties
            row["evidence"]["audit"]["propertyReferences"] = references
            row["configPaths"] = sorted({ref for ref in references.values() if ref in config_objects})
            # A null root in a concrete prototype GA is a broken configuration, not an empty graph.
            if row["evidence"]["audit"]["kind"] == "GameplayAbilityBlueprint" and row["family"] in {
                    "ActorPrototype", "MixedPrototype", "Extension", "Batch6Special"}:
                if not properties.get("prototype"):
                    raise ValueError("Prototype GA has no root configuration")
            # Serialized hard/soft package references catch references that loaded as null.
            options = u.AssetRegistryDependencyOptions(include_soft_package_references=True,
                                                       include_hard_package_references=True,
                                                       include_searchable_names=False,
                                                       include_soft_management_references=False,
                                                       include_hard_management_references=False)
            dependencies = sorted(str(p) for p in (registry.get_dependencies(
                str(assets[path].package_name), options) or [])) if path in assets else []
            row["evidence"]["audit"]["packageDependencies"] = dependencies
            for dependency in dependencies:
                if dependency.startswith(("/Script/", "/Engine/")):
                    continue
                if not registry.get_assets_by_package_name(dependency, True):
                    raise ValueError("Unresolved serialized package dependency: " + dependency)
        except Exception as error:
            row["issues"].append(str(error))
            errors.append({"assetPath": path, "error": str(error)})

    # Runtime-owned Stander config cannot be obtained from the GA CDO alone. Keep exact
    # registry referencers as evidence rather than claiming every config belongs to every GA.
    for row in rows:
        if row["evidence"]["audit"]["kind"] == "Configuration":
            options = u.AssetRegistryDependencyOptions(include_soft_package_references=True,
                                                       include_hard_package_references=True)
            row["evidence"]["audit"]["referencerPackages"] = sorted(str(p) for p in
                (registry.get_referencers(str(assets[row["assetPath"]].package_name), options) or []))
        lineage_names = {p.rsplit(".", 1)[-1] for p in row["evidence"]["audit"]["ancestry"]}
        if row["assetPath"] in ga_objects and lineage_names & {"StanderAbility", "SwordShieldAbility"}:
            bindings = [binding for binding in runtime_bindings if binding["property"] == "sword_config" and
                        ("StanderAbility" not in lineage_names or
                         "/Script/test_25d.StanderPlayer" in binding["ownerAncestry"])]
            row["evidence"]["audit"]["runtimeConfigBindings"] = bindings
            row["configPaths"] = sorted(set(row["configPaths"]) | {binding["configPath"] for binding in bindings})
            row["issues"].append("Avatar-selected configuration candidates; actual grant/loadout needs PIE verification")
        if row["assetPath"] in ga_objects and lineage_names & {"TelegraphedEnemyAbility", "TelegraphedSelectAttack"}:
            bindings = []
            for binding in runtime_bindings:
                if binding["property"] == "sword_config":
                    continue
                if "TelegraphedSelectAttack" in lineage_names:
                    if "StoneGolemSelectAttack" not in lineage_names or "/Script/test_25d.StoneGolemCharacter" in binding["ownerAncestry"]:
                        bindings.append(binding)
                elif any(row["generatedClass"] == granted or (
                        row["evidence"]["audit"]["kind"] == "NativeGameplayAbility" and
                        row["generatedClass"] in ancestors(granted)) for granted in binding["grantedAbilityClasses"]):
                    bindings.append(binding)
            row["evidence"]["audit"]["runtimeConfigBindings"] = bindings
            row["configPaths"] = sorted(set(row["configPaths"]) | {binding["configPath"] for binding in bindings})
            row["issues"].append("Avatar combat configuration candidates; actual spawn/grant needs PIE verification")
    unknown = [row["assetPath"] for row in rows if row["family"] == "Unknown"]
    return rows, expected, valid_paths, {"blueprintsScanned": len(ledger), "assetRegistryAssetsScanned": len(assets),
            "blueprintLedger": ledger, "nativeClassLedger": native_ledger,
            "sourceNativeGameplayAbilities": len(source_classes), "unknownAssets": unknown, "errors": errors}


def write_json(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def run_unreal():
    import unreal as u
    root = Path(u.Paths.project_dir()).resolve()
    run_id = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    out = Path(os.environ.get("UNIFIED_GA_AUDIT_OUTPUT", str(root / "Saved/GAAuthoring/UnifiedGA" / run_id)))
    out.mkdir(parents=True, exist_ok=False)
    matrix = Path(os.environ.get("UNIFIED_GA_MATRIX", str(root / "docs/ga-unified-migration-matrix.json")))
    result = {"schemaVersion": SCHEMA_VERSION, "runId": run_id,
              "generatedAtUtc": dt.datetime.now(dt.timezone.utc).isoformat(),
              "source": "UE AssetRegistry + loaded CDO/DataAsset properties (read-only)",
              "matrixSource": str(matrix), "success": False}
    try:
        previous = json.loads(matrix.read_text(encoding="utf-8-sig"))
        if previous.get("schemaVersion") != SCHEMA_VERSION:
            raise ValueError("Unsupported matrix schemaVersion")
        observed, expected, valid_paths, scan = scan_unreal(u)
        result.update(scan)
        write_json(out / "observed.json", {"schemaVersion": SCHEMA_VERSION, "rows": observed})
        validate(observed, expected, valid_paths)
        rows, changes = merge(previous["rows"], observed)
        write_json(out / "diff.json", {"schemaVersion": SCHEMA_VERSION, "changes": changes})
        validate(rows, expected, valid_paths)
        result["counts"] = {"rows": len(rows), "gameplayAbilityBlueprints": sum(
            row["evidence"]["audit"].get("kind") == "GameplayAbilityBlueprint" for row in observed),
            "nativeGameplayAbilities": sum(row["evidence"]["audit"].get("kind") == "NativeGameplayAbility"
                                           for row in observed),
            "excludedTestNativeClasses": sum(not item["included"] and item.get("testOnly", False)
                                             for item in scan["nativeClassLedger"]),
            "configurations": sum(row["evidence"]["audit"].get("kind") == "Configuration" for row in observed),
            "byFamily": {family: sum(row["family"] == family for row in rows) for family in sorted(FAMILIES)}}
        result["success"] = bool(rows) and not scan["errors"] and not any(
            change["change"] == "missing" or (change["change"] == "changed" and
            indexed(rows)[change["assetPath"]]["migrationStatus"] == "Verified") for change in changes)
        candidate = {"schemaVersion": SCHEMA_VERSION, "inventory": {key: value for key, value in result.items()
                     if key != "blueprintLedger"}, "rows": rows}
        write_json(out / "matrix-candidate.json", candidate)
    except Exception as error:
        result["fatalError"] = str(error)
    write_json(out / "audit-report.json", result)
    u.log("Unified GA inventory report: " + str(out / "audit-report.json"))
    if not result["success"]:
        raise RuntimeError("Unified GA inventory failed; inspect " + str(out / "audit-report.json"))


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        self_test()
    else:
        run_unreal()
