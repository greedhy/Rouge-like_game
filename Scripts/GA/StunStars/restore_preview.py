"""Restore the original map and camera; never save the transient preview."""
import json
from pathlib import Path
import unreal as u

assert not u.EditorLevelLibrary.get_game_world(), "Stop this task's PIE first"
world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().startswith("/Temp/")
assert any(a.actor_has_tag("StunStarsPreview") for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors())
state = json.loads((Path(u.Paths.project_dir()) / "Saved/GAAuthoring/StunStars/20261116/preview-restore.json").read_text())
assert u.EditorLoadingAndSavingUtils.load_map(state["map"])
pitch, yaw, roll = state["rotation"]
u.EditorLevelLibrary.set_level_viewport_camera_info(u.Vector(*state["location"]), u.Rotator(pitch=pitch, yaw=yaw, roll=roll))
report = {"restored_map": u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name(),
          "pie_active": bool(u.EditorLevelLibrary.get_game_world()),
          "dirty_maps": [p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
          "dirty_content": [p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}
(Path(u.Paths.project_dir()) / "Saved/GAAuthoring/StunStars/20261116/final-editor-state.json").write_text(json.dumps(report, indent=2))
u.log("STUN_STARS_PREVIEW_RESTORED")
