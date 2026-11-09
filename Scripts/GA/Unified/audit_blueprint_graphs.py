"""Read-only commandlet audit of actual Blueprint graphs through the native inspector.

Default: discover every project GameplayAbility Blueprint and unresolved GA_* candidate.
For a focused run, set GA_BLUEPRINT_AUDIT_ASSETS to a JSON array of asset paths, or
GA_BLUEPRINT_AUDIT_REQUEST to a JSON file with {"assets": [...], "outputDir": "Saved/..."}.
Only JSON reports beneath the project's Saved directory are written. No compile,
reparent, Blueprint edit, package save, map change or PIE operation is performed.
"""
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import sys


def object_path(value):
    if hasattr(value, "package_name") and hasattr(value, "asset_name"):
        return str(value.package_name) + "." + str(value.asset_name)
    result = str(value or "")
    if "'" in result:
        result = result.split("'", 1)[1].rsplit("'", 1)[0]
    return "" if result in ("None", "null") else result


def graph_digest(description):
    """Hash graph semantics, retaining raw audits separately for byte-level diagnosis.

    UE reconstructs transient pin IDs and reorders dynamic pins on load/compile.
    Actual identity is node GUID plus named pin/direction; topology, defaults,
    types, variable definitions and execution-link ordering remain protected.
    """
    if description.get("schemaVersion") != 1 or not description.get("supported") or not description.get("success"):
        raise ValueError("Blueprint inspector did not return a successful schema-1 editor snapshot")
    if not isinstance(description.get("graphs"), list) or not isinstance(description.get("variables"), list):
        raise ValueError("Blueprint inspector omitted graph or variable arrays")
    def reference(value):
        return {key: item for key, item in value.items() if key != "pinId"}

    graphs = []
    for original in description["graphs"]:
        graph = dict(original)
        nodes = []
        for original_node in original["nodes"]:
            node = {key: value for key, value in original_node.items() if key not in ("index", "pins")}
            pins = []
            for original_pin in original_node["pins"]:
                pin = {key: value for key, value in original_pin.items() if key not in ("id", "index")}
                for field in ("links", "subPins"):
                    if field in pin:
                        pin[field] = [reference(item) for item in pin[field]]
                for field in ("parentPin", "referencePassThrough"):
                    if field in pin:
                        pin[field] = reference(pin[field])
                pins.append(pin)
            node["pins"] = sorted(pins, key=lambda item: json.dumps(item, sort_keys=True, ensure_ascii=False))
            nodes.append(node)
        graph["nodes"] = sorted(nodes, key=lambda item: item["guid"])
        graphs.append(graph)
    content = {"graphs": sorted(graphs, key=lambda item: (item.get("guid", ""), item.get("name", ""))),
               "variables": sorted(description["variables"], key=lambda item: item.get("name", ""))}
    return hashlib.sha256(json.dumps(content, ensure_ascii=False, sort_keys=True,
                                     separators=(",", ":")).encode("utf-8")).hexdigest()


def discover(unreal):
    """AssetRegistry ancestry determines GA membership; suspicious unresolved names stay visible in the audit."""
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    base = unreal.TopLevelAssetPath("/Script/GameplayAbilities", "GameplayAbility")
    derived = {object_path(item) for item in registry.get_derived_class_names([base], set())}
    blueprint_base = unreal.TopLevelAssetPath("/Script/Engine", "Blueprint")
    blueprint_types = {"/Script/Engine.Blueprint"} | {
        object_path(item) for item in registry.get_derived_class_names([blueprint_base], set())}
    result = []
    for asset in registry.get_all_assets(True):
        if str(asset.package_name).startswith(("/Engine/", "/Script/", "/Temp/")):
            continue
        if object_path(asset.asset_class_path) not in blueprint_types:
            continue
        generated = object_path(asset.get_tag_value("GeneratedClass"))
        if generated in derived or str(asset.asset_name).startswith(("GA_", "GA-")):
            result.append(object_path(asset))
    return sorted(set(result))


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True), encoding="utf-8")


def run_unreal():
    import unreal

    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--asset", action="append", default=[])
    parser.add_argument("--request")
    parser.add_argument("--output-dir")
    args, _ = parser.parse_known_args(sys.argv[1:])
    request_path = args.request or os.environ.get("GA_BLUEPRINT_AUDIT_REQUEST")
    request = json.loads(Path(request_path).read_text(encoding="utf-8-sig")) if request_path else {}
    if not isinstance(request, dict):
        raise ValueError("Audit request must be a JSON object")
    assets = args.asset or request.get("assets")
    if not assets and os.environ.get("GA_BLUEPRINT_AUDIT_ASSETS"):
        assets = json.loads(os.environ["GA_BLUEPRINT_AUDIT_ASSETS"])
    if assets is None:
        assets = discover(unreal)
    if not isinstance(assets, list) or not assets or any(not isinstance(item, str) for item in assets):
        raise ValueError("Blueprint audit requires a nonempty list of asset paths; zero discovery is a failure")
    assets = sorted(set(assets))
    project = Path(unreal.Paths.project_dir()).resolve()
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    output = args.output_dir or request.get("outputDir") or "Saved/GAAuthoring/UnifiedGA/BlueprintAudit/" + stamp
    out = (project / output).resolve()
    if not out.is_relative_to((project / "Saved").resolve()):
        raise ValueError("Blueprint audit reports must be written beneath the project Saved directory")
    report = {"schemaVersion": 1, "startedUtc": stamp, "readOnly": True, "success": False,
              "requested": assets, "blueprints": [], "errors": []}
    for path in assets:
        try:
            blueprint = unreal.load_asset(path)
            if not isinstance(blueprint, unreal.Blueprint):
                raise ValueError("Asset is missing or is not a Blueprint")
            data = json.loads(unreal.AbilityBlueprintAuditLibrary.describe_blueprint(blueprint))
            digest = graph_digest(data)
            name = str(blueprint.get_name()) + "-" + hashlib.sha256(path.encode("utf-8")).hexdigest()[:12] + ".json"
            write_json(out / name, data)
            report["blueprints"].append({"assetPath": path, "description": name, "graphDigest": digest,
                                         "graphCount": len(data["graphs"]),
                                         "nodeCount": sum(len(graph["nodes"]) for graph in data["graphs"])})
        except Exception as error:
            report["errors"].append({"assetPath": path, "error": str(error)})
        write_json(out / "index.json", report)
    report["success"] = not report["errors"] and len(report["blueprints"]) == len(assets)
    report["completedUtc"] = dt.datetime.now(dt.timezone.utc).isoformat()
    write_json(out / "index.json", report)
    unreal.log("Blueprint graph audit: " + str(out / "index.json"))
    if not report["success"]:
        raise RuntimeError("Blueprint graph audit incomplete; inspect index.json")


def self_test():
    baseline = {"schemaVersion": 1, "supported": True, "success": True,
                "graphs": [{"name": "EventGraph", "nodes": [{"guid": "node", "pins": []}]}],
                "variables": [], "parentClass": "OldParent"}
    reparented = json.loads(json.dumps(baseline))
    reparented["parentClass"] = "NewParent"
    assert graph_digest(baseline) == graph_digest(reparented)
    changed = json.loads(json.dumps(baseline))
    changed["graphs"][0]["nodes"][0]["pins"].append({"name": "Then", "links": [{"pinId": "other"}]})
    assert graph_digest(baseline) != graph_digest(changed)
    dynamic = json.loads(json.dumps(changed))
    dynamic["graphs"][0]["nodes"][0]["pins"] = [
        {"name": "A", "direction": "input", "id": "transient1", "index": 0, "defaultValue": "1", "links": []},
        {"name": "Then", "direction": "output", "id": "transient2", "index": 1,
         "links": [{"nodeGuid": "target", "pinName": "execute", "pinId": "transient3"}]}]
    reconstructed = json.loads(json.dumps(dynamic))
    pins = reconstructed["graphs"][0]["nodes"][0]["pins"]
    pins.reverse()
    for index, pin in enumerate(pins):
        pin["index"] = index
        pin["id"] = "new" + str(index)
        for link in pin["links"]:
            link["pinId"] = "newlink"
    assert graph_digest(dynamic) == graph_digest(reconstructed)
    pins[0]["links"][0]["nodeGuid"] = "differentTarget"
    assert graph_digest(dynamic) != graph_digest(reconstructed)
    pins[0]["links"][0]["nodeGuid"] = "target"
    pins[1]["defaultValue"] = "2"
    assert graph_digest(dynamic) != graph_digest(reconstructed)
    for invalid in ({}, dict(baseline, supported=False), dict(baseline, variables=None)):
        try:
            graph_digest(invalid)
        except ValueError:
            continue
        raise AssertionError("Invalid/unsupported/partial snapshots must fail")
    print("PASS: Blueprint graph digest preserves reparent metadata independence and detects topology changes")


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        self_test()
    else:
        run_unreal()
