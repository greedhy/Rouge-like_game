"""Fresh UE inventory -> route/compile audit -> optional authorized GA migration.

Default is read-only. UNIFIED_GA_MIGRATION_APPLY=1 authorizes only the literal
Blueprint whitelist below. Every write is backed up before reparenting,
compiled and saved. A separate process must verify the saved report with
UNIFIED_GA_MIGRATION_VERIFY_REPORT=/absolute/path/catalog-report.json.
Arcane's native configuration is copied from its actual old CDO and graph pins.
The original Blueprint graphs and protected gameplay defaults remain the baseline.
"""
import collections
import datetime as dt
import hashlib
import json
import math
import os
import re
from pathlib import Path
import shutil
import struct
import sys
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parent))
import audit_catalog as audit
import audit_blueprint_graphs as blueprint_audit

MY_BASE = "/Script/test_25d.MyGameplayAbility"
HIT_REACT_PACKAGE = "/Game/BluePrints/AbilitySystem/Enemy/Ability/GA_HitReact"
ATTRIBUTE_EVENT_PACKAGE = "/Game/BluePrints/AbilitySystem/Player/Ability/Passive_Startup/GA_ListenForEvent"
XP_EVENT_PACKAGE = "/Game/BluePrints/AbilitySystem/Player/Ability/Passive_Startup/GA_Listen_New"
UTILITY_PACKAGES = frozenset({
    "/Game/BluePrints/AbilitySystem/Enemy/Ability/GA_HitReact",
    "/Game/BluePrints/AbilitySystem/Player/Ability/Passive_Startup/GA_ListenForEvent",
    "/Game/BluePrints/AbilitySystem/Player/Ability/Passive_Startup/GA_Listen_New",
})
ARCANE_PACKAGE = "/Game/BluePrints/AbilitySystem/Player/Ability/Arcane/ArcaneChard/GA_ArcaneShards"
ARCANE_PARENT = "/Script/test_25d.MyArcaneShardAbility"
BEAM_PACKAGE = "/Game/BluePrints/AbilitySystem/Player/Ability/Thunder/GA_Electrocute"
SUMMON_PACKAGE = "/Game/BluePrints/AbilitySystem/Enemy/Ability/GA_SummonAbility"
AUTHORIZED_PARENTS = dict.fromkeys(UTILITY_PACKAGES, MY_BASE)
AUTHORIZED_PARENTS[HIT_REACT_PACKAGE] = "/Script/test_25d.MyHitReactAbility"
AUTHORIZED_PARENTS[ATTRIBUTE_EVENT_PACKAGE] = AUTHORIZED_PARENTS[XP_EVENT_PACKAGE] = "/Script/test_25d.MyAttributeEventAbility"
AUTHORIZED_PARENTS[ARCANE_PACKAGE] = ARCANE_PARENT
AUTHORIZED_PARENTS[BEAM_PACKAGE] = "/Script/test_25d.MyBeamSpell"
AUTHORIZED_PARENTS[SUMMON_PACKAGE] = "/Script/test_25d.SummonAbility"
ARCANE_POINT_CLASS = "/Game/BluePrints/AbilitySystem/Player/Ability/Arcane/ArcaneChard/BP_PointCollection.BP_PointCollection_C"
ARCANE_MONTAGE = "/Game/Assets/Characters/Aura/Animations/Abilities/AM_Cast_ArcaneShards.AM_Cast_ArcaneShards"
BEAM_MONTAGE = "/Game/Assets/Characters/Aura/Animations/Abilities/AM_Cast_Electrocute.AM_Cast_Electrocute"
SUMMON_MONTAGE = "/Game/Assets/Enemies/Shaman/Animations/AM_Shaman_Summon.AM_Shaman_Summon"
SUMMON_EFFECT = "/Game/Assets/Effects/Combat/NS_GroundSummon.NS_GroundSummon"
INVENTORY_KINDS = frozenset({"GameplayAbilityBlueprint", "NativeGameplayAbility", "Configuration"})
BASE_ONLY_CLASSES = {"MyGameplayAbility", "MyDamageGameplayAbility"}
# Every nearest native class below was reviewed for overrides. A new subclass cannot inherit a green route by accident.
REVIEWED_NATIVE_CLASSES = set("""Batch6ChainAbility Batch6FieldAbility Batch6PetrifyAbility DemonClawAbility
DemonTailspinAbility ExtensionAbility GhoulLeftAbility GhoulRightAbility GoblinSlingAbility GoblinSpearAbility
GruxChargeAbility GruxComboAbility GruxEnemyAbility GruxRoarAbility MyArcaneNova MyBeamSpell MyDamageGameplayAbility
MyFireBolt MyGameplayAbility MyMeleeAttack MyOrbitingFireBoltAbility MyPassiveAbility MyProjectileSpell PrototypeAbility
PureMassAbility ShamanBurstAbility ShamanCastAbility ShamanSummonAbility ShroomSlamAbility ShroomSporeAbility
StanderAbility StanderComboAbility StanderDashAbility StanderGuardAbility StanderLiftAbility StanderShieldBashAbility
StanderSpinAbility StanderWeaponActionAbility StoneGolemAttackAbility StoneGolemHeavyHammer StoneGolemQuake
StoneGolemSelectAttack StoneGolemShieldBash SummonAbility SwordComboAbility SwordGuardAbility SwordShieldAbility
SwordSpinAbility TelegraphedEnemyAbility TelegraphedSelectAttack UnifiedGraphAbility MyArcaneShardAbility
MyAttributeEventAbility MyHitReactAbility""".split())
SPECIAL_ACTION_ROUTES = {
    "GoblinSlingAbility": ("Action.EnemySling", ["Delivery.EnemySling"], "LegacyMonsterAbility.cpp",
                           "AbilityEnemyActionNodeExecutions.cpp", ["AbilityEnemyActions::FireSling"]),
    "ShamanSummonAbility": ("Action.ShamanMinions", ["Spawn.ShamanMinions"], "ShamanEnemyAbility.cpp",
                            "AbilityEnemyActionNodeExecutions.cpp", ["AbilityEnemyActions::SummonShamanMinions"]),
    "GruxChargeAbility": ("Action.GruxCharge", ["Move.GruxCharge"], "GruxEnemyAbility.cpp",
                          "AbilityEnemySpecialNodeExecutions.cpp", ["AbilityEnemySpecialActions::PlanCharge", "MakeShared<FAbilityEnemyAreaAction>"]),
    "GruxRoarAbility": ("Action.GruxRoar", ["Status.GruxRoarArmor"], "GruxEnemyAbility.cpp",
                        "AbilityEnemySpecialNodeExecutions.cpp", ["AbilityEnemySpecialActions::ApplyRoar"]),
    "ShroomSporeAbility": ("Action.ShroomSpore", ["Field.ShroomSpore"], "ShroomEnemyAbility.cpp",
                           "AbilityEnemySpecialNodeExecutions.cpp", ["MakeShared<FAbilityEnemyAreaAction>"]),
}
REGISTERED_ROUTES = (
    ("MyAttributeEventAbility", "Action.AttributeEvents", "Effect.GameplayEffect", "MyAttributeEventAbility.cpp", "AbilityGameplayEffectNodes.cpp"),
    ("MyHitReactAbility", "Action.HitReact", "Status.OwnedEffect", "MyHitReactAbility.cpp", "AbilityGameplayEffectNodes.cpp"),
    ("MyOrbitingFireBoltAbility", "Graph.OrbitingFireBolts", "Channel.OrbitingFireBolts", "MyOrbitingFireBoltAbility.cpp", "AbilityOrbitNodeExecutions.cpp"),
    ("MyBeamSpell", "Graph.Electrocute", "Channel.Electrocute", "MyBeamSpell.cpp", "AbilityElectrocuteNodeExecutions.cpp"),
    ("MyMeleeAttack", "Graph.Melee", "Melee.Contact", "MyMeleeAttack.cpp", "AbilityMeleeNode.cpp"),
    ("SummonAbility", "Graph.Summon", "Spawn.Summon", "SummonAbility.cpp", "AbilityTraditionalNodeExecutions.cpp"),
    ("MyArcaneNova", "Graph.ArcaneNova", "Shape.ArcaneNova", "MyArcaneNova.cpp", "AbilityNovaNode.cpp"),
    ("MyArcaneShardAbility", "Graph.ArcaneShards", "Shape.ArcaneShards", "MyArcaneShardAbility.cpp", "AbilityArcaneShardNode.cpp"),
    ("MyProjectileSpell", "Action.ActorProjectiles", "Delivery.ActorProjectiles", "MyProjectileSpell.cpp", "AbilityTraditionalNodeExecutions.cpp"),
)
HOST_ACTION_ROUTES = (
    ("StanderWeaponActionAbility", "Action.Weapon", "Weapon.Action", "StanderWeaponActionAbility.cpp",
     ["Combat.CharacterStrike", "Delivery.WeaponProjectile", "Status.GuardWindow"],
     ["ExecuteStrike", "SpawnWeaponProjectile", "OpenGuardWindow"]),
    ("StanderAbility", "Action.CharacterStrike", "Stander.Strike", "StanderAbility.cpp",
     ["Combat.CharacterStrike", "Status.GuardWindow"], ["ExecuteStrike", "OpenGuardWindow"]),
    ("SwordShieldAbility", "Action.SwordShieldStrike", "SwordShield.Strike", "SwordShieldAbility.cpp",
     ["Combat.CharacterStrike", "Status.GuardWindow"], ["ExecuteStrike", "OpenGuardWindow"]),
    ("TelegraphedEnemyAbility", "Action.TelegraphedContact", "Enemy.Contact", "TelegraphedEnemyAbility.cpp",
     ["Combat.TelegraphedContact"], ["GatherTelegraphedTargets", "ApplyTelegraphedTargets"]),
)
SPECIAL_ROUTE_SOURCES = {
    "Graph.Batch6Chain": ("Delivery.ForkedChain", "AbilitySystem/Batch6/Batch6ChainAbility.cpp", "AbilitySystem/Unified/Nodes/AbilityBatch6NodeExecutions.cpp"),
    "Graph.Batch6Petrify": ("Status.PetrifyArea", "AbilitySystem/Batch6/Batch6Petrify.cpp", "AbilitySystem/Unified/Nodes/AbilityBatch6NodeExecutions.cpp"),
    "Graph.LegacyActor": (None, "AbilitySystem/Prototypes/PrototypeAbility.cpp", "AbilitySystem/Unified/Nodes/AbilityLegacyNodeExecution.cpp"),
    "Graph.LegacyMixed": (None, "PureMass/PureMassAbility.cpp", "AbilitySystem/Unified/Nodes/AbilityLegacyNodeExecution.cpp"),
    "Graph.LegacyExtension": (None, "AbilitySystem/PrototypeExtensions/ExtensionAbility.cpp", "AbilitySystem/Unified/Nodes/AbilityLegacyNodeExecution.cpp"),
    "Graph.Unified": (None, "AbilitySystem/Unified/UnifiedGraphAbility.cpp", "AbilitySystem/Unified/AbilityNodeRegistry.cpp"),
}
PROFILE_BY_ROUTE = {"Graph.LegacyActor": "LegacyActor.v1", "Graph.LegacyMixed": "LegacyMixed.v1",
                    "Graph.LegacyExtension": "LegacyExtension.v1"}
PRESERVED_PROPERTIES = (
    "ability_tags", "activation_owned_tags", "activation_required_tags", "activation_blocked_tags",
    "cancel_abilities_with_tag", "block_abilities_with_tag", "ability_triggers", "instancing_policy",
    "net_execution_policy", "net_security_policy", "cost_gameplay_effect_class", "cooldown_gameplay_effect_class",
)
ARCANE_PRESERVED_PROPERTIES = (
    "damage", "damage_type", "damage_effect_class", "debuff_chance", "debuff_damage", "debuff_duration",
    "debuff_frequency", "knockback_chance", "knockback_force_magnitude", "death_impulse_magnitude",
    "is_radial_damage", "radial_damage_inner_radius", "radial_damage_outer_radius",
    "Num Points", "ShardSpawnDeltaTime",
)


def package(path):
    return path.split(".", 1)[0]


def utc():
    return dt.datetime.now(dt.timezone.utc).isoformat()


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=False,
                                    separators=(",", ":")).encode("utf-8")).hexdigest()


def file_digest(path):
    hasher = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def float32(value):
    """Native UPROPERTY float round-trip; old Blueprint pins may be doubles."""
    return struct.unpack("f", struct.pack("f", value))[0]


def classify(row):
    """Most-specific ancestry wins; a shared lifecycle never proves graph composability."""
    details = row["evidence"]["audit"]
    kind = details["kind"]
    names = {item.rsplit(".", 1)[-1] for item in details.get("ancestry", [])}
    nearest_native = next((item.rsplit(".", 1)[-1] for item in details.get("ancestry", [])
                           if item.startswith("/Script/test_25d.")), None)
    if kind != "Configuration" and nearest_native and nearest_native not in REVIEWED_NATIVE_CLASSES:
        raise ValueError("Unclassified native override requires explicit review: " + nearest_native)
    if row.get("family") == "Unknown":
        raise ValueError("Unresolved ancestry: " + row["assetPath"])
    route, composition = None, "NotApplicable"
    strategy_ids, source_files, action_ids, shared_symbols = [], [], [], []
    entry_dispatch = "LifecycleOnly"
    registered = next((item for item in REGISTERED_ROUTES if item[0] in names), None)
    host_action = next((item for item in HOST_ACTION_ROUTES if item[0] in names), None)
    special_action = SPECIAL_ACTION_ROUTES.get(nearest_native)
    if kind == "Configuration":
        route = "Configuration.referenced"
    elif kind not in ("GameplayAbilityBlueprint", "NativeGameplayAbility"):
        raise ValueError("Unknown inventory kind: " + str(kind))
    elif "Batch6ChainAbility" in names:
        route = "Graph.Batch6Chain"
    elif "Batch6PetrifyAbility" in names:
        route = "Graph.Batch6Petrify"
    elif "UnifiedGraphAbility" in names:
        route = "Graph.Unified"
    elif "ExtensionAbility" in names:
        route = "Graph.LegacyExtension"
    elif "PureMassAbility" in names:
        route = "Graph.LegacyMixed"
    elif "PrototypeAbility" in names:
        route = "Graph.LegacyActor"
    elif registered:
        _, route, strategy, entry, registration = registered
        strategy_ids = [strategy]
        source_files = ["Source/test_25d/Private/AbilitySystem/Ability/" + entry,
                        "Source/test_25d/Private/AbilitySystem/Unified/Nodes/" + registration]
        entry_dispatch = "SharedRegisteredAction" if route.startswith("Action.") else "GraphInstance"
        composition = "RegisteredSharedAction" if route.startswith("Action.") else "RegisteredGraphStrategy"
    elif "TelegraphedSelectAttack" in names:
        route, composition = "Session.AttackSelector", "AttackSelectionOnly"
        source_files = ["Source/test_25d/Private/AbilitySystem/Ability/TelegraphedEnemyAbility.cpp"]
    elif "MyPassiveAbility" in names:
        route, composition = "Session.PassiveListener", "PassiveActivationListener"
        source_files = ["Source/test_25d/Private/AbilitySystem/Ability/MyPassiveAbility.cpp"]
    elif special_action:
        route, strategy_ids, entry, registration, shared_symbols = special_action
        source_files = ["Source/test_25d/Private/AbilitySystem/Ability/" + entry,
                        "Source/test_25d/Private/AbilitySystem/Unified/Nodes/" + registration]
        entry_dispatch, composition = "SharedRegisteredAction", "RegisteredSharedAction"
    elif host_action:
        _, route, action, entry, strategy_ids, shared_symbols = host_action
        action_ids = [action]
        source_files = ["Source/test_25d/Private/AbilitySystem/Ability/" + entry,
                        "Source/test_25d/Private/AbilitySystem/Unified/Nodes/AbilityCharacterNodeExecutions.cpp"]
        entry_dispatch, composition = "HostSessionSharedAction", "RegisteredSharedAction"
    elif kind == "NativeGameplayAbility" and row["generatedClass"].rsplit(".", 1)[-1] in BASE_ONLY_CLASSES:
        route, composition = "Session.Native", "LifecycleBaseOnly"
    elif details.get("blueprintInspection", {}).get("noGameplayActionRequired"):
        route, composition = "Session.BlueprintUtility", "NoGameplayActionRequired"
    elif package(row["assetPath"]) in UTILITY_PACKAGES:
        route, composition = "Session.BlueprintUtility", "UtilityEffectListenerOrReaction"
    elif kind == "GameplayAbilityBlueprint" and names & BASE_ONLY_CLASSES:
        route, composition = "Session.BlueprintUtility", "UnsupportedOpaqueBlueprint"
    if route is None:
        raise ValueError("Unclassified entry, explicit review required: " + row["assetPath"])
    if route.startswith("Graph."):
        composition = "RegisteredGraphStrategy"
        entry_dispatch = "GraphInstance"
    if route in SPECIAL_ROUTE_SOURCES:
        strategy, entry, registration = SPECIAL_ROUTE_SOURCES[route]
        strategy_ids = [strategy] if strategy else []
        source_files = ["Source/test_25d/Private/" + entry, "Source/test_25d/Private/" + registration]
        if route in PROFILE_BY_ROUTE:
            source_files.append("Source/test_25d/Private/AbilitySystem/Unified/AbilityLegacyGraphCompiler.cpp")
    return {"assetPath": row["assetPath"], "kind": kind, "route": route,
            "actionComposition": composition, "graphComposable": route.startswith(("Graph.", "Action.")),
            "strategyTypeIds": list(strategy_ids), "hostActionIds": list(action_ids),
            "sharedActionSymbols": list(shared_symbols),
            "entryDispatch": entry_dispatch, "routeSourceFiles": source_files,
            "reviewedNativeClass": nearest_native,
            "strategyEvidenceScope": "ImplementationCapabilities",
            "profile": PROFILE_BY_ROUTE.get(route), "generatedClass": row["generatedClass"],
            "blueprintInspection": details.get("blueprintInspection"),
            "parentClass": row["parentClass"], "configPaths": sorted(row["configPaths"]),
            "propertyFingerprint": digest(details.get("properties", {})),
            "requiresUtilityReparent": package(row["assetPath"]) in UTILITY_PACKAGES and
                                      AUTHORIZED_PARENTS[package(row["assetPath"])] not in details.get("ancestry", []),
            "requiresArcaneReparent": package(row["assetPath"]) == ARCANE_PACKAGE and
                                     ARCANE_PARENT not in details.get("ancestry", [])}


def classify_catalog(rows):
    """Narrow implementation capabilities using actual per-asset config events and reviewed native modes."""
    by_path = audit.indexed(rows)
    result = [classify(row) for row in rows]
    root = Path(__file__).resolve().parents[3]
    native_sources = collections.defaultdict(set)
    declaration = re.compile(r"^(?:[\w:<>&*]+[ \t]+)*U(\w+)::\w+\s*\(", re.M)
    for source in (root / "Source/test_25d/Private").rglob("*.cpp"):
        if "Tests" in source.parts:
            continue
        for native in declaration.findall(source.read_text(encoding="utf-8-sig")):
            if native in REVIEWED_NATIVE_CLASSES:
                native_sources[native].add(source.relative_to(root).as_posix())
    kinds = {"MELEE": "Combat.CharacterStrike", "PROJECTILE": "Delivery.WeaponProjectile",
             "GUARD": "Status.GuardWindow", "MOVE": "Movement.Character"}
    for routed in result:
        native = routed["reviewedNativeClass"]
        entry = by_path[routed["assetPath"]]
        if native == "StanderWeaponActionAbility":
            events = []
            config_evidence = []
            for path in routed["configPaths"]:
                config = by_path.get(path)
                if not config or "StanderWeaponActionConfig" not in config["generatedClass"]:
                    continue
                props = config["evidence"]["audit"]["properties"]
                if props.get("timeline"):
                    timeline = by_path.get(props["timeline"])
                    if not timeline:
                        raise ValueError("Weapon timeline was omitted from inventory: " + props["timeline"])
                    timeline_props = timeline["evidence"]["audit"]["properties"]
                    if timeline_props.get("events"):
                        raise ValueError("New timeline event binding requires explicit review: " + props["timeline"])
                    config_evidence.append({"assetPath": props["timeline"], "propertyFingerprint": digest(timeline_props)})
                    routed["frontendRecipe"] = "TimelinePosition -> AdvanceAt uses the same ActionConfig.Events; timeline has no additional gameplay events"
                events.extend(props.get("events", []))
                config_evidence.append({"assetPath": path, "propertyFingerprint": digest(props)})
            if routed["kind"] == "GameplayAbilityBlueprint" and not config_evidence:
                raise ValueError("Weapon Blueprint lacks its actual event configuration: " + routed["assetPath"])
            if config_evidence:
                used = set()
                for event in events:
                    match = re.search(r"StanderWeaponActionKind\.([A-Z_]+)", event["kind"])
                    if not match or match.group(1) not in set(kinds) | {"PRESENTATION"}:
                        raise ValueError("Unreviewed weapon action kind: " + str(event["kind"]))
                    if match.group(1) in kinds:
                        used.add(kinds[match.group(1)])
                routed["strategyTypeIds"] = sorted(used)
                routed["strategyEvidenceScope"] = "ConfiguredGameplayActions"
                routed["actionConfigurationEvidence"] = config_evidence
                routed["sharedActionSymbols"] = [symbol for strategy, symbol in (
                    ("Combat.CharacterStrike", "ExecuteStrike"), ("Delivery.WeaponProjectile", "SpawnWeaponProjectile"),
                    ("Status.GuardWindow", "OpenGuardWindow")) if strategy in used]
            else:
                routed["strategyTypeIds"].append("Movement.Character")
            if "Movement.Character" in routed["strategyTypeIds"]:
                routed["sharedActionSymbols"].append("AbilityMovementActions::WeaponArc")
                routed["routeSourceFiles"].append("Source/test_25d/Private/AbilitySystem/Unified/Nodes/AbilityMovementNodeExecutions.cpp")
        elif native in {"StanderDashAbility", "StanderAbility"}:
            routed["strategyTypeIds"].append("Movement.Character")
            routed["sharedActionSymbols"].append("AbilityMovementActions::GroundDash")
            routed["routeSourceFiles"].append("Source/test_25d/Private/AbilitySystem/Unified/Nodes/AbilityMovementNodeExecutions.cpp")
        elif native in {"StanderComboAbility", "StanderLiftAbility", "StanderShieldBashAbility", "StanderSpinAbility",
                        "SwordComboAbility", "SwordSpinAbility"}:
            routed["strategyTypeIds"] = ["Combat.CharacterStrike"]
            routed["sharedActionSymbols"] = ["ExecuteStrike"]
        if native == "StanderDashAbility":
            routed["strategyTypeIds"] = ["Combat.CharacterStrike", "Movement.Character"]
            routed["sharedActionSymbols"] = ["ExecuteStrike", "AbilityMovementActions::GroundDash"]
        if native in {"GruxChargeAbility", "GruxComboAbility"}:
            routed["strategyTypeIds"] = sorted(set(routed["strategyTypeIds"]) | {"Combat.TelegraphedContact"})
            routed["routeSourceFiles"].extend([
                "Source/test_25d/Private/AbilitySystem/Ability/TelegraphedEnemyAbility.cpp",
                "Source/test_25d/Private/AbilitySystem/Unified/Nodes/AbilityCharacterNodeExecutions.cpp"])
            routed["sharedActionSymbols"].extend(["GatherTelegraphedTargets", "ApplyTelegraphedTargets"])
            routed["frontendRecipe"] = "Original staged warning/montage timing; followup contact uses shared action"
        if native == "GruxEnemyAbility":
            routed["strategyTypeIds"].extend(["Move.GruxCharge", "Status.GruxRoarArmor"])
            routed["routeSourceFiles"].extend([
                "Source/test_25d/Private/AbilitySystem/Ability/GruxEnemyAbility.cpp",
                "Source/test_25d/Private/AbilitySystem/Unified/Nodes/AbilityEnemySpecialNodeExecutions.cpp"])
            routed["sharedActionSymbols"].extend(["AbilityEnemySpecialActions::PlanCharge", "AbilityEnemySpecialActions::ApplyRoar"])
        if native in {"StanderGuardAbility", "SwordGuardAbility"}:
            routed["frontendRecipe"] = "Original guard-hit filtering, deferred montage switch and timed counter contact"
        routed["routeSourceFiles"].extend(native_sources.get(native, []))
        routed["strategyTypeIds"] = sorted(set(routed["strategyTypeIds"]))
        routed["routeSourceFiles"] = sorted(set(routed["routeSourceFiles"]))
    return result


def arcane_settings(description, old_properties):
    """Derive native defaults from actual pins/CDO; missing or changed bindings fail closed."""
    bindings = {"Class": set(), "MontageToPlay": set()}
    for graph in description["graphs"]:
        for node in graph["nodes"]:
            for pin in node.get("pins", []):
                if pin["name"] in bindings and pin.get("defaultObject"):
                    bindings[pin["name"]].add(pin["defaultObject"])
    if bindings["Class"] != {ARCANE_POINT_CLASS} or bindings["MontageToPlay"] != {ARCANE_MONTAGE}:
        raise ValueError("Arcane graph's point collection/montage binding differs from the reviewed original")
    count = old_properties["Num Points"]
    interval = old_properties["ShardSpawnDeltaTime"]
    if isinstance(count, bool) or not isinstance(count, int) or not 1 <= count <= 11:
        raise ValueError("Arcane original Num Points must be an integer in 1..11")
    if isinstance(interval, bool) or not isinstance(interval, (int, float)) or not math.isfinite(interval) or interval < .2:
        raise ValueError("Arcane original interval must support its original -0.2 initial delay")
    return {"shard_point_collection_class": next(iter(bindings["Class"])),
            "shard_montage": next(iter(bindings["MontageToPlay"])),
            "shard_interval": float32(interval), "max_shard_points": count}


def native_settings(asset_package, description, properties):
    if asset_package in UTILITY_PACKAGES:
        return utility_settings(asset_package, description, properties)
    if asset_package == ARCANE_PACKAGE:
        return arcane_settings(description, properties)
    if asset_package == BEAM_PACKAGE:
        interval, minimum = properties["DamageDeltaTime"], properties["MiniSpellTIme"]
        if properties["Montage_Electrocute"] != BEAM_MONTAGE:
            raise ValueError("Electrocute original montage differs from the reviewed binding")
        if not all(isinstance(value, (float, int)) and not isinstance(value, bool) and math.isfinite(value) and value > 0
                   for value in (interval, minimum)):
            raise ValueError("Electrocute original damage/minimum intervals must be finite and positive")
        return {"electrocute_montage": properties["Montage_Electrocute"],
                "electrocute_damage_interval": float32(interval), "minimum_electrocute_time": float32(minimum)}
    if asset_package != SUMMON_PACKAGE:
        return {}
    pins = [pin for graph in description["graphs"] for node in graph["nodes"] for pin in node.get("pins", [])]

    def original(name, field):
        values = {pin[field] for pin in pins if pin["name"] == name and pin.get(field)}
        if len(values) != 1:
            raise ValueError("Summon original pin must have one unambiguous value: " + name)
        return next(iter(values))

    montage, effect = original("MontageToPlay", "defaultObject"), original("SystemTemplate", "defaultObject")
    event = original("EventTag", "defaultValue")
    interval = float(original("Duration", "defaultValue"))
    # Only the reviewed Add_VectorVector operation raises spawn points; other constants cannot supply this value.
    heights = [pin["defaultValue"] for graph in description["graphs"] for node in graph["nodes"]
               if 'MemberName="Add_VectorVector"' in node.get("references", {}).get("FunctionReference", "")
               for pin in node.get("pins", []) if pin["name"] == "B" and pin.get("defaultValue")]
    if len(heights) != 1:
        raise ValueError("Summon original spawn offset is missing or ambiguous")
    offset = [float(value) for value in heights[0].split(",")]
    if montage != SUMMON_MONTAGE or effect != SUMMON_EFFECT or event != '(TagName="Montage.Attack.2")' or interval != .2 or offset != [0., 0., 60.]:
        raise ValueError("Summon original presentation or timing differs from the reviewed graph")
    return {"summon_montage": montage, "summon_ground_effect": effect, "summon_event_tag": event,
            "summon_spawn_interval": float32(interval), "summon_spawn_height": float32(offset[2])}


def activation_reachable_nodes(description):
    """Follow actual exec links from K2_ActivateAbility; disconnected tutorial alternatives remain inactive."""
    nodes = {node["guid"]: node for graph in description["graphs"] for node in graph["nodes"]}
    pending = [guid for guid, node in nodes.items()
               if 'MemberName="K2_ActivateAbility"' in node.get("references", {}).get("EventReference", "")]
    reached = set()
    while pending:
        guid = pending.pop()
        if guid in reached:
            continue
        reached.add(guid)
        for pin in nodes[guid].get("pins", []):
            if pin["direction"] == "output" and pin["type"]["category"] == "exec":
                pending.extend(link["nodeGuid"] for link in pin.get("links", []) if link["nodeGuid"] in nodes)
    return [nodes[guid] for guid in sorted(reached)]


def utility_settings(asset_package, description, properties):
    reachable = activation_reachable_nodes(description)
    if asset_package == HIT_REACT_PACKAGE:
        apply = [node for node in reachable if 'MemberName="BP_ApplyGameplayEffectToOwner"' in
                 node.get("references", {}).get("FunctionReference", "")]
        if len(apply) != 1:
            raise ValueError("HitReact must have exactly one reachable original effect application")
        pins = {pin["name"]: pin for pin in apply[0]["pins"]}
        effect = pins["GameplayEffectClass"]["defaultObject"]
        if effect != "/Game/BluePrints/AbilitySystem/Enemy/Effect/GE_HitReact.GE_HitReact_C" or pins["Stacks"]["defaultValue"] != "1":
            raise ValueError("HitReact original effect/stack binding differs from reviewed graph")
        return {"hit_react_effect_class": effect, "hit_react_effect_level": float32(float(pins["GameplayEffectLevel"]["defaultValue"]))}
    wait = [node for node in reachable if node.get("references", {}).get("ProxyFactoryFunctionName") == "WaitGameplayEvent"]
    if len(wait) != 1:
        raise ValueError("Attribute listener requires exactly one reachable original WaitGameplayEvent")
    pins = {pin["name"]: pin for pin in wait[0]["pins"]}
    if pins["OnlyTriggerOnce"]["defaultValue"] != "false":
        raise ValueError("Original attribute listener is no longer repeated")
    listed = asset_package == ATTRIBUTE_EVENT_PACKAGE
    effect = properties["EventBasedEffectClass" if listed else "NewVar"]
    if not isinstance(effect, str) or not effect.startswith("/Game/") or not effect.endswith("_C"):
        raise ValueError("Original attribute event GE class cannot be established from actual CDO")
    graphs = [graph for graph in description["graphs"] if graph["name"] == ("NewFunction" if listed else "EventGraph")]
    levels = [pin["defaultValue"] for graph in graphs for node in graph["nodes"]
              if 'MemberName="MakeOutgoingSpec"' in node.get("references", {}).get("FunctionReference", "")
              for pin in node["pins"] if pin["name"] == "Level"]
    if len(levels) != 1:
        raise ValueError("Original listener effect level is missing or ambiguous")
    return {"event_effect_class": effect, "listen_tag": pins["EventTag"]["defaultValue"],
            "match_event_exactly": pins["OnlyMatchExact"]["defaultValue"] == "true",
            "event_effect_level": float32(float(levels[0])), "initialize_listed_tags": listed,
            "initialized_event_tags": properties["EventTags"] if listed else []}


def blueprint_semantics(description):
    """Inspect actual nodes; only a closed set of print/delay/end lifecycle code is action-free."""
    fingerprint = blueprint_audit.graph_digest(description)
    nodes = [node for graph in description["graphs"] for node in graph["nodes"]]
    functions, events, unsupported_nodes = set(), set(), []
    utility_node_classes = {"K2Node_Event", "K2Node_CallFunction", "K2Node_Knot", "EdGraphNode_Comment"}
    for node in nodes:
        node_class = node["class"].rsplit(".", 1)[-1]
        if node_class not in utility_node_classes:
            unsupported_nodes.append(node_class)
        for key, value in node.get("references", {}).items():
            if key == "FunctionReference":
                functions.update(re.findall(r'MemberName="([^"]+)"', value))
            elif key == "EventReference":
                events.update(re.findall(r'MemberName="([^"]+)"', value))
            elif key == "ProxyFactoryFunctionName" and value:
                functions.add(value)
    allowed_functions = {"PrintString", "Delay", "K2_EndAbility"}
    allowed_events = {"K2_ActivateAbility", "K2_OnEndAbility"}
    no_action = bool(nodes) and not unsupported_nodes and functions <= allowed_functions and events <= allowed_events
    return {"graphFingerprint": fingerprint, "nodeCount": len(nodes), "functions": sorted(functions),
            "events": sorted(events), "noGameplayActionRequired": no_action,
            "basis": "Actual reflected Blueprint graphs and declared variables; no asset-name inference"}


def reviewed_inventory_counts(rows, declared_counts=None):
    """The reviewed matrix is the baseline; historical inventory totals are not a permanent ceiling."""
    if not rows:
        raise ValueError("Reviewed inventory must not be empty")
    audit.indexed(rows)
    counts = collections.Counter(row["evidence"]["audit"]["kind"] for row in rows)
    if set(counts) - INVENTORY_KINDS:
        raise ValueError("Unknown inventory kind: " + ", ".join(sorted(set(counts) - INVENTORY_KINDS)))
    if declared_counts is not None:
        if not isinstance(declared_counts, dict):
            raise ValueError("Unknown reviewed matrix counts format")
        legacy_names = {"gameplayAbilityBlueprints": "GameplayAbilityBlueprint",
                        "nativeGameplayAbilities": "NativeGameplayAbility", "configurations": "Configuration"}
        legacy_required = set(legacy_names) | {"rows"}
        legacy_allowed = legacy_required | {"excludedTestNativeClasses", "byFamily"}
        if set(declared_counts) == INVENTORY_KINDS:
            normalized = dict(declared_counts)
        elif legacy_required.issubset(declared_counts) and set(declared_counts).issubset(legacy_allowed):
            normalized = {kind: declared_counts[name] for name, kind in legacy_names.items()}
            if type(declared_counts["rows"]) is not int or declared_counts["rows"] != len(rows):
                raise ValueError("Reviewed matrix declared rows do not match its actual rows")
            if "excludedTestNativeClasses" in declared_counts:
                excluded = declared_counts["excludedTestNativeClasses"]
                # Excluded fixtures are not inventory rows; retain this auxiliary
                # historical count without pretending it can be derived from rows.
                if type(excluded) is not int or excluded < 0:
                    raise ValueError("Invalid historical excluded test-class count")
            if "byFamily" in declared_counts:
                families = dict(collections.Counter(row["family"] for row in rows))
                declared_families = declared_counts["byFamily"]
                if (not isinstance(declared_families, dict) or
                        any(type(value) is not int or value < 0 for value in declared_families.values()) or
                        set(declared_families) - audit.FAMILIES or set(families) - audit.FAMILIES or
                        any(declared_families.get(family, 0) != families.get(family, 0) for family in audit.FAMILIES)):
                    raise ValueError("Reviewed matrix byFamily counts do not match its actual rows")
        else:
            raise ValueError("Unknown reviewed matrix counts format")
        if (any(type(value) is not int or value < 0 for value in normalized.values()) or
                normalized != dict(counts)):
            raise ValueError("Reviewed matrix counts do not match its actual rows")
    return dict(counts)


def assert_coverage(baseline, observed, classified, expected, declared_counts=None):
    """No baseline, newly discovered, native or unknown row may silently disappear."""
    before, after, routed = audit.indexed(baseline), audit.indexed(observed), audit.indexed(classified)
    missing = set(before) - set(after)
    if missing:
        raise ValueError("Baseline rows disappeared: " + ", ".join(sorted(missing)))
    if set(after) != set(routed) or set(after) != set(expected):
        raise ValueError("Fresh inventory and classified coverage differ")
    counts = reviewed_inventory_counts(baseline, declared_counts)
    reviewed_inventory_counts(observed)
    for path, old in before.items():
        if old["evidence"]["audit"]["kind"] != after[path]["evidence"]["audit"]["kind"]:
            raise ValueError("Inventory kind changed: " + path)
    for path, row in after.items():
        if routed[path]["kind"] != row["evidence"]["audit"]["kind"]:
            raise ValueError("Classified route kind differs from inventory: " + path)
    return counts


def content_file(root, asset_path):
    """Resolve only project packages; never construct writes from unchecked export text."""
    if ".." in asset_path.split("/") or "\\" in asset_path:
        raise ValueError("Invalid package path: " + asset_path)
    asset_package = package(asset_path)
    if not asset_package.startswith("/Game/") or ".." in asset_package.split("/"):
        raise ValueError("Not a project Content package: " + asset_path)
    content = (root / "Content").resolve()
    target = (content / (asset_package[len("/Game/"):] + ".uasset")).resolve()
    if not target.is_relative_to(content):
        raise ValueError("Package escaped Content: " + asset_path)
    return target


def compilation_requests(rows, classified):
    """Compile actual backend bindings and explicit orphan structural checks separately."""
    by_path = audit.indexed(rows)
    requests = {}

    def add(path, profile, owner):
        row = by_path.get(path)
        if not row or row["evidence"]["audit"]["kind"] != "Configuration":
            return
        names = {item.rsplit(".", 1)[-1] for item in row["evidence"]["audit"].get("ancestry", [])}
        if "AbilityPrototype" not in names:
            return
        key = (path, profile)
        if key in requests:
            requests[key].add(owner)
            return
        requests[key] = {owner}
        for child in row["configPaths"]:
            add(child, profile, owner)

    for route in classified:
        profile = route["profile"]
        if profile:
            for ref in route["configPaths"]:
                add(ref, profile, route["assetPath"])
    covered = {path for path, _ in requests}
    for row in rows:
        if row["evidence"]["audit"]["kind"] != "Configuration" or row["assetPath"] in covered:
            continue
        names = {item.rsplit(".", 1)[-1] for item in row["evidence"]["audit"].get("ancestry", [])}
        if "AbilityPrototype" not in names:
            continue
        profiles = ["LegacyExtension.v1"] if "ExtensionNode" in names else (
            ["LegacyMixed.v1"] if "Batch6FieldPrototype" in names else ["LegacyActor.v1", "LegacyMixed.v1"])
        for profile in profiles:
            add(row["assetPath"], profile, "StructuralOnly.NoLegacyEntryBinding")
    return [{"assetPath": path, "profile": profile, "requestedBy": sorted(owners)}
            for (path, profile), owners in sorted(requests.items())]


def normalize_compile_result(result):
    # UObject return + out Errors is a Python tuple, unlike bool+single-out validators.
    if not isinstance(result, tuple) or len(result) != 2:
        raise ValueError("Unexpected CompileForAuthoring Python binding: " + repr(type(result)))
    graph, errors = result
    errors = [str(item) for item in errors]
    if graph is None or errors:
        raise ValueError("Native compile failed: " + "; ".join(errors))
    return graph


def native_bindings(u):
    names = ("MyGameplayAbility", "AbilityLegacyGraphCompiler", "AbilityGraphDefinition", "AbilityGraphInstance",
             "AbilityLegacyNodeExecution", "AbilityDelayNodeExecution", "AbilityPetrifyNodeExecution",
             "AbilityQueryNodeExecution", "AbilityDamageNodeExecution", "AbilityForkedChainNodeExecution",
             "AbilityAreaPetrifyNodeExecution", "AbilityBlueprintAuditLibrary", "MyArcaneShardAbility",
             "AbilityArcaneShardNodeConfig", "AbilityArcaneShardNodeExecution", "MyBeamSpell", "SummonAbility",
             "AbilityOrbitNodeExecution", "AbilityCharacterStrikeNodeExecution", "AbilityWeaponProjectileNodeExecution",
             "AbilityGuardWindowNodeExecution", "AbilityTelegraphedContactNodeExecution", "AbilityNovaNodeExecution",
             "MyAttributeEventAbility", "MyHitReactAbility", "AbilityGameplayEffectNodeExecution", "AbilityOwnedEffectNodeExecution")
    names += ("AbilityEnemySlingNodeExecution", "AbilityShamanSummonNodeExecution",
              "AbilityEnemySpecialNodeExecution", "AbilityCharacterMoveNodeExecution")
    loaded = {}
    for name in names:
        cls = u.load_class(None, "/Script/test_25d." + name)
        if cls is None:
            raise ValueError("Required native class is missing; rebuild before asset audit: " + name)
        loaded[name] = cls.get_path_name()
    for owner, method in (("AbilityLegacyGraphCompiler", "compile_for_authoring"),
                          ("AbilityBlueprintAuditLibrary", "describe_blueprint")):
        if not callable(getattr(getattr(u, owner, None), method, None)):
            raise ValueError("Required reflected function is missing: " + owner + "." + method)
    return loaded


def route_source_evidence(root, row):
    """Confirm described dispatch against current sources; this is structural, never runtime parity evidence."""
    files = {name: (root / name).read_text(encoding="utf-8-sig") for name in row["routeSourceFiles"]}
    if row["strategyTypeIds"]:
        registration = "\n".join(value for name, value in files.items() if "/Nodes/" in name)
        if any('TEXT("' + name + '")' not in registration for name in row["strategyTypeIds"]):
            raise ValueError("Reported strategy has no matching registration source: " + row["assetPath"])
        entries = "\n".join(value for name, value in files.items() if "/Nodes/" not in name)
        if row["entryDispatch"] == "GraphInstance" and any('TEXT("' + name + '")' not in entries for name in row["strategyTypeIds"]):
            raise ValueError("Reported graph entry does not select its strategy: " + row["assetPath"])
        if row["entryDispatch"] == "SharedRegisteredAction":
            symbol = None
            if row["route"] in ("Action.AttributeEvents", "Action.HitReact"):
                symbol = "AbilityGameplayEffectActions::Apply"
            elif row["route"] == "Action.ActorProjectiles":
                symbol = "AbilityTraditionalActions::SpawnProjectiles"
            if symbol and (symbol not in entries or symbol not in registration):
                raise ValueError("Reported shared action entry is missing: " + symbol)
        for symbol in row["sharedActionSymbols"]:
            name = symbol if "::" in symbol or "<" in symbol else "AbilityCharacterActions::" + symbol
            if name not in entries or name not in registration:
                raise ValueError("Reported shared action does not exist in both entry and strategy: " + name)
    for action in row["hostActionIds"]:
        if not any('DispatchExecutionAction(TEXT("' + action + '")' in value for value in files.values()):
            raise ValueError("Reported host action has no matching source dispatch: " + action)
    return {name: file_digest(root / name) for name in files}


def assert_descendants(u, rows, allow_pending):
    base = u.load_class(None, MY_BASE)
    evidence = []
    for row in rows:
        kind = row["evidence"]["audit"]["kind"]
        if kind == "Configuration":
            continue
        cls = u.load_class(None, row["generatedClass"])
        if cls is None:
            raise ValueError("Generated/native class cannot load: " + row["assetPath"])
        inherited = bool(u.MathLibrary.class_is_child_of(cls, base))
        pending = allow_pending and package(row["assetPath"]) in UTILITY_PACKAGES
        if not inherited and not pending:
            raise ValueError("Production GA bypasses the unified base: " + row["assetPath"])
        evidence.append({"assetPath": row["assetPath"], "baseVerified": inherited,
                         "pendingAuthorizedReparent": not inherited and pending})
    return evidence


def stable_property(value):
    if value is None or isinstance(value, (str, int, float, bool)):
        return value
    if hasattr(value, "export_text"):
        return value.export_text()
    if hasattr(value, "get_path_name"):
        return value.get_path_name()
    try:
        return [stable_property(item) for item in value]
    except TypeError:
        return str(value)


def utility_snapshot(u, blueprint):
    cdo = u.get_default_object(blueprint.generated_class())
    properties = {}
    names = PRESERVED_PROPERTIES
    if package(blueprint.get_path_name()) == ARCANE_PACKAGE:
        names += ARCANE_PRESERVED_PROPERTIES
    elif package(blueprint.get_path_name()) == BEAM_PACKAGE:
        names += ARCANE_PRESERVED_PROPERTIES[:-2] + ("max_num_shock_targets", "Montage_Electrocute", "DamageDeltaTime", "MiniSpellTIme")
    elif package(blueprint.get_path_name()) == SUMMON_PACKAGE:
        names += ("num_minions", "minion_classes", "min_spawn_distance", "max_spawn_distance", "spawn_spread")
    elif package(blueprint.get_path_name()) == ATTRIBUTE_EVENT_PACKAGE:
        names += ("EventBasedEffectClass", "EventTags")
    elif package(blueprint.get_path_name()) == XP_EVENT_PACKAGE:
        names += ("NewVar",)
    for name in names:
        try:
            properties[name] = stable_property(cdo.get_editor_property(name))
        except Exception as error:
            raise ValueError("Cannot capture protected GA property " + name + ": " + str(error)) from error
    description = json.loads(u.AbilityBlueprintAuditLibrary.describe_blueprint(blueprint))
    if description.get("supported") is False or "error" in description:
        raise ValueError("Blueprint graph inspection unavailable: " + repr(description))
    # Parent/generated class identities intentionally change; event graphs must not.
    if "graphs" not in description:
        raise ValueError("Blueprint audit did not return its graph contents")
    return {"properties": properties, "graphsFingerprint": blueprint_audit.graph_digest(description)}


def migrated_snapshot(u, blueprint, expected):
    cdo = u.get_default_object(blueprint.generated_class())
    return {name: stable_property(cdo.get_editor_property(name)) for name in expected}


def protected_policy_value(u, name, snapshot):
    """Reparenting inherits new native defaults when the old BP had no override; preserve the actual old enum."""
    enum_types = {"instancing_policy": "GameplayAbilityInstancingPolicy",
                  "net_execution_policy": "GameplayAbilityNetExecutionPolicy"}
    enum_type = enum_types[name]
    match = re.fullmatch(r"<" + enum_type + r"\.([A-Z_]+): \d+>", snapshot)
    if not match:
        raise ValueError("Unexpected protected UE policy enum: " + repr(snapshot))
    return getattr(getattr(u, enum_type), match.group(1))


def saved_write_lineage(report_paths):
    """Combine only actual saved/compiled rows from the two authorized migration attempts."""
    records = {}
    for report_path in report_paths:
        path = Path(report_path).resolve()
        prior = json.loads(path.read_text(encoding="utf-8-sig"))
        if prior.get("dryRun") or prior.get("processId") == os.getpid():
            raise ValueError("Write lineage must come from a different APPLY process")
        for change in prior.get("assetWrites", []):
            if not change.get("saved"):
                continue
            asset_path = change["assetPath"]
            if not change.get("compiled") or package(asset_path) not in AUTHORIZED_PARENTS:
                raise ValueError("Saved lineage is uncompiled or outside the six-asset whitelist")
            if change.get("newParentClass") != AUTHORIZED_PARENTS[package(asset_path)]:
                raise ValueError("Saved lineage has an unexpected native parent")
            if not change.get("files") or not all(item.get("sha256Before") and item.get("sha256After")
                                                  for item in change["files"]):
                raise ValueError("Saved lineage lacks before/after package fingerprints")
            if not change.get("before", {}).get("graphsFingerprint"):
                raise ValueError("Saved lineage lacks original semantic graph evidence")
            records[asset_path] = {"change": change, "report": str(path), "reportSha256": file_digest(path),
                                   "processId": prior["processId"], "matrixSha256": prior.get("matrixSha256")}
    if {package(path) for path in records} != set(AUTHORIZED_PARENTS) or len(records) != len(AUTHORIZED_PARENTS):
        raise ValueError("Readback lineage must contain all six actually saved authorized assets")
    return records


def assert_saved_change(u, blueprint, change):
    parent_path = AUTHORIZED_PARENTS[package(change["assetPath"])]
    if change["newParentClass"] != parent_path:
        raise ValueError("Prior evidence parent differs from the authorized target")
    parent = u.load_class(None, parent_path)
    if not u.MathLibrary.class_is_child_of(blueprint.generated_class(), parent):
        raise ValueError("Saved GA is not derived from its authorized parent")
    actual = utility_snapshot(u, blueprint)
    change["latestSnapshot"] = actual
    if actual != change["before"]:
        change["protectedPropertyDifferences"] = {
            name: {"before": value, "after": actual["properties"].get(name)}
            for name, value in change["before"]["properties"].items() if actual["properties"].get(name) != value}
        raise ValueError("Saved GA changed original graph, variables or protected defaults: " + change["assetPath"])
    expected = change.get("migratedProperties", {})
    if migrated_snapshot(u, blueprint, expected) != expected:
        raise ValueError("Saved GA native configuration differs from its original bindings/defaults")


def reparent_utilities(u, root, out, rows, changes):
    candidates = [row for row in rows if row["requiresUtilityReparent"] or row["requiresArcaneReparent"] or
                  row.get("requiresConfigurationMigration")]
    dirty = {item.get_path_name() for item in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    # Back up every candidate before changing the first one; partial failures remain recoverable.
    for row in candidates:
        asset_path = row["assetPath"]
        if package(asset_path) not in AUTHORIZED_PARENTS:
            raise ValueError("Write request is outside literal whitelist: " + asset_path)
        if package(asset_path) in dirty:
            raise ValueError("Refusing to overwrite an already dirty GA package: " + asset_path)
        source = content_file(root, asset_path)
        if not source.is_file():
            raise ValueError("GA package file missing: " + str(source))
        bp = u.load_asset(asset_path)
        if not isinstance(bp, u.Blueprint):
            raise ValueError("Whitelist target is not a Blueprint: " + asset_path)
        before = utility_snapshot(u, bp)
        description = json.loads(u.AbilityBlueprintAuditLibrary.describe_blueprint(bp))
        audit_path = out / "blueprint-baselines" / (package(asset_path).rsplit("/", 1)[-1] + ".json")
        audit_path.parent.mkdir(parents=True, exist_ok=True)
        audit.write_json(audit_path, description)
        migrated = {}
        if package(asset_path) in AUTHORIZED_PARENTS:
            migrated = native_settings(package(asset_path), description, before["properties"])
        files = []
        for suffix in (".uasset", ".uexp", ".ubulk"):
            item = source.with_suffix(suffix)
            if item.exists():
                destination = out / "backups" / item.relative_to(root)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(item, destination)
                if file_digest(item) != file_digest(destination):
                    raise ValueError("Backup verification failed: " + str(item))
                files.append({"source": str(item), "backup": str(destination), "sha256Before": file_digest(item)})
        changes.append({"assetPath": asset_path, "before": before, "files": files,
                        "originalGraphAudit": str(audit_path),
                        "newParentClass": AUTHORIZED_PARENTS[package(asset_path)], "migratedProperties": migrated,
                        "reparented": False, "compiled": False, "saved": False, "reloaded": False})
    for change in changes:
        bp = u.load_asset(change["assetPath"])
        base = u.load_class(None, change["newParentClass"])
        if package(change["assetPath"]) in (BEAM_PACKAGE, SUMMON_PACKAGE):
            if not u.MathLibrary.class_is_child_of(bp.generated_class(), base):
                raise ValueError("Configuration-only migration cannot change its original native parent")
        elif not u.MathLibrary.class_is_child_of(bp.generated_class(), base):
            u.BlueprintEditorLibrary.reparent_blueprint(bp, base)
            change["reparented"] = True
            u.BlueprintEditorLibrary.compile_blueprint(bp)
        cdo = u.get_default_object(bp.generated_class())
        for name in ("instancing_policy", "net_execution_policy"):
            cdo.set_editor_property(name, protected_policy_value(u, name, change["before"]["properties"][name]))
        for name, value in change["migratedProperties"].items():
            if name in ("shard_point_collection_class", "event_effect_class", "hit_react_effect_class"):
                value = u.load_class(None, value)
            elif name in ("shard_montage", "electrocute_montage", "summon_montage", "summon_ground_effect"):
                value = u.load_asset(value)
            elif name in ("summon_event_tag", "listen_tag"):
                tag = u.GameplayTag()
                tag.import_text(value)
                value = tag
            elif name == "initialized_event_tags":
                values = []
                for exported in value:
                    tag = u.GameplayTag()
                    tag.import_text(exported)
                    values.append(tag)
                value = values
            if value is None:
                raise ValueError("Cannot load Arcane original native configuration binding: " + name)
            cdo.set_editor_property(name, value)
        u.BlueprintEditorLibrary.compile_blueprint(bp)
        status = str(bp.get_editor_property("status"))
        if "UP_TO_DATE" not in status.upper():
            raise ValueError("Reparented Blueprint did not compile: " + change["assetPath"] + " " + status)
        change["compiled"] = True
        assert_saved_change(u, bp, change)
        if not u.EditorAssetLibrary.save_loaded_asset(bp, False):
            raise ValueError("GA package save failed: " + change["assetPath"])
        change["saved"] = True
        assert_saved_change(u, bp, change)
        change["inMemoryVerified"] = True
        change["requiresIndependentReadback"] = True
        for item in change["files"]:
            item["sha256After"] = file_digest(Path(item["source"]))


def graph_evidence(graph):
    errors = graph.validate_for_authoring()
    if errors is None or list(errors):
        raise ValueError("Compiled graph did not pass native graph validation: " + repr(errors))
    nodes = []
    for node in graph.get_editor_property("nodes"):
        config = node.get_editor_property("config")
        original = config.get_editor_property("presentation_parameters")
        nodes.append({"nodeId": node.get_editor_property("node_id").export_text(),
                      "typeId": str(node.get_editor_property("type_id")),
                      "sourceAsset": original.get_path_name(),
                      "legacyDepth": config.get_editor_property("legacy_depth"),
                      "singleProjectile": config.get_editor_property("single_projectile")})
    edges = [{"from": edge.get_editor_property("from").export_text(),
              "to": edge.get_editor_property("to").export_text(),
              "port": str(edge.get_editor_property("output_port"))}
             for edge in graph.get_editor_property("edges")]
    return {"schemaVersion": graph.get_editor_property("schema_version"),
            "profile": str(graph.get_editor_property("compatibility_profile")),
            "budgetPolicy": str(graph.get_editor_property("budget_policy")),
            "budget": graph.get_editor_property("budget").export_text(),
            "nodes": nodes, "edges": edges}


def run_unreal():
    import unreal as u
    root = Path(u.Paths.project_dir()).resolve()
    run_id = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    out = root / "Saved/GAAuthoring/UnifiedGA/CatalogMigration" / run_id
    out.mkdir(parents=True, exist_ok=False)
    apply_value = os.environ.get("UNIFIED_GA_MIGRATION_APPLY", "0")
    report = {"schemaVersion": 1, "runId": run_id, "startedAtUtc": utc(), "processId": os.getpid(),
              "dryRun": apply_value != "1", "success": False,
              "independentProcessReadback": False, "assetWrites": [], "errors": [], "rows": [], "compilations": []}
    destination = out / "catalog-report.json"
    try:
        if apply_value not in ("", "0", "1"):
            raise ValueError("UNIFIED_GA_MIGRATION_APPLY must be absent, 0 or 1")
        if apply_value == "1" and os.environ.get("UNIFIED_GA_MIGRATION_VERIFY_REPORT"):
            raise ValueError("Independent verification must run without APPLY")
        matrix_file = root / "docs/ga-unified-migration-matrix.json"
        matrix = json.loads(matrix_file.read_text(encoding="utf-8-sig"))
        report["matrixSource"] = str(matrix_file)
        report["matrixSha256"] = file_digest(matrix_file)
        if matrix.get("schemaVersion") != 1:
            raise ValueError("Unsupported reviewed matrix schema")
        report["nativeBindings"] = native_bindings(u)
        observed, expected, valid_paths, scan = audit.scan_unreal(u)
        report["scan"] = scan
        audit.validate(observed, expected, valid_paths)
        if scan["errors"] or scan["unknownAssets"]:
            raise ValueError("Fresh inventory has unresolved rows or references")
        for row in observed:
            if row["evidence"]["audit"]["kind"] == "GameplayAbilityBlueprint":
                bp = u.load_asset(row["assetPath"])
                description = json.loads(u.AbilityBlueprintAuditLibrary.describe_blueprint(bp))
                row["evidence"]["audit"]["blueprintInspection"] = blueprint_semantics(description)
        classified = classify_catalog(observed)
        report["inventoryRows"] = observed
        report["inventoryFingerprint"] = digest(observed)
        report["classifierSha256"] = file_digest(Path(__file__))
        report["rows"] = classified
        for row in classified:
            if package(row["assetPath"]) in AUTHORIZED_PARENTS:
                bp = u.load_asset(row["assetPath"])
                original = utility_snapshot(u, bp)
                description = json.loads(u.AbilityBlueprintAuditLibrary.describe_blueprint(bp))
                row["nativeConfigurationMigration"] = native_settings(package(row["assetPath"]), description, original["properties"])
                row["requiresConfigurationMigration"] = row["requiresArcaneReparent"] or row["requiresUtilityReparent"] or migrated_snapshot(
                    u, bp, row["nativeConfigurationMigration"]) != row["nativeConfigurationMigration"]
        report["baselineCounts"] = assert_coverage(matrix["rows"], observed, classified, expected,
                                                   matrix.get("inventory", {}).get("counts"))
        report["counts"] = dict(collections.Counter(row["kind"] for row in classified))
        report["routeCounts"] = dict(collections.Counter(row["route"] for row in classified))
        report["descendantEvidence"] = assert_descendants(u, observed, allow_pending=True)
        for row in classified:
            row["routeSourceFingerprints"] = route_source_evidence(root, row)
            if row["assetPath"].startswith("/Game/"):
                row["packageSha256"] = file_digest(content_file(root, row["assetPath"]))
            else:
                original = next(item for item in observed if item["assetPath"] == row["assetPath"])
                header = original["evidence"]["audit"].get("source", {}).get("sourceHeader")
                if not header or header == "Unresolved":
                    raise ValueError("Native entry lacks a verified source binding: " + row["assetPath"])
                row["sourceHeader"] = header
                row["sourceSha256"] = file_digest(root / header)
        by_path = audit.indexed(classified)
        for request in compilation_requests(observed, classified):
            record = dict(request)
            record["success"] = False
            try:
                obj = u.load_asset(request["assetPath"])
                graph = normalize_compile_result(u.AbilityLegacyGraphCompiler.compile_for_authoring(obj, request["profile"]))
                data = graph_evidence(graph)
                data["sourceFingerprints"] = {node["sourceAsset"]: by_path[node["sourceAsset"]]["packageSha256"]
                                               for node in data["nodes"]}
                record.update({"success": True, "definition": data, "graphFingerprint": digest(data)})
                for owner in request["requestedBy"]:
                    if owner in by_path:
                        entry = by_path[owner]
                        entry["strategyTypeIds"] = sorted(set(entry["strategyTypeIds"]) | {node["typeId"] for node in data["nodes"]})
            except Exception as error:
                record["error"] = str(error)
                report["errors"].append({"assetPath": request["assetPath"], "profile": request["profile"], "error": str(error)})
            report["compilations"].append(record)
        if report["errors"]:
            raise ValueError("Native compilation rejected one or more configurations; no assets were modified")
        if apply_value == "1":
            reparent_utilities(u, root, out, classified, report["assetWrites"])
            report["descendantEvidence"] = assert_descendants(u, observed, allow_pending=False)
            for change in report["assetWrites"]:
                entry = by_path[change["assetPath"]]
                entry["parentClass"] = change["newParentClass"]
                entry["requiresUtilityReparent"] = False
                entry["requiresArcaneReparent"] = False
                entry["requiresConfigurationMigration"] = False
                if package(change["assetPath"]) == ARCANE_PACKAGE:
                    entry.update({"route": "Graph.ArcaneShards", "actionComposition": "RegisteredGraphStrategy",
                                  "graphComposable": True, "strategyTypeIds": ["Shape.ArcaneShards"],
                                  "entryDispatch": "GraphInstance",
                                  "routeSourceFiles": ["Source/test_25d/Private/AbilitySystem/Ability/MyArcaneShardAbility.cpp",
                                                       "Source/test_25d/Private/AbilitySystem/Unified/Nodes/AbilityArcaneShardNode.cpp"]})
                    entry["routeSourceFingerprints"] = route_source_evidence(root, entry)
                elif package(change["assetPath"]) in UTILITY_PACKAGES:
                    native_name = change["newParentClass"].rsplit(".", 1)[-1]
                    binding = next(item for item in REGISTERED_ROUTES if item[0] == native_name)
                    entry.update({"route": binding[1], "actionComposition": "RegisteredSharedAction", "graphComposable": True,
                                  "strategyTypeIds": [binding[2]], "entryDispatch": "SharedRegisteredAction",
                                  "routeSourceFiles": ["Source/test_25d/Private/AbilitySystem/Ability/" + binding[3],
                                                       "Source/test_25d/Private/AbilitySystem/Unified/Nodes/" + binding[4]]})
                    entry["routeSourceFingerprints"] = route_source_evidence(root, entry)
                entry["packageSha256"] = file_digest(content_file(root, change["assetPath"]))
        previous_report = os.environ.get("UNIFIED_GA_MIGRATION_VERIFY_REPORT")
        if previous_report:
            previous = json.loads(Path(previous_report).read_text(encoding="utf-8-sig"))
            if not previous.get("success") or previous.get("dryRun") or previous.get("processId") == os.getpid():
                raise ValueError("Readback requires a successful APPLY report from a different process")
            if previous.get("matrixSha256") != report["matrixSha256"]:
                raise ValueError("Reviewed baseline changed since APPLY")
            prior_reports = json.loads(os.environ.get("UNIFIED_GA_MIGRATION_PRIOR_REPORTS", "[]"))
            if not isinstance(prior_reports, list) or any(not isinstance(path, str) for path in prior_reports):
                raise ValueError("UNIFIED_GA_MIGRATION_PRIOR_REPORTS must be a JSON array of APPLY report paths")
            lineage = saved_write_lineage(prior_reports + [previous_report])
            verified_lineage = []
            for asset_path, evidence in sorted(lineage.items()):
                if evidence["matrixSha256"] != report["matrixSha256"]:
                    raise ValueError("Write lineage uses another reviewed matrix")
                change = evidence["change"]
                bp = u.load_asset(change["assetPath"])
                assert_saved_change(u, bp, change)
                allowed_sources = {content_file(root, change["assetPath"]).with_suffix(suffix)
                                   for suffix in (".uasset", ".uexp", ".ubulk")}
                for item in change["files"]:
                    source = Path(item["source"]).resolve()
                    if source not in allowed_sources or file_digest(source) != item["sha256After"]:
                        raise ValueError("Saved package fingerprint changed after APPLY")
                    backup = Path(item["backup"]).resolve()
                    if not backup.is_relative_to(root / "Saved/GAAuthoring/UnifiedGA/CatalogMigration") or file_digest(backup) != item["sha256Before"]:
                        raise ValueError("Original backup fingerprint is missing or changed")
                verified_lineage.append({"assetPath": asset_path, "applyReport": evidence["report"],
                                         "applyReportSha256": evidence["reportSha256"], "applyProcessId": evidence["processId"],
                                         "originalSnapshot": change["before"], "migratedProperties": change.get("migratedProperties", {}),
                                         "newParentClass": change["newParentClass"], "files": change["files"],
                                         "compiled": True, "saved": True, "independentlyVerified": True})
            report["descendantEvidence"] = assert_descendants(u, observed, allow_pending=False)
            report["independentProcessReadback"] = True
            report["verifiedApplyReport"] = str(Path(previous_report).resolve())
            report["verifiedWriteLineage"] = verified_lineage
        report["pendingUtilityReparents"] = [row["assetPath"] for row in classified if row["requiresUtilityReparent"]]
        report["pendingArcaneReparents"] = [row["assetPath"] for row in classified if row["requiresArcaneReparent"]]
        report["pendingConfigurationMigrations"] = [row["assetPath"] for row in classified if row.get("requiresConfigurationMigration")]
        report["routeCounts"] = dict(collections.Counter(row["route"] for row in classified))
        report["unsupportedActionComposition"] = [row["assetPath"] for row in classified
                                                  if row["actionComposition"] in ("UnsupportedOpaqueBlueprint", "NativeBehaviorRequiresStrategy",
                                                                                   "HostBoundActionRequiresStrategy")]
        report["allRequiredActionsComposable"] = not report["unsupportedActionComposition"]
        report["allEntriesUseUnifiedLifecycle"] = all(item["baseVerified"] for item in report["descendantEvidence"])
        report["limitations"] = ["Route discovery and native compile are structural evidence; runtime and PIE need separate reports.",
                                 "Registered shared actions are composable; frontend warning/animation/counter recipes retain their original timing responsibilities.",
                                 "Legacy streaming uses bounded FIFO child admission; old recursive ordering is not claimed identical."]
        report["success"] = True
    except Exception as error:
        report["fatalError"] = str(error)
    finally:
        report["finishedAtUtc"] = utc()
        audit.write_json(destination, report)
        u.log("Unified GA catalog migration report: " + str(destination))
    if not report["success"]:
        raise RuntimeError("Catalog migration failed; inspect " + str(destination))


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        import unittest
        suite = unittest.defaultTestLoader.discover(str(Path(__file__).resolve().parent), "test_catalog_migration.py")
        if not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful():
            raise SystemExit(1)
    else:
        run_unreal()
