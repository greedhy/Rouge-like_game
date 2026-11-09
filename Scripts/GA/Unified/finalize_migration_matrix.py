"""Join fresh migration/readback, per-asset validation and named automation evidence.

Writes a reviewable candidate under Saved only. It never edits Content or the
reviewed docs matrix. Verified means migration code/configuration checks and the
listed shared behavior tests passed; visual PIE and two-client acceptance remain
separate Pending fields. Missing evidence is listed per asset, never filled green.
"""
import argparse
import collections
import copy
import datetime as dt
import hashlib
import json
from pathlib import Path
import re
import sys
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parent))
import migrate_catalog as migration

TEST_PREFIX = "test_25d.UnifiedGA."
STRATEGY_CASES = {
    "Delivery.ActorProjectiles": ["Traditional.NativeAndGraphProjectileParity"],
    "Spawn.Summon": ["Traditional.SummonCadenceOwnershipAndCancellation", "Traditional.NativeSummonNotifyAndCancel"],
    "Channel.Electrocute": ["Electrocute.CadenceCostAndFinalDamage", "Electrocute.CleanupPreservesNewTargetLife"],
    "Channel.OrbitingFireBolts": ["Orbit.StandaloneStrategyUpkeepAndCleanup", "Orbit.NewAttackRequiredAfterTargetReuse"],
    "Melee.Contact": ["Melee.ContactRadiusAndLegacyExtraHit"],
    "Shape.ArcaneNova": ["Nova.DamageSnapshotComposesPetrify"],
    "Shape.ArcaneShards": ["ArcaneShards.StaggerAndTail", "ArcaneShards.LegacyFriendlyActorDamage", "ArcaneShards.LegacyActorRejectsStaleLives"],
    "Delivery.ForkedChain": ["Batch6.ForkedChainDamageFalloffAndBudget", "Batch6.ProductionChainTargetDataCommit"],
    "Status.PetrifyArea": ["Batch6.AreaPetrifyRegisteredStrategy", "LegacyPetrify.MixedRangeCostAndCooldown"],
    "Combat.CharacterStrike": ["CharacterActions.StrikeProfilesAndCancellation"],
    "Delivery.WeaponProjectile": ["CharacterActions.WeaponProjectileDamageAndCancellation"],
    "Status.GuardWindow": ["CharacterActions.GuardOwnsOnlyItsEffect"],
    "Combat.TelegraphedContact": ["CharacterActions.TelegraphedUsesLockedSurface"],
    "Movement.Character": ["CharacterActions.MovementProfilesSweepAndCleanup", "CharacterActions.ConcurrentMovementOwnership"],
    "Delivery.EnemySling": ["EnemyActions.SlingNativeActorAndGraphCancellation"],
    "Spawn.ShamanMinions": ["EnemyActions.ShamanUsesExistingMinionOwnership"],
    "Move.GruxCharge": ["EnemySpecial.ChargeSweptHitIsDeduplicated", "EnemySpecial.SourceReuseCannotContinueCharge"],
    "Field.ShroomSpore": ["EnemySpecial.SporeWarningAndNoCatchup", "EnemySpecial.StunReleasesOwnedMovement"],
    "Status.GruxRoarArmor": ["EnemySpecial.RoarReusesOwnedArmorEffect"],
    "Effect.GameplayEffect": ["Utility.AttributeEventsApplyAndUnbind", "Utility.RegisteredEffectChangesHealth", "Utility.ClientListenerRetainsOriginalAscPermission"],
    "Status.OwnedEffect": ["Utility.OwnedEffectCleanup", "Utility.HitReactSourceLossEndsActivation"],
    "Effect.GameplayEffectTarget": ["Utility.QueryComposesActorTargetEffects", "Utility.DelayedTargetEffectRejectsReusedActor"],
    "Status.OwnedEffectTarget": ["Utility.QueryComposesActorTargetEffects"],
    "Control.Delay": ["Integration.ActorPoolReuseRejectsOldCast"],
    "Status.Petrify": ["PetrifyService.ActorLifecycle", "PetrifyService.MassLifecycle"],
}
FAMILY_TEST_FILES = {
    "Graph.LegacyActor": ["PrototypeTests.cpp", "AbilityLegacyDriverTests.cpp"],
    "Graph.LegacyMixed": ["PureMassMixedTargetTests.cpp", "PureMassMixedSurfaceTests.cpp", "AbilityLegacyDriverTests.cpp"],
    "Graph.LegacyExtension": ["ExtensionTests.cpp", "AbilityCrossFamilyCompositionTests.cpp"],
}
CONFIG_CASES = {
    "StanderWeaponActionConfig": ["test_25d.StanderWeapons.TimelineValidation"],
    "AbilityTimelineDefinition": ["test_25d.StanderWeapons.TimelineValidation"],
    "StanderCombatConfig": ["test_25d.Stander.AuthoredAssets", "test_25d.Stander.CounterRequiresBlockedDamage"],
    "SwordShieldConfig": ["test_25d.SwordShield.AuthoredAssets", "test_25d.SwordShield.GuardRealDamage"],
    "StoneGolemAttackConfig": [TEST_PREFIX + "CharacterActions.TelegraphedUsesLockedSurface"],
    "GruxAttackConfig": ["test_25d.Grux.AuthoredConfiguration"],
    "ShroomSporeConfig": ["test_25d.Shroom.AuthoredConfiguration"],
    "SlingAttackConfig": ["test_25d.LegacyMonsters.AuthoredConfiguration"],
    "ShamanSummonConfig": ["test_25d.Shaman.SummonConfigBounds"],
    "OrbitingFireBoltConfig": [TEST_PREFIX + "Orbit.StandaloneStrategyUpkeepAndCleanup"],
    "MonsterCombatConfig": [TEST_PREFIX + "CharacterActions.TelegraphedUsesLockedSurface"],
    "StoneGolemConfig": ["test_25d.StoneGolem.AuthoredConfiguration"],
    "AbilityGraphDefinition": [TEST_PREFIX + "Integration.AuthoredSample"],
}

# These are reviewed frontend contracts, not the union of a node's optional modes.
# In particular the old Melee/Nova/Shards frontends leave bIncludeMass=false.
FRONTEND_CONTRACTS = {
    "Graph.LegacyActor": (["Actor"], "IndependentCastEffect; normal GA end preserves cast, explicit cancellation closes it"),
    "Graph.LegacyMixed": (["Actor", "Mass"], "IndependentCastEffect; normal GA end preserves cast, explicit cancellation closes it"),
    "Graph.LegacyExtension": (["Actor", "Mass"], "IndependentCastEffect with shared cast context and explicit cancellation"),
    "Graph.Batch6Chain": (["Actor", "Mass"], "BoundToAbility; synchronous fork traversal uses captured target lives"),
    "Graph.Batch6Petrify": (["Actor", "Mass"], "Synchronous area execution; target-owned petrify state expires independently"),
    "Graph.Melee": (["Actor"], "BoundToAbility; montage notify executes one contact graph"),
    "Graph.ArcaneNova": (["Actor"], "BoundToAbility; montage notify executes one synchronous nova"),
    "Graph.ArcaneShards": (["Actor"], "BoundToAbility; staggered shard graph and point collection close on GA end"),
    "Graph.Electrocute": (["Actor"], "BoundToAbility; channel graph, upkeep and target delegates close on GA end"),
    "Graph.OrbitingFireBolts": (["Actor"], "BoundToAbility; channel graph, upkeep and owned orbit projectiles close on GA end"),
    "Graph.Summon": (["SourceActor", "SpawnedActor"], "BoundToAbility spawn cadence; spawned minions retain source-life ownership after normal completion"),
    "Action.ActorProjectiles": (["Actor"], "Synchronous launch; independent projectile actors retain captured source identity"),
    "Action.CharacterStrike": (["Actor", "Mass"], "BoundToAbility animation host; contact/guard/movement events retain their original timing and cleanup"),
    "Action.SwordShieldStrike": (["Actor"], "BoundToAbility animation host; Actor-only strikes and owned guard handle"),
    "Action.TelegraphedContact": (["Actor"], "BoundToAbility enemy warning/montage host; captured attack serial and source life"),
    "Action.EnemySling": (["Actor"], "BoundToAbility warning; launched projectile survives normal end and is destroyed on cancellation"),
    "Action.ShamanMinions": (["SourceActor", "SpawnedActor"], "BoundToAbility warning; character owns spawned minions and death cleanup"),
    "Action.GruxCharge": (["Actor"], "BoundToAbility warning/charge host; swept targets deduplicated and movement restored on stop"),
    "Action.GruxRoar": (["SelfActor"], "BoundToAbility warning; owned timed armor survives normal end, cancellation removes its exact handle"),
    "Action.ShroomSpore": (["Actor"], "BoundToAbility warning/spore field; stop closes pulses and restores owned movement"),
    "Action.AttributeEvents": (["SelfActor"], "BoundToAbility attribute-event subscription; original ASC permission and GE duration policies retained"),
    "Action.HitReact": (["SelfActor"], "BoundToAbility; owned reaction GE removed at blend-out, interruption or source loss"),
    "Session.PassiveListener": (["SelfActor"], "BoundToAbility passive activation listener; exact delegate removed on end"),
    "Session.AttackSelector": (["Actor"], "Synchronous attack selection; selected attack owns warning and action lifetime"),
    "Session.Native": (["DerivedAbilityDefined"], "Base activation session only; concrete derived ability defines target and action lifetime"),
    "Session.BlueprintUtility": (["NoGameplayTargets"], "BoundToAbility; reviewed original utility print/delay/end graph"),
}

STRATEGY_TARGETS = {
    "Control.Delay": ["Actor", "Mass"], "Status.Petrify": ["Actor", "Mass"],
    "Status.PetrifyArea": ["Actor", "Mass"], "Delivery.ForkedChain": ["Actor", "Mass"],
    "Query.Shape": ["Actor", "Mass"], "Effect.Damage": ["Actor", "Mass"],
    "Target.ResolveImpact": ["Actor", "Mass"], "Target.ResolveDamageTarget": ["Actor", "Mass"],
    "Combat.CharacterStrike": ["Actor", "Mass"], "Delivery.WeaponProjectile": ["Actor", "Mass"],
    "Status.GuardWindow": ["SelfActor"], "Movement.Character": ["SourceCharacter"],
    "Effect.GameplayEffect": ["SelfActor"], "Status.OwnedEffect": ["SelfActor"],
    "Effect.GameplayEffectTarget": ["Actor"], "Status.OwnedEffectTarget": ["Actor"],
    "Delivery.ActorProjectiles": ["Actor"], "Channel.Electrocute": ["Actor"],
    "Channel.OrbitingFireBolts": ["Actor"], "Combat.TelegraphedContact": ["Actor"],
    "Delivery.EnemySling": ["Actor"], "Move.GruxCharge": ["Actor"], "Field.ShroomSpore": ["Actor"],
    "Status.GruxRoarArmor": ["SelfActor"], "Spawn.Summon": ["SourceActor", "SpawnedActor"],
    "Spawn.ShamanMinions": ["SourceActor", "SpawnedActor"],
}


def graph_targets(nodes):
    targets = set()
    for node in nodes:
        type_id = node.get("type_id", node.get("typeId", ""))
        if type_id.startswith("Legacy.Actor."):
            targets.add("Actor")
        elif type_id.startswith(("Legacy.Mixed.", "Legacy.Extension.")):
            targets.update(("Actor", "Mass"))
        elif type_id in {"Melee.Contact", "Shape.ArcaneNova", "Shape.ArcaneShards"}:
            targets.add("Actor")
            if node.get("config", {}).get("include_mass") is True:
                targets.add("Mass")
        elif type_id in STRATEGY_TARGETS:
            targets.update(STRATEGY_TARGETS[type_id])
        else:
            raise ValueError("No reviewed target contract for graph node: " + type_id)
    return sorted(targets) or ["NoGameplayTargets"]


def reviewed_contract(route, inventory_row, inventory, compilations):
    """Describe this frontend/configuration; never promote optional node capability to legacy behavior."""
    properties = inventory_row["evidence"]["audit"].get("properties", {})
    name = route["route"]
    if name in FRONTEND_CONTRACTS:
        targets, lifetime = FRONTEND_CONTRACTS[name]
        return list(targets), lifetime
    if name == "Action.Weapon":
        targets = graph_targets([{"typeId": value} for value in route["strategyTypeIds"]])
        return targets, "BoundToAbility animation events; owned guards/movement close on end; launched projectiles survive normal end only"
    if name == "Graph.Unified":
        graph = inventory.get(properties.get("graph"))
        targets = graph_targets(graph["evidence"]["audit"]["properties"]["nodes"]) if graph else ["ConfiguredGraphDefined"]
        lifetime = properties.get("execution_lifetime", "")
        if "BOUND_TO_ABILITY" in lifetime:
            return targets, "BoundToAbility configured graph; target-persistent state retains its own duration"
        if "INDEPENDENT_CAST_EFFECT" in lifetime:
            return targets, "IndependentCastEffect configured graph; world-owned after normal GA end, source identity still required"
        raise ValueError("Unreviewed UnifiedGraphAbility execution lifetime: " + str(lifetime))
    if route["kind"] != "Configuration":
        raise ValueError("No reviewed frontend target/lifetime contract: " + name)
    config_class = inventory_row["generatedClass"].rsplit(".", 1)[-1]
    prefix = "ConfigurationOnly; no activation or runtime state; "
    if config_class in {"AbilityPrototype", "Batch6FieldPrototype", "ExtensionNode"}:
        records = [item for item in compilations if item["assetPath"] == route["assetPath"]]
        nodes = [node for record in records for node in record["definition"]["nodes"]
                 if node["sourceAsset"] == route["assetPath"]]
        if not nodes:
            raise ValueError("Configuration has no actual compiled target profile: " + route["assetPath"])
        return graph_targets(nodes), prefix + "per-cast snapshot in " + ", ".join(sorted({item["profile"] for item in records}))
    if config_class == "AbilityGraphDefinition":
        return graph_targets(properties["nodes"]), prefix + "graph owner chooses lifetime; status nodes own target durations"
    if config_class == "StanderWeaponActionConfig":
        mapping = {"MELEE": "Combat.CharacterStrike", "PROJECTILE": "Delivery.WeaponProjectile",
                   "GUARD": "Status.GuardWindow", "MOVE": "Movement.Character"}
        nodes = []
        for event in properties.get("events", []):
            match = re.search(r"StanderWeaponActionKind\.([A-Z_]+)", event["kind"])
            kind = match.group(1) if match else None
            if kind == "PRESENTATION":
                continue
            if kind not in mapping:
                raise ValueError("Unreviewed configured weapon event: " + str(event["kind"]))
            nodes.append({"typeId": mapping[kind]})
        return graph_targets(nodes), prefix + "bound animation host consumes exact authored events"
    if config_class == "GruxAttackConfig":
        return (["SelfActor"] if ".ROAR:" in properties["kind"] else ["Actor"]), prefix + "Grux warning/charge/combo/owned armor host selected by Kind"
    configurations = {
        "AbilityTimelineDefinition": (["NoGameplayTargets"], "animation position/phase timing only; consumer defines gameplay events"),
        "StanderCombatConfig": (["Actor", "Mass", "SelfActor", "SourceCharacter"], "Stander mixed strikes, guard and dash host"),
        "SwordShieldConfig": (["Actor", "SelfActor"], "SwordShield Actor-only strike and guard host"),
        "StoneGolemAttackConfig": (["Actor"], "telegraphed Actor-only contact host"),
        "ShroomSporeConfig": (["Actor"], "bound warning/spore pulse host"),
        "SlingAttackConfig": (["Actor"], "warning host and launched original sling actor"),
        "ShamanSummonConfig": (["SourceActor", "SpawnedActor"], "character-owned minion lifecycle"),
        "OrbitingFireBoltConfig": (["Actor"], "bound orbit channel and owned projectile lifecycle"),
        "MonsterCombatConfig": (["ConfiguredAttackDefined"], "model/damage/attack-class configuration; selected concrete attack defines targets and lifetime"),
        "StoneGolemConfig": (["ConfiguredAttackDefined"], "model/damage/attack-class configuration; selected concrete attack defines targets and lifetime"),
    }
    if config_class not in configurations:
        raise ValueError("No reviewed configuration target/lifetime contract: " + config_class)
    targets, lifetime = configurations[config_class]
    return list(targets), prefix + lifetime


def apply_reviewed_matrix_contract(result, route, targets, lifetime, names, report_paths):
    """Attach explicit code/evidence review, retaining prior reviews and pending visual acceptance."""
    result["replacementPath"] = None  # Existing UE identities are migrated in place.
    result["targetSupport"] = targets
    result["lifecycle"] = lifetime
    review = {"kind": "MigrationCodeAndEvidenceReview", "reviewer": "Codex migration code review",
              "assetPath": result["assetPath"], "inPlaceMigration": True, "implementationRoute": route["route"],
              "strategyTypeIds": route["strategyTypeIds"], "configPaths": route["configPaths"],
              "targetSupportBasis": "Actual frontend defaults/configured events or actual compiled profile; optional graph modes do not widen old GA behavior",
              "targetSupport": targets, "lifecycle": lifetime, "tests": names, "reports": report_paths,
              "implementationFingerprints": route["routeSourceFingerprints"],
              "scope": "Reviewed code route and named shared behavior/configuration evidence; not a separate complete cast of every asset",
              "pieVisualAcceptance": "Pending", "twoClientAcceptance": "Pending"}
    review["reviewId"] = migration.digest(review)
    manual = result["evidence"].setdefault("manual", [])
    if not any(isinstance(item, dict) and item.get("reviewId") == review["reviewId"] for item in manual):
        manual.append(review)


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def discover_cases(root):
    result = {}
    pattern = re.compile(r'IMPLEMENT_(?:SIMPLE|CUSTOM_SIMPLE)_AUTOMATION_TEST\s*\(\s*[^,]+,\s*"([^"]+)"', re.S)
    for path in (root / "Source/test_25d/Private/Tests").glob("*.cpp"):
        for match in pattern.finditer(path.read_text(encoding="utf-8-sig")):
            if match.group(1) in result:
                raise ValueError("Duplicate automation declaration: " + match.group(1))
            result[match.group(1)] = path
    return result


INPUT_SCOPE = "Source+Config+ProjectDLL+MatrixContentPackages+ProjectFile"


def current_input_manifest(root):
    """Match test.ps1 input membership, including added headers/classes and sidecars."""
    root = Path(root).resolve()
    files = set()
    for folder in ("Source", "Config"):
        directory = root / folder
        if not directory.is_dir():
            raise ValueError("Missing validation input directory: " + str(directory))
        files.update(path.resolve() for path in directory.rglob("*") if path.is_file())
    libraries = {path.resolve() for path in (root / "Binaries/Win64").rglob("*")
                 if path.is_file() and path.suffix.lower() == ".dll"}
    if not libraries:
        raise ValueError("No project binaries available for validation")
    files.update(libraries)
    files.add(root / "test_25d.uproject")
    packages = set()
    for row in read(root / "docs/ga-unified-migration-matrix.json")["rows"]:
        asset = row["assetPath"]
        if asset.startswith("/Script/"):
            continue
        package_file = migration.content_file(root, asset)
        if not package_file.is_file():
            raise ValueError("Missing matrix package input: " + str(package_file))
        packages.add(migration.package(asset).lower())
        files.add(package_file)
        files.update(package_file.with_suffix(suffix) for suffix in (".uexp", ".ubulk")
                     if package_file.with_suffix(suffix).is_file())
    if not packages:
        raise ValueError("Matrix contains no project asset inputs")
    if any(not path.is_relative_to(root) for path in files):
        raise ValueError("Validation input escaped project")
    return {"matrixAssetCount": len(packages),
            "files": {path.relative_to(root).as_posix(): migration.file_digest(path) for path in sorted(files)}}


def validate_automation_manifest(root, report_path, current):
    """Require the actual unique run record; never manufacture evidence for old reports."""
    run_id = report_path.parent.name
    expected_report = root / "Saved/Automation/UnifiedGA" / run_id / "index.json"
    if not re.fullmatch(r"\d{8}T\d{6}-[0-9a-f]{8}", run_id) or report_path != expected_report.resolve():
        raise ValueError("Automation report is not in its unique project run directory")
    validation_path = root / "Saved/GAAuthoring/UnifiedGA" / run_id / "validation.json"
    if not validation_path.is_file():
        raise ValueError("Automation run has no input validation manifest: " + run_id)
    validation = read(validation_path)
    manifest = validation.get("inputManifest", {})
    if (validation.get("schemaVersion") != 2 or validation.get("success") is not True or
            validation.get("exitCode") != 0 or validation.get("runId") != run_id or
            manifest.get("schemaVersion") != 1 or manifest.get("runId") != run_id or
            manifest.get("scope") != INPUT_SCOPE or manifest.get("unchanged") is not True):
        raise ValueError("Automation run manifest is failed, incomplete or belongs to another run")
    if (Path(validation.get("reportFile", "")).resolve() != report_path or
            Path(validation.get("reportDirectory", "")).resolve() != report_path.parent or
            validation.get("reportSha256") != migration.file_digest(report_path)):
        raise ValueError("Automation report does not match its run manifest fingerprint")
    before, after = manifest.get("before", {}), manifest.get("after", {})
    for snapshot in (before, after):
        if snapshot.get("files") != current["files"] or snapshot.get("matrixAssetCount") != current["matrixAssetCount"]:
            raise ValueError("Automation input fingerprints or membership differ from current project")
    try:
        timestamps = [dt.datetime.fromisoformat(value.replace("Z", "+00:00")) for value in
                      (before["capturedUtc"], validation["startedUtc"], after["capturedUtc"], validation["finishedUtc"])]
        if any(stamp.tzinfo is None for stamp in timestamps) or timestamps != sorted(timestamps):
            raise ValueError("unordered timestamps")
        report_time = dt.datetime.fromtimestamp(report_path.stat().st_mtime, dt.timezone.utc)
        if not timestamps[1] <= report_time <= timestamps[3]:
            raise ValueError("report was not written during this run")
    except (KeyError, TypeError, ValueError) as error:
        raise ValueError("Automation manifest does not establish a fresh executed run: " + str(error)) from error
    return validation, validation_path


def load_results(paths, declarations, root=None):
    root = Path(root or Path(__file__).resolve().parents[3]).resolve()
    current = current_input_manifest(root)
    results = {}
    for filename in paths:
        path = Path(filename).resolve()
        report = read(path)
        if any(report.get(name, -1) != 0 for name in ("failed", "notRun", "inProcess")) or not report.get("tests"):
            raise ValueError("Automation report is incomplete or failed: " + str(path))
        validation, validation_path = validate_automation_manifest(root, path, current)
        seen = set()
        for test in report["tests"]:
            name = test["fullTestPath"]
            if name in seen or test.get("state") != "Success":
                raise ValueError("Duplicate/unsuccessful test: " + name)
            seen.add(name)
            source = declarations.get(name)
            if source and source.stat().st_mtime > path.stat().st_mtime:
                raise ValueError("Test source changed after automation report: " + name)
            results[name] = {"name": name, "state": "Success", "report": str(path),
                             "reportSha256": migration.file_digest(path),
                             "runId": validation["runId"], "inputValidation": str(validation_path),
                             "inputValidationSha256": migration.file_digest(validation_path),
                             "source": str(source) if source else None,
                             "sourceSha256": migration.file_digest(source) if source else None}
        expected, executed = validation.get("expectedTests"), validation.get("executedTests")
        if (not isinstance(expected, list) or not expected or not set(expected).issubset(seen) or
                not isinstance(executed, list) or len(executed) != len(seen) or set(executed) != seen):
            raise ValueError("Automation run manifest does not match expected/executed cases")
    return results


def expected_cases(route, inventory, declarations):
    names, gaps = set(), []
    if route["kind"] == "Configuration":
        config_class = inventory["generatedClass"].rsplit(".", 1)[-1]
        names.update(CONFIG_CASES.get(config_class, []))
        if config_class == "StanderWeaponActionConfig":
            event_strategies = {"MELEE": "Combat.CharacterStrike", "PROJECTILE": "Delivery.WeaponProjectile",
                                "GUARD": "Status.GuardWindow", "MOVE": "Movement.Character"}
            for event in inventory["evidence"]["audit"]["properties"].get("events", []):
                match = re.search(r"StanderWeaponActionKind\.([A-Z_]+)", event["kind"])
                strategy = event_strategies.get(match.group(1)) if match else None
                if strategy:
                    names.update(TEST_PREFIX + suffix for suffix in STRATEGY_CASES[strategy])
        if config_class in {"AbilityPrototype", "Batch6FieldPrototype", "ExtensionNode"}:
            family = "Graph.LegacyExtension" if config_class == "ExtensionNode" else "Graph.LegacyMixed"
            names.update(name for name, source in declarations.items() if source.name in FAMILY_TEST_FILES[family])
    elif route["route"] in FAMILY_TEST_FILES:
        names.update(name for name, source in declarations.items() if source.name in FAMILY_TEST_FILES[route["route"]])
        names.add(TEST_PREFIX + "Migration.AllKindsCompileAndCyclesUnroll")
    else:
        for strategy in route["strategyTypeIds"]:
            if not STRATEGY_CASES.get(strategy):
                gaps.append("No reviewed behavior mapping for strategy " + strategy)
            names.update(TEST_PREFIX + suffix for suffix in STRATEGY_CASES.get(strategy, []))
    if route["route"] == "Graph.Unified":
        names.add(TEST_PREFIX + "Integration.AuthoredSample")
    if route["route"].startswith("Session."):
        names.add(TEST_PREFIX + "Entry.RealGasCommitPassiveReactivation")
    if route.get("frontendRecipe") and "guard" in route["frontendRecipe"].lower():
        names.add("test_25d.Stander.CounterRequiresBlockedDamage" if "Stander" in route.get("reviewedNativeClass", "")
                  else "test_25d.SwordShield.GuardRealDamage")
    if not names:
        gaps.append("No reviewed behavior case mapped to this configuration/entry")
    return sorted(names), gaps


def checked_report(path, label):
    report = read(path)
    if not report.get("success"):
        raise ValueError(label + " report is not successful")
    return report


def validate_write_lineage(root, catalog):
    """Preserve only real saved writes and their independently read-back package bytes."""
    entries = catalog.get("verifiedWriteLineage", [])
    indexed = migration.audit.indexed(entries)
    if (len(indexed) != len(migration.AUTHORIZED_PARENTS) or
            {migration.package(path) for path in indexed} != set(migration.AUTHORIZED_PARENTS)):
        raise ValueError("Verified write lineage must cover the six authorized assets exactly")
    for asset, entry in indexed.items():
        if (any(entry.get(flag) is not True for flag in ("saved", "compiled", "independentlyVerified")) or
                entry.get("newParentClass") != migration.AUTHORIZED_PARENTS[migration.package(asset)]):
            raise ValueError("Write lineage is incomplete or has an unexpected native parent: " + asset)
        apply_path = Path(entry["applyReport"]).resolve()
        if migration.file_digest(apply_path) != entry.get("applyReportSha256"):
            raise ValueError("Original APPLY report changed since readback: " + asset)
        apply = read(apply_path)
        # An APPLY may save early assets and fail on a later one. Its actual saved
        # rows remain the provenance; the current independent readback must pass.
        if (apply.get("dryRun") is not False or
                apply.get("processId") != entry.get("applyProcessId") or
                apply.get("processId") == catalog.get("processId")):
            raise ValueError("Write lineage lacks a separate actual APPLY process: " + asset)
        changes = [change for change in apply.get("assetWrites", [])
                   if change.get("assetPath") == asset and change.get("saved") is True and change.get("compiled") is True]
        if len(changes) != 1:
            raise ValueError("Original APPLY does not contain exactly one actual saved write: " + asset)
        change = changes[0]
        if (change.get("before") != entry.get("originalSnapshot") or
                change.get("migratedProperties", {}) != entry.get("migratedProperties") or
                change.get("newParentClass") != entry.get("newParentClass") or
                change.get("files") != entry.get("files")):
            raise ValueError("Readback write lineage differs from its original APPLY record: " + asset)
        package = migration.content_file(root, asset)
        expected = {package.with_suffix(suffix) for suffix in (".uasset", ".uexp", ".ubulk")
                    if package.with_suffix(suffix).is_file()}
        observed = set()
        for record in entry.get("files", []):
            source, backup = Path(record["source"]).resolve(), Path(record["backup"]).resolve()
            if source not in expected or source in observed or not backup.is_relative_to((root / "Saved").resolve()):
                raise ValueError("Write lineage package/backup escaped its authorized boundary: " + asset)
            observed.add(source)
            if (migration.file_digest(source) != record.get("sha256After") or
                    migration.file_digest(backup) != record.get("sha256Before")):
                raise ValueError("Original backup or saved package changed since readback: " + asset)
        if observed != expected or package not in observed:
            raise ValueError("Write lineage omitted package bytes: " + asset)
    return indexed


def make_candidate(root, catalog_path, blueprint_path, configuration_path, automation_paths):
    catalog = checked_report(catalog_path, "Catalog")
    blueprints = checked_report(blueprint_path, "Blueprint compilation")
    configs = checked_report(configuration_path, "Configuration validation")
    if not catalog.get("independentProcessReadback") or not catalog.get("allEntriesUseUnifiedLifecycle"):
        raise ValueError("Catalog must be the fresh independent-process readback")
    if catalog.get("classifierSha256") != migration.file_digest(Path(migration.__file__)):
        raise ValueError("Classifier changed since fresh readback; rerun catalog audit")
    write_lineage = validate_write_lineage(root, catalog)
    if any(catalog.get(name) for name in ("pendingUtilityReparents", "pendingArcaneReparents", "pendingConfigurationMigrations", "unsupportedActionComposition")):
        raise ValueError("Catalog still has unresolved migration actions")
    inventory = migration.audit.indexed(catalog["inventoryRows"])
    routes = migration.audit.indexed(catalog["rows"])
    if set(inventory) != set(routes) or migration.digest(catalog["inventoryRows"]) != catalog["inventoryFingerprint"]:
        raise ValueError("Fresh inventory and route evidence differ")
    bp_by_path, config_by_path = migration.audit.indexed(blueprints["rows"]), migration.audit.indexed(configs["rows"])
    for kind, evidence in (("GameplayAbilityBlueprint", bp_by_path), ("Configuration", config_by_path)):
        if {path for path, row in routes.items() if row["kind"] == kind} != set(evidence):
            raise ValueError(kind + " validation coverage differs from fresh inventory")
    declarations = discover_cases(root)
    results = load_results(automation_paths, declarations, root)
    missing_unified = sorted(name for name in declarations if name.startswith(TEST_PREFIX) and name not in results)
    if missing_unified:
        raise ValueError("Full current UnifiedGA suite is incomplete: " + ", ".join(missing_unified))
    baseline_path = root / "docs/ga-unified-migration-matrix.json"
    baseline = read(baseline_path)
    if migration.file_digest(baseline_path) != catalog["matrixSha256"]:
        raise ValueError("Reviewed baseline matrix changed after catalog readback")
    previous = migration.audit.indexed(baseline["rows"])
    review_reports = {"catalog": str(Path(catalog_path).resolve()),
                      "blueprintValidation": str(Path(blueprint_path).resolve()),
                      "configurationValidation": str(Path(configuration_path).resolve()),
                      "automation": [str(Path(path).resolve()) for path in automation_paths]}
    candidate, unresolved = [], []
    for path, route in sorted(routes.items()):
        issues = []
        for source, expected in route["routeSourceFingerprints"].items():
            if migration.file_digest(root / source) != expected:
                raise ValueError("Route source changed since catalog readback: " + source)
        if route["kind"] == "NativeGameplayAbility" and migration.file_digest(root / route["sourceHeader"]) != route["sourceSha256"]:
            raise ValueError("Native declaration changed since catalog readback: " + route["sourceHeader"])
        if path.startswith("/Game/"):
            asset_file = migration.content_file(root, path)
            if migration.file_digest(asset_file) != route["packageSha256"]:
                raise ValueError("Asset package changed since catalog readback: " + path)
        if path in bp_by_path:
            row = bp_by_path[path]
            if not row.get("success") or not row.get("graphUnchanged") or row.get("packageSha256") != route["packageSha256"]:
                raise ValueError("Blueprint compile/semantic preservation mismatch: " + path)
        if path in config_by_path:
            row = config_by_path[path]
            if not row.get("success") or row.get("errors"):
                issues.append("Native configuration validation failed")
            if hashlib.md5(migration.content_file(root, path).read_bytes()).hexdigest().lower() != row.get("packageMd5", "").lower():
                raise ValueError("Configuration package changed since validation: " + path)
        names, gaps = expected_cases(route, inventory[path], declarations)
        issues.extend(gaps)
        issues.extend("Missing passing behavior test: " + name for name in names if name not in results)
        compilation = [record for record in catalog["compilations"] if record["assetPath"] == path or path in record["requestedBy"]]
        if any(not record.get("success") for record in compilation):
            issues.append("Native legacy graph compilation failed")
        result = copy.deepcopy(inventory[path])
        if path in previous:
            result["evidence"]["manual"] = copy.deepcopy(previous[path]["evidence"].get("manual", []))
        result["migrationStatus"] = "Verified" if not issues else "Adapting"
        result["compatibilityProfile"] = route.get("profile") or route["route"]
        targets, lifetime = reviewed_contract(route, inventory[path], inventory, catalog["compilations"])
        apply_reviewed_matrix_contract(result, route, targets, lifetime,
                                       [name for name in names if name in results], review_reports)
        result["issues"] = issues
        result["evidence"]["migration"] = {
            "route": route, "catalogReport": str(Path(catalog_path).resolve()),
            "verifiedWriteLineage": write_lineage.get(path),
            "configurationValidation": config_by_path.get(path), "blueprintValidation": bp_by_path.get(path),
            "referencedConfigurationValidation": [config_by_path[ref] for ref in route["configPaths"] if ref in config_by_path],
            "compiledProfiles": [{key: record[key] for key in ("assetPath", "profile", "graphFingerprint")} for record in compilation],
            "behaviorScope": "Named shared executor behavior cases plus per-asset native configuration validation; not every visual/data permutation",
            "behaviorCases": [results[name] for name in names if name in results],
            "missingBehaviorCases": [name for name in names if name not in results],
            "pieVisualAcceptance": "Pending", "twoClientAcceptance": "Pending",
        }
        if issues:
            unresolved.append({"assetPath": path, "issues": issues})
        candidate.append(result)
    # Validate the actual publication schema, not just each upstream evidence report.
    migration.audit.validate(candidate, set(routes), set(inventory))
    output = copy.deepcopy(baseline)
    output["rows"] = candidate
    output["inventory"] = {"source": "Fresh catalog independent readback + explicit per-asset validation", "generatedAtUtc": migration.utc(),
                            "catalogReport": str(Path(catalog_path).resolve()), "counts": catalog["counts"]}
    summary = {"schemaVersion": 1, "success": not unresolved, "candidateOnly": True,
               "statusCounts": dict(collections.Counter(row["migrationStatus"] for row in candidate)),
               "unresolvedRows": unresolved, "pieVisualAcceptance": "Pending", "twoClientAcceptance": "Pending",
               "sources": {str(Path(path).resolve()): migration.file_digest(Path(path))
                           for path in [catalog_path, blueprint_path, configuration_path] + list(automation_paths)}}
    return output, summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", required=True)
    parser.add_argument("--blueprint-validation", required=True)
    parser.add_argument("--configuration-validation", required=True)
    parser.add_argument("--automation", action="append", required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    candidate, summary = make_candidate(root, args.catalog, args.blueprint_validation, args.configuration_validation, args.automation)
    out = root / "Saved/GAAuthoring/UnifiedGA/FinalMatrix" / (dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8])
    out.mkdir(parents=True, exist_ok=False)
    migration.audit.write_json(out / "matrix-candidate.json", candidate)
    migration.audit.write_json(out / "finalization-report.json", summary)
    print(json.dumps({"directory": str(out), "success": summary["success"], "statusCounts": summary["statusCounts"]}))


if __name__ == "__main__":
    main()
