"""Read saved enemy assets and animation nodes; never mutate Content."""
import json
import os
import hashlib
from pathlib import Path
import unreal as u

root = Path(u.Paths.project_dir())
out = root / "Saved/GAAuthoring/StunStars/20261116"
baseline = json.loads((out / "inspection.json").read_text(encoding="utf-8"))
rows, animations, errors, overrides = [], {}, [], {}
for entry in baseline["enemies"]:
    bp = u.load_asset(entry["asset"])
    cdo = u.get_default_object(bp.generated_class())
    stars = cdo.get_editor_property("stun_debuff_component")
    system = stars.get_asset()
    tag = stars.get_editor_property("debuff_tag").export_text()
    if 'TagName="Debuff.Stun"' not in tag:
        errors.append(entry["asset"] + ": wrong stun tag " + tag)
    if not system or system.get_name() != "NS_Stars_EnemyStun":
        errors.append(entry["asset"] + ": missing NS_Stars")
    mesh = cdo.get_component_by_class(u.SkeletalMeshComponent)
    anim = mesh.get_editor_property("anim_class")
    if isinstance(cdo, u.TelegraphedEnemyCharacter):
        config = cdo.get_editor_property("combat_config")
        if isinstance(cdo, u.StoneGolemCharacter):
            config = cdo.get_editor_property("monster_config")
        if config:
            anim = config.get_editor_property("anim_class")
    chain = []
    while anim:
        chain.append(anim.get_path_name())
        # Blueprint parent_class is available even when UClass's superclass accessor isn't exposed.
        anim_bp = u.load_asset(anim.get_path_name().split(".")[0])
        if not isinstance(anim_bp, u.AnimBlueprint):
            break
        animations[anim_bp.get_path_name()] = anim_bp
        try:
            overrides[anim_bp.get_path_name()] = [x.export_text() for x in anim_bp.get_editor_property("ParentAssetOverrides")]
        except Exception as error:
            overrides[anim_bp.get_path_name()] = str(error)
        parent = u.EditorAssetLibrary.find_asset_data(anim_bp.get_path_name()).get_tag_value("ParentClass") or ""
        if "/Game/" not in parent:
            break
        anim = u.load_class(None, parent.split("'")[1] if "'" in parent else parent)
    rows.append({"asset": entry["asset"], "stars": system.get_path_name() if system else None,
                 "tag": tag, "animation_chain": chain})
nodes = []
for obj in u.ObjectIterator(u.AnimGraphNode_Base):
    if not any(obj.get_path_name().startswith(path + ":") for path in animations):
        continue
    try:
        data = obj.get_editor_property("node").export_text()
    except Exception:
        data = ""
    nodes.append({"path": obj.get_path_name(), "class": obj.get_class().get_name(), "node": data})
mass = []
for entry in baseline["mass_types"]:
    asset = u.load_asset(entry["asset"])
    try:
        system = asset.get_editor_property("stun_system")
        if not system or system.get_name() != "NS_Stars_EnemyStun":
            errors.append(entry["asset"] + ": missing Mass stars")
        mass.append({"asset": entry["asset"], "stars": system.get_path_name() if system else None})
    except Exception as error:
        errors.append(str(error))
author_path = out / "author-latest.json"
if author_path.exists():
    author = json.loads(author_path.read_text(encoding="utf-8"))
    if author["process_id"] == os.getpid():
        errors.append("Independent process required for saved asset verification")
    for path, expected in author["after"].items():
        file = root / "Content" / (path.removeprefix("/Game/") + ".uasset")
        if hashlib.sha256(file.read_bytes()).hexdigest() != expected:
            errors.append("Saved package differs: " + path)
    original = root / "Content/Assets/Effects/Stun/NS_Stars.uasset"
    if hashlib.sha256(original.read_bytes()).hexdigest() != author["stars_before"]:
        errors.append("Shared NS_Stars changed")
report = {"passed": not errors, "process_id": os.getpid(), "errors": errors, "enemies": rows,
          "mass": mass, "animation_nodes": nodes, "animation_overrides": overrides,
          "dirty_content": [p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
          "dirty_maps": [p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]}
(out / "asset-verification.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
u.log("STUN_STARS_ASSETS " + str(report["passed"]) + " errors=" + str(len(errors)))
