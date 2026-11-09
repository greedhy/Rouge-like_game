"""Repair invalid authored Sword_Shield phase durations, preserving source boundaries."""
import json
import shutil
import uuid
from pathlib import Path

import unreal as u

BASE = "/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield"
ROOT = Path(u.Paths.project_dir())
RUN = ROOT / "Saved/GAAuthoring/StanderSwordTimelineRepair" / uuid.uuid4().hex
RUN.mkdir(parents=True)
lib = u.EditorAssetLibrary
assert not u.EditorLevelLibrary.get_game_world()
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
changes = []
for path in sorted(lib.list_assets(BASE, recursive=False)):
    if "/DA_" not in path:
        continue
    config = u.load_asset(path)
    if not isinstance(config, u.StanderWeaponActionConfig):
        continue
    montage = config.get_editor_property("montage")
    timeline = config.get_editor_property("timeline")
    assert timeline
    if timeline.validate_for_montage(montage) is not None:
        continue
    phases = timeline.get_editor_property("phases")
    length = montage.get_play_length()
    blend = montage.get_editor_property("blend_out").get_editor_property("blend_time")
    start = 0.0
    fields = []
    for index, phase in enumerate(phases):
        end = phase.get_editor_property("source_end")
        duration = phase.get_editor_property("duration")
        span = end - start
        assert span > 0
        minimum = span / 9.99
        if index < len(phases) - 1:
            minimum = max(minimum, span * (blend + 0.001) / (length - end))
        if duration < minimum:
            timing = phase.get_editor_property("timing")
            assert not timing.get_editor_property("data_table"), "Fix the timing table rather than its overridden value"
            phase.set_editor_property("duration", minimum)
            phases[index] = phase
            fields.append({"phase": str(phase.get_editor_property("name")), "before": duration, "after": minimum})
        start = end
    assert fields, f"Timeline failed for a reason other than excessive phase speed: {path}"
    package = timeline.get_path_name().split(".")[0]
    src = ROOT / "Content" / (package.removeprefix("/Game/") + ".uasset")
    shutil.copy2(src, RUN / src.name)
    timeline.set_editor_property("phases", phases)
    assert timeline.validate_for_montage(montage) is not None, path
    assert lib.save_loaded_asset(timeline, False), package
    changes.append({"config": path, "timeline": package, "changes": fields})
(RUN / "report.json").write_text(json.dumps(changes, indent=2), encoding="utf-8")
print(f"SWORD_TIMELINE_REPAIR_OK {len(changes)} {RUN}")
