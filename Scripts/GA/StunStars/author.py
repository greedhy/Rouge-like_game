"""Bind the tuned NS_Stars copy on 16 enemy defaults and two Mass type assets."""
import hashlib
import json
import os
import shutil
import uuid
from pathlib import Path
import unreal as u

root = Path(u.Paths.project_dir())
base = root / "Saved/GAAuthoring/StunStars/20261116"
out = base / ("author-" + uuid.uuid4().hex)
out.mkdir(parents=True)
baseline = json.loads((base / "inspection.json").read_text(encoding="utf-8"))
targets = sorted([r["asset"] for r in baseline["enemies"]], key=lambda p: (p.rsplit("/", 1)[-1] != "BP_EnemyBase", p))
targets += [r["asset"].split(".")[0] for r in baseline["mass_types"]]
assert len(targets) == 18
dirty = {p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
assert not dirty.intersection(targets), "Unsaved target assets must be preserved"
assert not u.EditorLevelLibrary.get_game_world(), "Stop PIE before asset authoring"
source_path = "/Game/Assets/Effects/Stun/NS_Stars"
stars_path = "/Game/BluePrints/Characters/Effects/NS_Stars_EnemyStun"
stars = u.load_asset(stars_path)
assert isinstance(stars, u.NiagaraSystem)


def filename(path):
    return root / "Content" / (path.removeprefix("/Game/") + ".uasset")


def digest(path):
    return hashlib.sha256(filename(path).read_bytes()).hexdigest()


report = {"success": False, "process_id": os.getpid(), "targets": targets, "saved": [],
          "before": {p: digest(p) for p in targets}, "stars_before": digest(source_path)}
for path in targets:
    backup = out / "backup" / (path.removeprefix("/Game/") + ".uasset")
    backup.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(filename(path), backup)
    asset = u.load_asset(path)
    if isinstance(asset, u.Blueprint):
        cdo = u.get_default_object(asset.generated_class())
        component = cdo.get_editor_property("stun_debuff_component")
        assert not component.get_asset() or component.get_asset().get_name() in ("NS_Stars", "NS_Stars_EnemyStun"), "Preserve another authored effect"
        component.set_asset(stars)
        component.set_editor_property("auto_activate", False)
        u.BlueprintEditorLibrary.compile_blueprint(asset)
        assert str(asset.get_editor_property("status")) != "BlueprintStatus.BS_ERROR"
        assert u.get_default_object(asset.generated_class()).get_editor_property("stun_debuff_component").get_asset() == stars
    else:
        assert isinstance(asset, u.PureMassMonsterType)
        asset.set_editor_property("stun_system", stars)
    assert u.EditorAssetLibrary.save_loaded_asset(asset, False), path
    report["saved"].append(path)
assert u.EditorAssetLibrary.save_loaded_asset(stars, False)
report["saved"].append(stars_path)
assert digest(source_path) == report["stars_before"], "Shared NS_Stars source changed"
report.update(success=True, after={p: digest(p) for p in report["saved"]},
              dirty_after=[p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()])
(out / "author.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
(base / "author-latest.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
u.log("STUN_STARS_AUTHOR_OK " + str(out))
