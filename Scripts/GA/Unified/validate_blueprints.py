"""Compile every freshly discovered GA Blueprint in memory; never save assets.

Run in a dedicated UE Python commandlet after migration/readback. A fresh report
records exact package digests, semantic graph digests, compile status and errors.
This validates generated classes, not animation appearance or network play.
"""
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import sys
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parent))
import audit_blueprint_graphs as graphs
from migrate_catalog import content_file


def run():
    import unreal as u
    root = Path(u.Paths.project_dir()).resolve()
    output = root / "Saved/GAAuthoring/UnifiedGA/BlueprintValidation" / (
        dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8])
    paths = graphs.discover(u)
    if not paths:
        raise RuntimeError("No GameplayAbility Blueprints discovered")
    report = {"startedUtc": dt.datetime.now(dt.timezone.utc).isoformat(), "processId": os.getpid(),
              "saveAssets": False, "success": False, "rows": [], "errors": []}
    for path in sorted(set(paths)):
        row = {"assetPath": path}
        report["rows"].append(row)
        try:
            bp = u.load_asset(path)
            if not isinstance(bp, u.Blueprint):
                raise ValueError("Discovered GA is not a Blueprint")
            disk = content_file(root, path)
            before_bytes = hashlib.sha256(disk.read_bytes()).hexdigest()
            before = json.loads(u.AbilityBlueprintAuditLibrary.describe_blueprint(bp))
            row["beforeGraphDigest"] = graphs.graph_digest(before)
            u.BlueprintEditorLibrary.compile_blueprint(bp)
            row["status"] = str(bp.get_editor_property("status"))
            if "UP_TO_DATE" not in row["status"].upper():
                raise ValueError("Blueprint compile failed: " + row["status"])
            after = json.loads(u.AbilityBlueprintAuditLibrary.describe_blueprint(bp))
            row["afterGraphDigest"] = graphs.graph_digest(after)
            row["graphUnchanged"] = row["beforeGraphDigest"] == row["afterGraphDigest"]
            row["packageSha256"] = hashlib.sha256(disk.read_bytes()).hexdigest()
            if row["packageSha256"] != before_bytes:
                raise ValueError("Validation unexpectedly changed the disk package")
            if not row["graphUnchanged"]:
                graphs.write_json(output / (bp.get_name() + "-before.json"), before)
                graphs.write_json(output / (bp.get_name() + "-after.json"), after)
                raise ValueError("Compilation changed serialized graph semantics; inspect snapshots")
            row["success"] = True
        except Exception as error:
            row["success"] = False
            report["errors"].append({"assetPath": path, "error": str(error)})
        graphs.write_json(output / "index.json", report)
    report["success"] = not report["errors"] and len(report["rows"]) == len(set(paths))
    report["completedUtc"] = dt.datetime.now(dt.timezone.utc).isoformat()
    graphs.write_json(output / "index.json", report)
    u.log("Unified GA Blueprint validation: " + str(output / "index.json"))
    if not report["success"]:
        raise RuntimeError("GA Blueprint compilation validation failed; inspect " + str(output / "index.json"))


if __name__ == "__main__":
    run()
