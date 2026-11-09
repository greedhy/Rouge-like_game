"""Standalone evidence-gate tests; no Unreal process and no production asset writes."""
import copy
import datetime as dt
import json
from pathlib import Path
import tempfile
import unittest

import finalize_migration_matrix as finalizer
import migrate_catalog as migration

ROOT = Path(__file__).resolve().parents[3]


def fixture_run(directory, report=None):
    """Create real hashed bytes and a synthetic run entirely under the test's Saved directory."""
    root = Path(directory)
    for relative in ("Source/Ability.cpp", "Source/Ability.h", "Source/Module.Build.cs", "Config/Default.ini",
                     "Binaries/Win64/UnrealEditor-Fixture.dll", "Content/Fixture/GA_Test.uasset", "Content/Fixture/GA_Test.uexp"):
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("original fixture bytes", encoding="utf-8")
    (root / "test_25d.uproject").write_text(json.dumps({"Modules": [{"Name": "Fixture"}]}), encoding="utf-8")
    matrix = root / "docs/ga-unified-migration-matrix.json"
    matrix.parent.mkdir(parents=True, exist_ok=True)
    matrix.write_text(json.dumps({"rows": [{"assetPath": "/Game/Fixture/GA_Test.GA_Test"},
                                          {"assetPath": "/Script/test_25d.Fixture"}]}), encoding="utf-8")
    run_id = "20260101T000000-12345678"
    path = root / "Saved/Automation/UnifiedGA" / run_id / "index.json"
    validation_path = root / "Saved/GAAuthoring/UnifiedGA" / run_id / "validation.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    validation_path.parent.mkdir(parents=True, exist_ok=True)
    report = report if report is not None else {"failed": 0, "notRun": 0, "inProcess": 0,
                                               "tests": [{"fullTestPath": "Fixture.Known", "state": "Success"}]}
    path.write_text(json.dumps(report), encoding="utf-8")
    current = finalizer.current_input_manifest(root)
    now = dt.datetime.now(dt.timezone.utc)
    stamp = lambda delta: (now + dt.timedelta(seconds=delta)).isoformat()
    validation = {"schemaVersion": 2, "runId": run_id, "success": True, "exitCode": 0,
                  "reportDirectory": str(path.parent), "reportFile": str(path), "reportSha256": migration.file_digest(path),
                  "startedUtc": stamp(-1), "finishedUtc": stamp(2), "expectedTests": ["Fixture.Known"],
                  "executedTests": [case["fullTestPath"] for case in report["tests"]],
                  "inputManifest": {"schemaVersion": 1, "runId": run_id, "scope": finalizer.INPUT_SCOPE, "unchanged": True,
                                    "before": dict(copy.deepcopy(current), capturedUtc=stamp(-2)),
                                    "after": dict(copy.deepcopy(current), capturedUtc=stamp(1))}}
    validation_path.write_text(json.dumps(validation), encoding="utf-8")
    return root, path, validation_path, validation


class FinalizerTests(unittest.TestCase):
    def test_final_review_rows_obey_existing_matrix_schema(self):
        row = migration.audit.new_row("/Game/GA_Reviewed.GA_Reviewed", "ReviewedClass", "NativeParent", "Traditional", {})
        route = {"route": "Graph.Melee", "strategyTypeIds": ["Melee.Contact"], "configPaths": [],
                 "routeSourceFingerprints": {"Source/Reviewed.cpp": "checked fingerprint"}}
        targets, lifetime = finalizer.reviewed_contract(route, row, {}, [])
        finalizer.apply_reviewed_matrix_contract(row, route, targets, lifetime, ["Reviewed.ActualCase"],
                                                 {"automation": ["Saved/actual-run/index.json"]})
        row["migrationStatus"] = "Verified"
        migration.audit.validate([row], {row["assetPath"]}, {row["assetPath"]})
        self.assertIsNone(row["replacementPath"])
        self.assertEqual(row["targetSupport"], ["Actor"])
        self.assertNotIn("NeedsReview", row["lifecycle"])
        self.assertEqual(row["evidence"]["manual"][0]["tests"], ["Reviewed.ActualCase"])
        self.assertEqual(row["evidence"]["manual"][0]["pieVisualAcceptance"], "Pending")
        finalizer.apply_reviewed_matrix_contract(row, route, targets, lifetime, ["Reviewed.ActualCase"],
                                                 {"automation": ["Saved/actual-run/index.json"]})
        self.assertEqual(len(row["evidence"]["manual"]), 1)
        row["replacementPath"] = "Graph.Melee"
        with self.assertRaisesRegex(ValueError, "Invalid replacement"):
            migration.audit.validate([row], {row["assetPath"]}, {row["assetPath"]})

    def test_legacy_frontend_mass_support_is_not_inferred_from_optional_node_modes(self):
        for route in ("Graph.Melee", "Graph.ArcaneNova", "Graph.ArcaneShards", "Action.SwordShieldStrike"):
            targets, _ = finalizer.reviewed_contract({"route": route}, {"evidence": {"audit": {}}}, {}, [])
            self.assertEqual(targets, ["Actor"])
        self.assertEqual(finalizer.graph_targets([{"type_id": "Shape.ArcaneNova", "config": {"include_mass": True}}]),
                         ["Actor", "Mass"])
        self.assertEqual(finalizer.graph_targets([{"type_id": "Shape.ArcaneNova"}]), ["Actor"])

    def test_configuration_profile_and_action_roles_are_explicit(self):
        row = {"assetPath": "/Game/DA_Config.DA_Config", "generatedClass": "/Script/test_25d.AbilityPrototype",
               "evidence": {"audit": {"properties": {}}}}
        route = {"assetPath": row["assetPath"], "kind": "Configuration", "route": "Configuration.referenced"}
        compiled = [{"assetPath": row["assetPath"], "profile": "LegacyActor.v1",
                     "definition": {"nodes": [{"sourceAsset": row["assetPath"], "typeId": "Legacy.Actor.Projectile"}]}}]
        targets, lifetime = finalizer.reviewed_contract(route, row, {}, compiled)
        self.assertEqual(targets, ["Actor"])
        self.assertIn("ConfigurationOnly", lifetime)
        self.assertIn("LegacyActor.v1", lifetime)
        with self.assertRaisesRegex(ValueError, "actual compiled target profile"):
            finalizer.reviewed_contract(route, row, {}, [])

    def test_explicit_behavior_mappings_exist_in_sources(self):
        declared = finalizer.discover_cases(ROOT)
        expected = {finalizer.TEST_PREFIX + suffix for names in finalizer.STRATEGY_CASES.values() for suffix in names}
        expected |= {name for names in finalizer.CONFIG_CASES.values() for name in names}
        self.assertEqual(expected - set(declared), set())

    def test_unknown_strategy_does_not_inherit_any_suite_pass(self):
        route = {"kind": "GameplayAbilityBlueprint", "route": "Action.New", "strategyTypeIds": ["Unknown.New"]}
        names, gaps = finalizer.expected_cases(route, {}, {})
        self.assertFalse(names)
        self.assertTrue(any("Unknown.New" in issue for issue in gaps))

    def test_each_different_executor_receives_its_own_test_evidence(self):
        for strategy in ("Move.GruxCharge", "Field.ShroomSpore", "Spawn.ShamanMinions", "Effect.GameplayEffect"):
            route = {"kind": "GameplayAbilityBlueprint", "route": "Action.Reviewed", "strategyTypeIds": [strategy]}
            names, gaps = finalizer.expected_cases(route, {}, {})
            self.assertTrue(names)
            self.assertFalse(gaps)
            self.assertEqual(names, sorted(finalizer.TEST_PREFIX + suffix for suffix in finalizer.STRATEGY_CASES[strategy]))

    def test_failed_empty_partial_and_duplicate_automation_are_rejected(self):
        good = {"failed": 0, "notRun": 0, "inProcess": 0, "tests": [{"fullTestPath": "Reviewed.Case", "state": "Success"}]}
        with tempfile.TemporaryDirectory(dir=ROOT / "Saved") as directory:
            self.assertTrue(Path(directory).resolve().is_relative_to((ROOT / "Saved").resolve()))
            for mutate in (lambda data: data.update(failed=1), lambda data: data.update(tests=[]),
                           lambda data: data["tests"].append(copy.deepcopy(data["tests"][0])),
                           lambda data: data["tests"][0].update(state="NotRun")):
                bad = copy.deepcopy(good)
                mutate(bad)
                root, path, _, _ = fixture_run(directory, bad)
                with self.assertRaises(ValueError):
                    finalizer.load_results([path], {}, root)
            root, path, _, _ = fixture_run(directory)
            self.assertEqual(set(finalizer.load_results([path], {}, root)), {"Fixture.Known"})

    def test_test_source_newer_than_result_is_rejected(self):
        import os
        with tempfile.TemporaryDirectory(dir=ROOT / "Saved") as directory:
            self.assertTrue(Path(directory).resolve().is_relative_to((ROOT / "Saved").resolve()))
            root, path, _, _ = fixture_run(directory)
            source = root / "Source/Ability.cpp"
            os.utime(source, (path.stat().st_mtime + 1, path.stat().st_mtime + 1))
            with self.assertRaisesRegex(ValueError, "changed after"):
                finalizer.load_results([path], {"Fixture.Known": source}, root)

    def test_old_report_without_actual_run_manifest_is_rejected(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "Saved") as directory:
            root, path, validation_path, _ = fixture_run(directory)
            validation_path.unlink()
            with self.assertRaisesRegex(ValueError, "no input validation manifest"):
                finalizer.load_results([path], {}, root)

    def test_each_real_input_change_rejects_old_green_report(self):
        for relative in ("Source/Ability.cpp", "Source/Ability.h", "Source/Module.Build.cs", "Config/Default.ini",
                         "Binaries/Win64/UnrealEditor-Fixture.dll", "Content/Fixture/GA_Test.uasset", "Content/Fixture/GA_Test.uexp",
                         "test_25d.uproject", "Source/NewClass.h"):
            with self.subTest(path=relative), tempfile.TemporaryDirectory(dir=ROOT / "Saved") as directory:
                root, path, _, _ = fixture_run(directory)
                (root / relative).write_text("actually changed bytes", encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "input fingerprints or membership"):
                    finalizer.load_results([path], {}, root)

    def test_missing_input_even_with_matching_pre_post_snapshots_is_rejected(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "Saved") as directory:
            root, path, validation_path, validation = fixture_run(directory)
            for phase in ("before", "after"):
                del validation["inputManifest"][phase]["files"]["Source/Ability.h"]
            validation_path.write_text(json.dumps(validation), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "input fingerprints or membership"):
                finalizer.load_results([path], {}, root)

    def test_matrix_metadata_update_preserves_the_same_inputs(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "Saved") as directory:
            root, path, _, _ = fixture_run(directory)
            matrix = root / "docs/ga-unified-migration-matrix.json"
            data = finalizer.read(matrix)
            data["updatedAtUtc"] = "candidate publication metadata"
            matrix.write_text(json.dumps(data), encoding="utf-8")
            self.assertIn("Fixture.Known", finalizer.load_results([path], {}, root))

    def test_run_report_link_and_no_run_timestamps_are_checked(self):
        mutations = (lambda value: value.update(runId="another-run"),
                     lambda value: value.update(reportSha256="0" * 64),
                     lambda value: value["inputManifest"].update(unchanged=False),
                     lambda value: value["inputManifest"]["before"].update(capturedUtc="2999-01-01T00:00:00Z"),
                     lambda value: value.update(executedTests=[]))
        for mutate in mutations:
            with tempfile.TemporaryDirectory(dir=ROOT / "Saved") as directory:
                root, path, validation_path, validation = fixture_run(directory)
                mutate(validation)
                validation_path.write_text(json.dumps(validation), encoding="utf-8")
                with self.assertRaises(ValueError):
                    finalizer.load_results([path], {}, root)

    def test_six_saved_writes_require_original_reports_and_backup_fingerprints(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "Saved") as directory:
            root = Path(directory)
            changes = []
            for index, (package, parent) in enumerate(migration.AUTHORIZED_PARENTS.items()):
                asset = package + "." + package.rsplit("/", 1)[-1]
                source = migration.content_file(root, asset)
                backup = root / "Saved/backups" / (str(index) + ".uasset")
                source.parent.mkdir(parents=True, exist_ok=True)
                backup.parent.mkdir(parents=True, exist_ok=True)
                source.write_text("migrated " + asset, encoding="utf-8")
                backup.write_text("original " + asset, encoding="utf-8")
                changes.append({"assetPath": asset, "newParentClass": parent, "saved": True, "compiled": True,
                                "before": {"properties": {"cost": "retained"}, "graphsFingerprint": "original graph"},
                                "migratedProperties": {"native": "mapped"},
                                "files": [{"source": str(source), "backup": str(backup),
                                           "sha256Before": migration.file_digest(backup), "sha256After": migration.file_digest(source)}]})
            apply_path = root / "Saved/apply.json"
            apply_path.write_text(json.dumps({"success": True, "dryRun": False, "processId": 101,
                                              "assetWrites": changes}), encoding="utf-8")
            lineage = [{"assetPath": change["assetPath"], "applyReport": str(apply_path),
                        "applyReportSha256": migration.file_digest(apply_path), "applyProcessId": 101,
                        "originalSnapshot": change["before"], "migratedProperties": change["migratedProperties"],
                        "newParentClass": change["newParentClass"], "files": change["files"],
                        "saved": True, "compiled": True, "independentlyVerified": True} for change in changes]
            catalog = {"processId": 202, "verifiedWriteLineage": lineage}
            self.assertEqual(len(finalizer.validate_write_lineage(root, catalog)), 6)
            partial = finalizer.read(apply_path)
            partial["success"] = False  # Later batch failure does not erase earlier real saved writes.
            apply_path.write_text(json.dumps(partial), encoding="utf-8")
            for entry in lineage:
                entry["applyReportSha256"] = migration.file_digest(apply_path)
            self.assertEqual(len(finalizer.validate_write_lineage(root, catalog)), 6)
            missing = copy.deepcopy(catalog)
            missing["verifiedWriteLineage"].pop()
            with self.assertRaisesRegex(ValueError, "six authorized"):
                finalizer.validate_write_lineage(root, missing)
            wrong_before = copy.deepcopy(catalog)
            wrong_before["verifiedWriteLineage"][0]["originalSnapshot"]["properties"]["cost"] = "changed"
            with self.assertRaisesRegex(ValueError, "differs from its original APPLY"):
                finalizer.validate_write_lineage(root, wrong_before)
            Path(lineage[0]["files"][0]["backup"]).write_text("changed backup", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "backup or saved package changed"):
                finalizer.validate_write_lineage(root, catalog)


if __name__ == "__main__":
    unittest.main()
