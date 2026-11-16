"""Read real enemy stun component/animation defaults; never save Content."""
import json
from pathlib import Path
import unreal as u

root = Path(u.Paths.project_dir())
out = root / "Saved/GAAuthoring/StunStars/20261116"
out.mkdir(parents=True, exist_ok=True)


def path(value):
    return value.get_path_name() if value else None


report = {"success": False, "enemies": [], "mass_types": []}
report["dirty_before"] = [path(p) for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
report["dirty_maps"] = [path(p) for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
registry = u.AssetRegistryHelpers.get_asset_registry()
for asset in registry.get_assets_by_path("/Game/BluePrints/Characters", recursive=True):
    if str(asset.asset_class_path.asset_name) != "Blueprint":
        continue
    name = str(asset.package_name)
    cls = u.EditorAssetLibrary.load_blueprint_class(name)
    cdo = u.get_default_object(cls) if cls else None
    if not isinstance(cdo, u.EnemyCharacter):
        continue
    mesh = cdo.get_component_by_class(u.SkeletalMeshComponent)
    stun = cdo.get_editor_property("stun_debuff_component")
    capsule = cdo.get_component_by_class(u.CapsuleComponent)
    row = {"asset": name, "class": path(cls), "anim_class": path(mesh.get_editor_property("anim_class")),
           "mesh": path(mesh.get_editor_property("skeletal_mesh_asset")),
           "mesh_location": str(mesh.get_editor_property("relative_location")),
           "mesh_scale": str(mesh.get_editor_property("relative_scale3d")),
           "half_height": capsule.get_unscaled_capsule_half_height(),
           "radius": capsule.get_unscaled_capsule_radius()}
    if stun:
        row["stun"] = {"asset": path(stun.get_asset()), "tag": str(stun.get_editor_property("debuff_tag")),
                       "location": str(stun.get_editor_property("relative_location")),
                       "scale": str(stun.get_editor_property("relative_scale3d")),
                       "auto_activate": stun.get_editor_property("auto_activate"),
                       "visible": stun.is_visible()}
    report["enemies"].append(row)
for asset in registry.get_assets_by_path("/Game/PureMass", recursive=True):
    if str(asset.asset_class_path.asset_name) == "PureMassMonsterType":
        obj = asset.get_asset()
        report["mass_types"].append({"asset": path(obj), "half_height": obj.get_editor_property("half_height"),
                                     "radius": obj.get_editor_property("radius")})
report["dirty_after"] = [path(p) for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
report["success"] = True
(out / "inspection.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
u.log("STUN_STARS_INSPECT_OK " + str(len(report["enemies"])))
