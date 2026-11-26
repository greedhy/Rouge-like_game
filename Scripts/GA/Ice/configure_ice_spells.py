"""Audit or apply the standalone Damage.Ice migration to the existing six ice spells."""

import json
import os
import shutil
from pathlib import Path
from uuid import uuid4

import unreal as u


COMPOSITIONS = "/Game/BluePrints/AbilitySystem/Player/Ability/Compositions"
EXTENSIONS = "/Game/PrototypeExtensions/Batch5"
BATCH6 = "/Game/CompositionBatch6"

# Each binding identifies the actual GA input and the owning data asset for its damage.
# Grounds and attach/delay controllers retain their existing non-damaging behavior.
PROTOTYPES = (
    *((f"{folder}/GA_{name}", f"{COMPOSITIONS}/DA_{name}", ())
      for folder in (COMPOSITIONS, "/Game/PureMass/Abilities")
      for name in ("FrostFan", "FrostFault")),
    (f"{EXTENSIONS}/GA_FrostParasite", f"{EXTENSIONS}/DA_FrostParasite",
     (("period_child", f"{EXTENSIONS}/DA_FrostParasite_Pulse"),)),
    (f"{BATCH6}/GA_GlacialStorm", f"{BATCH6}/DA_GlacialStorm",
     (("on_expire", f"{BATCH6}/DA_FrostField"),)),
)
GRAPHS = (
    ("/Game/UnifiedAbilities/ArtBatch", "GlacialLance", ("Lance", "Shatter")),
    ("/Game/UnifiedAbilities/MageBatch", "RimeSigil", ("FrostField", "FrostBurst")),
)
CATALOGS = (
    "/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo",
    "/Game/PureMass/DA_MassAbilityInfo",
    f"{EXTENSIONS}/DA_AbilityInfo",
)


def asset(path):
    result = u.load_asset(path)
    assert result, path
    return result


def ability(path):
    cls = u.EditorAssetLibrary.load_blueprint_class(path)
    assert cls, path
    return u.get_default_object(cls), cls.get_path_name()


def tag_text(name):
    tag = u.GameplayTag()
    tag.import_text(f'(TagName="{name}")')
    assert name in tag.export_text(), name
    return tag


def check_damage(payload, owner, changes):
    assert payload.get_editor_property("damage") > 0, payload.get_path_name()
    previous = payload.get_editor_property("damage_type").export_text()
    allowed = ("Ice", "Magic", "Water")
    if payload.get_name() == "DA_FrostFan":
        # The original Multishot template left its fire channel on this ice spell.
        allowed += ("Fire",)
    assert any(f'"Damage.{kind}"' in previous for kind in allowed), (
        payload.get_path_name(), previous)
    changes[payload.get_path_name()] = (payload, owner, previous)


def inspect():
    changes, classes = {}, set()
    for path, root_path, children in PROTOTYPES:
        cdo, class_path = ability(path)
        classes.add(class_path)
        root = asset(root_path)
        assert cdo.get_editor_property("prototype") == root, path
        if children:
            for field, child_path in children:
                child = asset(child_path)
                assert root.get_editor_property(field) == child, (root_path, field)
                check_damage(child, child, changes)
        else:
            check_damage(root, root, changes)
    for folder, name, nodes in GRAPHS:
        path = f"{folder}/GA_{name}"
        cdo, class_path = ability(path)
        classes.add(class_path)
        graph = asset(f"{folder}/DA_{name}_Graph")
        assert cdo.get_editor_property("graph") == graph, path
        configs = {node.get_editor_property("config").get_name(): node.get_editor_property("config")
                   for node in graph.get_editor_property("nodes")}
        for node in nodes:
            presentation = asset(f"{folder}/DA_{name}_{node}")
            config = configs[node + "Config"]
            assert config.get_editor_property("presentation_parameters") == presentation, (path, node)
            check_damage(config.get_editor_property("parameters"), graph, changes)
            check_damage(presentation, presentation, changes)
    catalog_membership = {}
    for path in CATALOGS:
        rows = asset(path).get_editor_property("ability_information")
        catalog_membership[path] = sorted(
            row.export_text() for row in rows if any(class_path in row.export_text() for class_path in classes))
    for class_path in classes:
        assert any(class_path in row for rows in catalog_membership.values() for row in rows), class_path
    return changes, catalog_membership


def apply(changes, run_dir):
    pending = [(payload, owner) for payload, owner, previous in changes.values() if '"Damage.Ice"' not in previous]
    owners = {owner.get_path_name(): owner for _, owner in pending}
    project = Path(u.Paths.project_dir()).resolve()
    ice = tag_text("Damage.Ice")
    for path in sorted(owners):
        relative = Path("Content") / (path.split(".", 1)[0].removeprefix("/Game/") + ".uasset")
        backup = run_dir / "backup" / relative
        backup.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(project / relative, backup)
    for payload, _ in pending:
        payload.set_editor_property("damage_type", ice)
    for owner in owners.values():
        if isinstance(owner, u.AbilityGraphDefinition):
            assert not owner.validate_for_authoring(), owner.get_path_name()
        assert u.EditorAssetLibrary.save_loaded_asset(owner, only_if_is_dirty=False), owner.get_path_name()
    return sorted(owners)


def run():
    mode = os.environ.get("GA_ICE_MODE", "verify")
    assert mode in {"audit", "apply", "verify"}
    root = Path(u.Paths.project_saved_dir()).resolve() / "GAAuthoring/Ice"
    run_dir = root / uuid4().hex
    run_dir.mkdir(parents=True)
    report = {"mode": mode, "passed": False}
    try:
        changes, catalog = inspect()
        report["previous"] = {path: previous for path, (_, _, previous) in changes.items()}
        report["catalogs"] = catalog
        if mode == "apply":
            report["saved"] = apply(changes, run_dir)
        report["readback"] = {
            path: payload.get_editor_property("damage_type").export_text()
            for path, (payload, _, _) in changes.items()
        }
        if mode != "audit":
            assert all('"Damage.Ice"' in value for value in report["readback"].values())
        report["passed"] = True
    except Exception as error:
        report["error"] = repr(error)
        raise
    finally:
        result = json.dumps(report, indent=2, ensure_ascii=False)
        (run_dir / "assets.json").write_text(result, encoding="utf-8")
        (root / f"latest-{mode}.json").write_text(result, encoding="utf-8")


run()
