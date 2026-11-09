"""Read-only, separate-process diagnostics for a partial APPLY report; never saves or reloads packages."""
import datetime as dt
import json
import os
from pathlib import Path
import sys
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parent))
import migrate_catalog as migration


def run():
    import unreal as u
    source = Path(os.environ["UNIFIED_GA_MIGRATION_INSPECT_REPORT"]).resolve()
    previous = json.loads(source.read_text(encoding="utf-8-sig"))
    if previous["processId"] == os.getpid():
        raise ValueError("Inspection must use a separate process")
    root = Path(u.Paths.project_dir()).resolve()
    out = root / "Saved/GAAuthoring/UnifiedGA/CatalogReadback" / (
        dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8])
    out.mkdir(parents=True, exist_ok=False)
    report = {"sourceReport": str(source), "processId": os.getpid(), "readOnly": True, "rows": [], "success": False}
    for change in previous["assetWrites"]:
        path = change["assetPath"]
        if migration.package(path) not in migration.AUTHORIZED_PARENTS:
            raise ValueError("Prior asset outside authorized list: " + path)
        row = {"assetPath": path, "wasSaved": change["saved"], "errors": []}
        report["rows"].append(row)
        try:
            bp = u.load_asset(path)
            actual = migration.utility_snapshot(u, bp)
            row["expectedSnapshot"] = change["before"]
            row["actualSnapshot"] = actual
            row["protectedChanges"] = {name: {"before": value, "after": actual["properties"].get(name)}
                                       for name, value in change["before"]["properties"].items()
                                       if actual["properties"].get(name) != value}
            row["graphsMatch"] = actual["graphsFingerprint"] == change["before"]["graphsFingerprint"]
            row["actualPackageSha256"] = migration.file_digest(migration.content_file(root, path))
            description = json.loads(u.AbilityBlueprintAuditLibrary.describe_blueprint(bp))
            audit_path = out / (migration.package(path).rsplit("/", 1)[-1] + "-fresh-graph.json")
            migration.audit.write_json(audit_path, description)
            row["freshGraphAudit"] = str(audit_path)
            if change["saved"]:
                row["actualNativeProperties"] = migration.migrated_snapshot(u, bp, change.get("migratedProperties", {}))
                migration.assert_saved_change(u, bp, change)
            elif actual != change["before"]:
                raise ValueError("Unsaved package differs from its protected before snapshot")
        except Exception as error:
            row["errors"].append(str(error))
    report["success"] = all(not row["errors"] for row in report["rows"])
    destination = out / "readback-report.json"
    migration.audit.write_json(destination, report)
    u.log("Unified GA independent readback diagnostics: " + str(destination))
    if not report["success"]:
        raise RuntimeError("Readback differs; inspect " + str(destination))


if __name__ == "__main__":
    run()
