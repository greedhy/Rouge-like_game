"""Read-only UE probe for stale unique rule rejection, without running author main.

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>. Optional
UNIQUE_AUTHOR_CONTRACT_SOURCE selects a saved author-script baseline for RED.
Only function definitions are loaded with AST; no author top-level statements,
ensure functions, save calls, or main are executed. Reports are written to Saved.
"""
import ast
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import re
from datetime import datetime, timezone
import traceback

import unreal as u


PROJECT = Path(u.Paths.project_dir()).resolve()
CONTENT = Path(u.Paths.project_content_dir()).resolve()
SAVED = Path(u.Paths.project_saved_dir()).resolve() / "Equipment"
SOURCE = Path(os.environ.get("UNIQUE_AUTHOR_CONTRACT_SOURCE", str(PROJECT / "Scripts/Equipment/author_unique_equipment.py"))).resolve()
MANIFEST = PROJECT / "Scripts/Equipment/unique_equipment_catalog.json"
RUN_ID = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ") + "_" + str(os.getpid())
RUN_DIR = SAVED / "UniqueEquipment/AuthorContract" / RUN_ID
RUN_DIR.mkdir(parents=True, exist_ok=True)
REPORT = dict(run_id=RUN_ID, author_source=str(SOURCE), success=False,
              asset_read_only=True, original_items_validated=[], negative_cases=[])


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def hash_file(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_validation_functions():
    """Load the small validation dependency set; importing the author would execute asset authoring."""
    required = {"require", "unique_json_keys", "validate_catalog", "load_catalog", "canonical_rows", "read_rows",
                "effect_path", "curve_row", "evaluate", "tags_in", "validate_set_rule", "validate_item"}
    tree = ast.parse(SOURCE.read_text(encoding="utf-8"), filename=str(SOURCE))
    definitions = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in required]
    require({node.name for node in definitions} == required, "Author baseline lacks required validation functions")
    namespace = dict(u=u, copy=copy, json=json, math=math, re=re, Path=Path,
                     BASE="/Game/Equipment/Unique", MANIFEST_PATH=MANIFEST,
                     TABLES=u.DataTableFunctionLibrary)
    exec(compile(ast.Module(body=definitions, type_ignores=[]), str(SOURCE), "exec"), namespace)
    return namespace


def main():
    namespace = load_validation_functions()
    items = namespace["load_catalog"]()
    table_path = "/Game/Equipment/DT_Equipment"
    curve_path = "/Game/Equipment/Unique/CT_UniqueEquipmentStats"
    packages = [table_path, curve_path] + [namespace["effect_path"](item) for item in items]
    files = {package: CONTENT / (package.removeprefix("/Game/") + ".uasset") for package in packages}
    before = {package: hash_file(path) for package, path in files.items()}
    REPORT["author_source_sha256"] = hash_file(SOURCE)
    REPORT["catalog_sha256"] = hash_file(MANIFEST)
    table = u.load_asset(table_path)
    curve = u.load_asset(curve_path)
    require(isinstance(table, u.DataTable) and isinstance(curve, u.CurveTable), "Real author assets are required")
    rows = namespace["read_rows"](table)
    effects = {}
    # A negative case is meaningful only after that exact saved GE passes its real manifest contract.
    for item in items:
        effect_class = u.EditorAssetLibrary.load_blueprint_class(namespace["effect_path"](item))
        require(effect_class is not None, "Missing GE class: " + item["id"])
        cdo = u.get_default_object(effect_class)
        namespace["validate_item"](item, rows[item["id"]], cdo, curve)
        effects[item["id"]] = cdo
        REPORT["original_items_validated"].append(item["id"])
    cases = [
        ("UniqueCinderOathBoots", "melee", ("melee", "melee_ability_tag", "damage_type", "hit_effect", "required_set_tags")),
        ("UniqueAshenPrism", "projectile", ("projectile", "projectile_ability_tag", "disable_homing", "required_set_tags")),
    ]
    for identity, group, removed_fields in cases:
        changed = copy.deepcopy(next(item for item in items if item["id"] == identity))
        for field in removed_fields:
            changed.pop(field, None)
        rejected = False
        error = None
        try:
            namespace["validate_item"](changed, rows[identity], effects[identity], curve)
        except RuntimeError as caught:
            error = str(caught)
            rejected = "Undeclared " + group + " rule remains" in error
        REPORT["negative_cases"].append(dict(item_id=identity, removed_rule=group,
                                              rejected_for_residual_rule=rejected, error=error))
    after = {package: hash_file(path) for package, path in files.items()}
    REPORT["asset_sha256_before"] = before
    REPORT["asset_sha256_after"] = after
    REPORT["asset_hashes_unchanged"] = before == after
    require(before == after, "Read-only contract probe observed changed asset packages")
    require(all(case["rejected_for_residual_rule"] for case in REPORT["negative_cases"]),
            "Saved GE retained a removed manifest rule without a targeted-migration rejection")
    REPORT["success"] = True
    REPORT["finished_utc"] = datetime.now(timezone.utc).isoformat()
    (SAVED / "unique_author_contract_verified.json").write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding="utf-8")
    u.log("UNIQUE_AUTHOR_CONTRACT_SUCCESS " + str(RUN_DIR / "report.json"))


try:
    main()
except Exception:
    REPORT["error"] = traceback.format_exc()
    u.log_error("UNIQUE_AUTHOR_CONTRACT_FAILED " + REPORT["error"])
    raise
finally:
    (RUN_DIR / "report.json").write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding="utf-8")
