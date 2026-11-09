"""Standalone contract tests for catalog classification and write containment; no Unreal import."""
import copy
import collections
import json
from pathlib import Path
import unittest
import tempfile

import migrate_catalog as migration


ROOT = Path(__file__).resolve().parents[3]


def fixture(path, ancestry, kind="GameplayAbilityBlueprint"):
    return {"assetPath": path, "generatedClass": ancestry[0], "parentClass": ancestry[1],
            "family": "Traditional", "configPaths": [],
            "evidence": {"audit": {"kind": kind, "ancestry": ancestry, "properties": {}}}}


class CatalogMigrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        matrix = json.loads((ROOT / "docs/ga-unified-migration-matrix.json").read_text(encoding="utf-8-sig"))
        cls.baseline = matrix["rows"]
        cls.reviewed_counts = matrix.get("inventory", {}).get("counts")

    def test_all_reviewed_rows_have_an_explicit_route(self):
        classified = [migration.classify(row) for row in self.baseline]
        counts = migration.assert_coverage(self.baseline, self.baseline, classified,
                                           {row["assetPath"] for row in self.baseline}, self.reviewed_counts)
        self.assertEqual(len(classified), len(self.baseline))
        self.assertEqual(dict(collections.Counter(row["kind"] for row in classified)), counts)
        self.assertEqual(set(counts), migration.INVENTORY_KINDS)

    def test_batch6_route_precedes_its_prototype_parent(self):
        expected = {"Batch6ChainAbility": "Graph.Batch6Chain", "Batch6PetrifyAbility": "Graph.Batch6Petrify",
                    "Batch6FieldAbility": "Graph.LegacyMixed", "ExtensionAbility": "Graph.LegacyExtension",
                    "PureMassAbility": "Graph.LegacyMixed", "UnifiedGraphAbility": "Graph.Unified"}
        for name, route in expected.items():
            row = next(row for row in self.baseline if row["assetPath"] == "/Script/test_25d." + name)
            self.assertEqual(migration.classify(row)["route"], route)

    def test_registered_routes_match_current_implementation_sources(self):
        for row in self.baseline:
            classified = migration.classify(row)
            evidence = migration.route_source_evidence(ROOT, classified)
            self.assertEqual(set(evidence), set(classified["routeSourceFiles"]))

    def test_precise_catalog_routes_are_backed_by_shared_implementations(self):
        routes = migration.classify_catalog(self.baseline)
        for route in routes:
            migration.route_source_evidence(ROOT, route)
        by_native = {row["reviewedNativeClass"]: row for row in routes if row["kind"] == "NativeGameplayAbility"}
        for native, strategy in (("GoblinSlingAbility", "Delivery.EnemySling"),
                                 ("ShamanSummonAbility", "Spawn.ShamanMinions"),
                                 ("GruxChargeAbility", "Move.GruxCharge"),
                                 ("GruxRoarAbility", "Status.GruxRoarArmor"),
                                 ("ShroomSporeAbility", "Field.ShroomSpore"),
                                 ("StanderDashAbility", "Movement.Character")):
            self.assertIn(strategy, by_native[native]["strategyTypeIds"])
        dodge = next(row for row in routes if row["assetPath"].endswith("/Bow/GA_Dodge.GA_Dodge"))
        self.assertEqual(dodge["strategyTypeIds"], ["Movement.Character"])
        self.assertEqual(dodge["strategyEvidenceScope"], "ConfiguredGameplayActions")
        self.assertTrue(dodge["actionConfigurationEvidence"])

    def test_unreviewed_native_override_cannot_inherit_composability(self):
        row = fixture("/Game/GA_Unknown.GA_Unknown", ["/Game/GA_Unknown.GA_Unknown_C",
                      "/Script/test_25d.NewEnemyOverride", "/Script/test_25d.TelegraphedEnemyAbility",
                      migration.MY_BASE, migration.audit.GA_BASE])
        with self.assertRaisesRegex(ValueError, "native override"):
            migration.classify(row)

    def test_reparent_restores_original_policies_without_weakening_snapshot_checks(self):
        from types import SimpleNamespace
        execution, predicted = object(), object()
        unreal = SimpleNamespace(GameplayAbilityInstancingPolicy=SimpleNamespace(INSTANCED_PER_EXECUTION=execution),
                                 GameplayAbilityNetExecutionPolicy=SimpleNamespace(LOCAL_PREDICTED=predicted))
        self.assertIs(migration.protected_policy_value(unreal, "instancing_policy",
                      "<GameplayAbilityInstancingPolicy.INSTANCED_PER_EXECUTION: 2>"), execution)
        self.assertIs(migration.protected_policy_value(unreal, "net_execution_policy",
                      "<GameplayAbilityNetExecutionPolicy.LOCAL_PREDICTED: 0>"), predicted)
        with self.assertRaisesRegex(ValueError, "Unexpected protected"):
            migration.protected_policy_value(unreal, "instancing_policy", "Unknown")

    def test_partial_apply_lineage_only_accepts_six_actual_saved_compiled_assets(self):
        records = [{"assetPath": path + "." + path.rsplit("/", 1)[-1], "newParentClass": parent,
                    "compiled": True, "saved": True, "before": {"graphsFingerprint": "original"},
                    "files": [{"sha256Before": "before", "sha256After": "after"}]}
                   for path, parent in sorted(migration.AUTHORIZED_PARENTS.items())]
        with tempfile.TemporaryDirectory(dir=ROOT / "Saved") as directory:
            self.assertTrue(Path(directory).resolve().is_relative_to((ROOT / "Saved").resolve()))
            first, second = Path(directory) / "partial.json", Path(directory) / "complete.json"
            partial = {"success": False, "dryRun": False, "processId": -1, "matrixSha256": "matrix",
                       "assetWrites": copy.deepcopy(records[:4]) + [dict(records[4], saved=False)]}
            final = {"success": True, "dryRun": False, "processId": -2, "matrixSha256": "matrix",
                     "assetWrites": copy.deepcopy(records[4:])}
            first.write_text(json.dumps(partial), encoding="utf-8")
            second.write_text(json.dumps(final), encoding="utf-8")
            self.assertEqual(len(migration.saved_write_lineage([first, second])), 6)
            with self.assertRaisesRegex(ValueError, "all six"):
                migration.saved_write_lineage([first])
            final["assetWrites"][0]["compiled"] = False
            second.write_text(json.dumps(final), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "uncompiled"):
                migration.saved_write_lineage([first, second])
            final["assetWrites"][0]["compiled"] = True
            final["assetWrites"][0]["assetPath"] = "/Game/Unapproved.GA"
            second.write_text(json.dumps(final), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "whitelist"):
                migration.saved_write_lineage([first, second])

    def test_orbit_controller_is_not_confused_with_projectile_action_parent(self):
        row = next(row for row in self.baseline if row["assetPath"] == "/Script/test_25d.MyOrbitingFireBoltAbility")
        route = migration.classify(row)
        self.assertEqual(route["route"], "Graph.OrbitingFireBolts")
        self.assertTrue(route["graphComposable"])
        self.assertEqual(route["strategyTypeIds"], ["Channel.OrbitingFireBolts"])

    def test_passives_and_selectors_do_not_claim_combat_strategy(self):
        for name, composition in (("MyPassiveAbility", "PassiveActivationListener"),
                                  ("TelegraphedSelectAttack", "AttackSelectionOnly")):
            row = next(row for row in self.baseline if row["assetPath"] == "/Script/test_25d." + name)
            route = migration.classify(row)
            self.assertEqual(route["actionComposition"], composition)
            self.assertFalse(route["graphComposable"])

    def test_only_three_literal_historical_utility_paths_are_pending(self):
        historical = [fixture(path + "." + path.rsplit("/", 1)[-1],
                              [path + "." + path.rsplit("/", 1)[-1] + "_C",
                               migration.audit.GA_BASE, "/Script/CoreUObject.Object"])
                      for path in sorted(migration.UTILITY_PACKAGES)]
        pending = {migration.package(row["assetPath"]) for row in historical
                   if migration.classify(row)["requiresUtilityReparent"]}
        self.assertEqual(pending, migration.UTILITY_PACKAGES)
        self.assertEqual(len(pending), 3)

    def test_opaque_offensive_blueprint_is_not_claimed_composable(self):
        shards = fixture(migration.ARCANE_PACKAGE + ".GA_ArcaneShards",
                         [migration.ARCANE_PACKAGE + ".GA_ArcaneShards_C", "/Script/test_25d.MyDamageGameplayAbility",
                          migration.MY_BASE, migration.audit.GA_BASE, "/Script/CoreUObject.Object"])
        result = migration.classify(shards)
        self.assertEqual(result["route"], "Session.BlueprintUtility")
        self.assertEqual(result["actionComposition"], "UnsupportedOpaqueBlueprint")
        self.assertFalse(result["graphComposable"])
        self.assertTrue(result["requiresArcaneReparent"])

    def test_current_migrated_utility_and_arcane_paths_are_no_longer_pending(self):
        expected = {migration.HIT_REACT_PACKAGE: ("Action.HitReact", "Status.OwnedEffect"),
                    migration.ATTRIBUTE_EVENT_PACKAGE: ("Action.AttributeEvents", "Effect.GameplayEffect"),
                    migration.XP_EVENT_PACKAGE: ("Action.AttributeEvents", "Effect.GameplayEffect"),
                    migration.ARCANE_PACKAGE: ("Graph.ArcaneShards", "Shape.ArcaneShards")}
        for package, (route_name, strategy) in expected.items():
            row = next(row for row in self.baseline if migration.package(row["assetPath"]) == package)
            result = migration.classify(row)
            self.assertEqual(result["route"], route_name)
            self.assertEqual(result["strategyTypeIds"], [strategy])
            self.assertTrue(result["graphComposable"])
            self.assertFalse(result["requiresUtilityReparent"])
            self.assertFalse(result["requiresArcaneReparent"])

    def test_reviewed_current_counts_are_checked_without_historical_ceiling(self):
        counts = migration.reviewed_inventory_counts(self.baseline, self.reviewed_counts)
        wrong = dict(counts)
        wrong["NativeGameplayAbility"] -= 1
        with self.assertRaisesRegex(ValueError, "counts do not match"):
            migration.reviewed_inventory_counts(self.baseline, wrong)
        # A fully observed new asset using a reviewed native implementation is legitimate.
        new = fixture("/Game/ReviewedNew.ReviewedNew", ["/Game/ReviewedNew.ReviewedNew_C",
                      "/Script/test_25d.MyPassiveAbility", migration.MY_BASE, migration.audit.GA_BASE])
        observed = self.baseline + [new]
        routes = [migration.classify(row) for row in observed]
        returned = migration.assert_coverage(self.baseline, observed, routes,
                                             {row["assetPath"] for row in observed}, self.reviewed_counts)
        self.assertEqual(returned, counts)

    def test_historical_declared_count_format_checks_kinds_rows_and_families(self):
        counts = collections.Counter(row["evidence"]["audit"]["kind"] for row in self.baseline)
        historical_format = {"rows": len(self.baseline),
                             "gameplayAbilityBlueprints": counts["GameplayAbilityBlueprint"],
                             "nativeGameplayAbilities": counts["NativeGameplayAbility"],
                             "configurations": counts["Configuration"], "excludedTestNativeClasses": 5,
                             "byFamily": {family: sum(row["family"] == family for row in self.baseline)
                                          for family in sorted(migration.audit.FAMILIES)}}
        routes = [migration.classify(row) for row in self.baseline]
        self.assertEqual(migration.assert_coverage(self.baseline, self.baseline, routes,
                                                  {row["assetPath"] for row in self.baseline}, historical_format), dict(counts))
        for field in ("rows", "gameplayAbilityBlueprints", "nativeGameplayAbilities", "configurations"):
            wrong = copy.deepcopy(historical_format)
            wrong[field] -= 1
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "do not match"):
                migration.reviewed_inventory_counts(self.baseline, wrong)
        wrong_family = copy.deepcopy(historical_format)
        first_family = next(iter(wrong_family["byFamily"]))
        wrong_family["byFamily"][first_family] += 1
        with self.assertRaisesRegex(ValueError, "byFamily"):
            migration.reviewed_inventory_counts(self.baseline, wrong_family)
        for wrong in ({"rows": len(self.baseline)}, dict(counts, unexpected=1),
                      dict(historical_format, unexpected=1)):
            with self.assertRaisesRegex(ValueError, "Unknown reviewed matrix counts format"):
                migration.reviewed_inventory_counts(self.baseline, wrong)

    def test_baseline_and_route_kind_changes_remain_rejected(self):
        index = next(index for index, row in enumerate(self.baseline)
                     if row["evidence"]["audit"]["kind"] == "GameplayAbilityBlueprint")
        observed = copy.deepcopy(self.baseline)
        observed[index]["evidence"]["audit"]["kind"] = "Configuration"
        routes = [migration.classify(row) for row in observed]
        with self.assertRaisesRegex(ValueError, "Inventory kind changed"):
            migration.assert_coverage(self.baseline, observed, routes, {row["assetPath"] for row in observed})
        routes = [migration.classify(row) for row in self.baseline]
        routes[index]["kind"] = "Configuration"
        with self.assertRaisesRegex(ValueError, "route kind differs"):
            migration.assert_coverage(self.baseline, self.baseline, routes, {row["assetPath"] for row in self.baseline})
        unknown = copy.deepcopy(self.baseline)
        unknown[0]["evidence"]["audit"]["kind"] = "UnknownKind"
        with self.assertRaisesRegex(ValueError, "Unknown inventory kind"):
            migration.reviewed_inventory_counts(unknown)

    def test_only_authorized_arcane_and_utilities_can_change_parent(self):
        self.assertEqual(set(migration.AUTHORIZED_PARENTS), migration.UTILITY_PACKAGES |
                         {migration.ARCANE_PACKAGE, migration.BEAM_PACKAGE, migration.SUMMON_PACKAGE})
        self.assertEqual(migration.AUTHORIZED_PARENTS[migration.ARCANE_PACKAGE], migration.ARCANE_PARENT)
        row = fixture(migration.ARCANE_PACKAGE + ".GA_ArcaneShards", ["/Game/Arcane_C", migration.ARCANE_PARENT,
                      migration.MY_BASE, migration.audit.GA_BASE])
        self.assertEqual(migration.classify(row)["route"], "Graph.ArcaneShards")
        self.assertFalse(migration.classify(row)["requiresArcaneReparent"])

    def test_arcane_defaults_come_from_actual_graph_and_original_cdo(self):
        description = {"graphs": [{"nodes": [{"pins": [
            {"name": "Class", "defaultObject": migration.ARCANE_POINT_CLASS},
            {"name": "MontageToPlay", "defaultObject": migration.ARCANE_MONTAGE},
        ]}]}]}
        values = {"Num Points": 9, "ShardSpawnDeltaTime": .3}
        result = migration.arcane_settings(description, values)
        self.assertEqual(result["max_shard_points"], 9)
        self.assertEqual(result["shard_interval"], migration.float32(.3))
        self.assertEqual(result["shard_montage"], migration.ARCANE_MONTAGE)
        self.assertEqual(result["shard_point_collection_class"], migration.ARCANE_POINT_CLASS)
        self.assertEqual(values, {"Num Points": 9, "ShardSpawnDeltaTime": .3})
        changed = copy.deepcopy(description)
        changed["graphs"][0]["nodes"][0]["pins"][0]["defaultObject"] = "/Game/Other.Other_C"
        with self.assertRaisesRegex(ValueError, "binding differs"):
            migration.arcane_settings(changed, values)
        for invalid in ({"Num Points": 12, "ShardSpawnDeltaTime": .2},
                        {"Num Points": 11, "ShardSpawnDeltaTime": float("nan")},
                        {"Num Points": 11, "ShardSpawnDeltaTime": .1}):
            with self.assertRaises(ValueError):
                migration.arcane_settings(description, invalid)

    def test_beam_copies_original_defaults_with_native_float_precision(self):
        original = {"Montage_Electrocute": migration.BEAM_MONTAGE, "DamageDeltaTime": .1, "MiniSpellTIme": 1.}
        result = migration.native_settings(migration.BEAM_PACKAGE, {}, original)
        self.assertEqual(result, {"electrocute_montage": migration.BEAM_MONTAGE,
                                 "electrocute_damage_interval": migration.float32(.1), "minimum_electrocute_time": 1.})
        original["Montage_Electrocute"] = "/Game/Other.Other"
        with self.assertRaisesRegex(ValueError, "montage differs"):
            migration.native_settings(migration.BEAM_PACKAGE, {}, original)

    def test_summon_extracts_only_reviewed_original_pin_values(self):
        description = {"graphs": [{"nodes": [{"references": {"FunctionReference": 'MemberName="Add_VectorVector"'},
            "pins": [{"name": "MontageToPlay", "defaultObject": migration.SUMMON_MONTAGE},
                     {"name": "SystemTemplate", "defaultObject": migration.SUMMON_EFFECT},
                     {"name": "EventTag", "defaultValue": '(TagName="Montage.Attack.2")'},
                     {"name": "Duration", "defaultValue": "0.2"},
                     {"name": "B", "defaultValue": "0.000000,0.000000,60.000000"}]}]}]}
        result = migration.native_settings(migration.SUMMON_PACKAGE, description, {})
        self.assertEqual(result["summon_spawn_interval"], migration.float32(.2))
        self.assertEqual(result["summon_spawn_height"], 60.)
        description["graphs"][0]["nodes"][0]["pins"][-1]["defaultValue"] = "0,0,100"
        with self.assertRaisesRegex(ValueError, "differs"):
            migration.native_settings(migration.SUMMON_PACKAGE, description, {})

    def test_unrecognized_direct_engine_blueprint_is_rejected(self):
        row = fixture("/Game/GA_New.GA_New", ["/Game/GA_New.GA_New_C", migration.audit.GA_BASE])
        with self.assertRaisesRegex(ValueError, "Unclassified"):
            migration.classify(row)

    def test_new_native_class_is_not_confused_with_lifecycle_base(self):
        row = fixture("/Script/test_25d.NewNative", ["/Script/test_25d.NewNative", migration.MY_BASE,
                      migration.audit.GA_BASE], "NativeGameplayAbility")
        with self.assertRaisesRegex(ValueError, "Unclassified"):
            migration.classify(row)

    def test_unknown_inventory_row_is_rejected(self):
        row = copy.deepcopy(self.baseline[0])
        row["family"] = "Unknown"
        with self.assertRaisesRegex(ValueError, "Unresolved"):
            migration.classify(row)

    def test_missing_native_and_configuration_are_detected(self):
        for kind in ("NativeGameplayAbility", "Configuration", "GameplayAbilityBlueprint"):
            omitted = next(row for row in self.baseline if row["evidence"]["audit"]["kind"] == kind)
            observed = [row for row in self.baseline if row is not omitted]
            with self.assertRaisesRegex(ValueError, "disappeared"):
                migration.assert_coverage(self.baseline, observed, [migration.classify(row) for row in observed],
                                          {row["assetPath"] for row in observed})

    def test_newly_discovered_row_cannot_be_omitted_from_routes(self):
        new = fixture("/Game/New.New", ["/Game/New.New_C", migration.MY_BASE, migration.audit.GA_BASE])
        observed = self.baseline + [new]
        with self.assertRaisesRegex(ValueError, "coverage differ"):
            migration.assert_coverage(self.baseline, observed, [migration.classify(row) for row in self.baseline],
                                      {row["assetPath"] for row in observed})

    def test_duplicate_route_is_rejected(self):
        classified = [migration.classify(row) for row in self.baseline]
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            migration.assert_coverage(self.baseline, self.baseline, classified + classified[:1],
                                      {row["assetPath"] for row in self.baseline})

    def test_compilation_profile_pairs_are_unique_and_configurations_are_covered(self):
        classified = [migration.classify(row) for row in self.baseline]
        requests = migration.compilation_requests(self.baseline, classified)
        keys = [(request["assetPath"], request["profile"]) for request in requests]
        self.assertEqual(len(keys), len(set(keys)))
        prototype_paths = {row["assetPath"] for row in self.baseline
                           if row["evidence"]["audit"]["kind"] == "Configuration" and
                           "/Script/test_25d.AbilityPrototype" in row["evidence"]["audit"]["ancestry"]}
        self.assertEqual(prototype_paths, {path for path, _ in keys})
        self.assertTrue(all(request["requestedBy"] for request in requests))

    def test_compile_errors_and_stale_bindings_are_rejected(self):
        graph = object()
        self.assertIs(migration.normalize_compile_result((graph, [])), graph)
        for result in ((None, []), (graph, ["Invalid configuration"]), graph, (graph,)):
            with self.assertRaises(ValueError):
                migration.normalize_compile_result(result)

    def test_paths_are_contained_in_project_content(self):
        valid = migration.content_file(ROOT, "/Game/A/B.B")
        self.assertEqual(valid, (ROOT / "Content/A/B.uasset").resolve())
        for invalid in ("/Game/../Config/Secret.Secret", "/Script/test_25d.MyGameplayAbility", "C:/Other/A.A"):
            with self.assertRaises(ValueError):
                migration.content_file(ROOT, invalid)

    def test_fingerprint_changes_for_semantic_changes(self):
        self.assertEqual(migration.digest({"a": 1, "b": 2}), migration.digest({"b": 2, "a": 1}))
        self.assertNotEqual(migration.digest({"damage": 1}), migration.digest({"damage": 2}))

    def test_actual_print_delay_blueprint_requires_no_gameplay_action(self):
        description = {"schemaVersion": 1, "success": True, "supported": True, "variables": [], "graphs": [{"nodes": [
            {"class": "/Script/BlueprintGraph.K2Node_Event", "references": {"EventReference": 'MemberName="K2_ActivateAbility"'}},
            {"class": "/Script/BlueprintGraph.K2Node_CallFunction", "references": {"FunctionReference": 'MemberName="PrintString"'}},
            {"class": "/Script/BlueprintGraph.K2Node_CallFunction", "references": {"FunctionReference": 'MemberName="Delay"'}},
            {"class": "/Script/BlueprintGraph.K2Node_CallFunction", "references": {"FunctionReference": 'MemberName="K2_EndAbility"'}},
        ]}]}
        for index, node in enumerate(description["graphs"][0]["nodes"]):
            node.update({"guid": str(index), "pins": []})
        inspection = migration.blueprint_semantics(description)
        self.assertTrue(inspection["noGameplayActionRequired"])
        row = fixture("/Game/Utility.Utility", ["/Game/Utility.Utility_C", migration.MY_BASE, migration.audit.GA_BASE])
        row["evidence"]["audit"]["blueprintInspection"] = inspection
        self.assertEqual(migration.classify(row)["actionComposition"], "NoGameplayActionRequired")
        description["graphs"][0]["nodes"].append({"class": "/Script/BlueprintGraph.K2Node_CallFunction",
                                                  "guid": "damage", "pins": [],
                                                  "references": {"FunctionReference": 'MemberName="ApplyDamageEffect"'}})
        self.assertFalse(migration.blueprint_semantics(description)["noGameplayActionRequired"])

    def test_utility_migration_uses_reachable_listener_and_real_cdo(self):
        def pin(name, value="", links=None):
            return {"name": name, "direction": "output", "type": {"category": "exec" if links else "value"},
                    "defaultValue": value, "links": links or []}
        active = {"guid": "active", "references": {"ProxyFactoryFunctionName": "WaitGameplayEvent"},
                  "pins": [pin("EventTag", '(TagName="Attributes")'), pin("OnlyMatchExact", "false"),
                           pin("OnlyTriggerOnce", "false")]}
        disconnected = copy.deepcopy(active)
        disconnected["guid"] = "dead"
        disconnected["pins"][0]["defaultValue"] = '(TagName="Attributes.Meta.IncomingXP")'
        description = {"graphs": [{"name": "EventGraph", "nodes": [
            {"guid": "activate", "references": {"EventReference": 'MemberName="K2_ActivateAbility"'},
             "pins": [pin("then", links=[{"nodeGuid": "active"}])]}, active, disconnected]},
            {"name": "NewFunction", "nodes": [{"guid": "make", "references": {"FunctionReference": 'MemberName="MakeOutgoingSpec"'},
                "pins": [pin("Level", "1.000000")]}]}]}
        properties = {"EventBasedEffectClass": "/Game/Original.Original_C", "EventTags": ['(TagName="Attributes.Primary.Strength")']}
        settings = migration.utility_settings(migration.ATTRIBUTE_EVENT_PACKAGE, description, properties)
        self.assertEqual(settings["listen_tag"], '(TagName="Attributes")')
        self.assertEqual(settings["event_effect_class"], "/Game/Original.Original_C")
        self.assertEqual(settings["initialized_event_tags"], properties["EventTags"])
        self.assertEqual(settings["event_effect_level"], 1.)
        self.assertEqual(migration.AUTHORIZED_PARENTS[migration.ATTRIBUTE_EVENT_PACKAGE], "/Script/test_25d.MyAttributeEventAbility")
        description["graphs"][0]["nodes"][0]["pins"][0]["links"].append({"nodeGuid": "dead"})
        with self.assertRaisesRegex(ValueError, "exactly one reachable"):
            migration.utility_settings(migration.ATTRIBUTE_EVENT_PACKAGE, description, properties)


if __name__ == "__main__":
    unittest.main()
