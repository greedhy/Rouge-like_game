"""Unsaved four-size real-enemy preview; reuse the existing isolated test GameMode."""
import json
from pathlib import Path
import unreal as u

out = Path(u.Paths.project_dir()) / "Saved/GAAuthoring/StunStars/20261116"
assert not u.EditorLevelLibrary.get_game_world()
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
editor = u.get_editor_subsystem(u.UnrealEditorSubsystem)
location, rotation = u.EditorLevelLibrary.get_level_viewport_camera_info()
(out / "preview-restore.json").write_text(json.dumps({
    "map": editor.get_editor_world().get_path_name().split(".")[0],
    "location": [location.x, location.y, location.z],
    "rotation": [rotation.pitch, rotation.yaw, rotation.roll]}))
mode = u.EditorAssetLibrary.load_blueprint_class("/Game/BluePrints/Characters/StoneGolem/BP_SG_TestGameMode")
assert mode
defaults = u.get_default_object(mode)
assert defaults.get_editor_property("character_class_info")
assert defaults.get_editor_property("player_controller_class") == u.PlayerController.static_class()
u.EditorLoadingAndSavingUtils.new_blank_map(False)
world = editor.get_editor_world()
world.get_world_settings().set_editor_property("default_game_mode", mode)
actors = u.get_editor_subsystem(u.EditorActorSubsystem)
floor = actors.spawn_actor_from_class(u.StaticMeshActor, u.Vector(0, 0, -5))
floor.static_mesh_component.set_mobility(u.ComponentMobility.MOVABLE)
floor.static_mesh_component.set_static_mesh(u.load_asset("/Engine/BasicShapes/Cube"))
floor.set_actor_scale3d(u.Vector(30, 30, .1))
floor.set_actor_location(u.Vector(0, 0, -5), False, True)
floor.set_editor_property("tags", ["StunStarsPreview"])
for index, (name, path, height) in enumerate([
    ("Goblin", "LegacyExpansion/BP_GoblinSpear", 51),
    ("Ghoul", "LegacyExpansion/BP_Ghoul", 88),
    ("StoneGolem", "StoneGolem/BP_StoneGolemElite", 89),
    ("Grux", "Grux/Expansion/BP_GruxBoss", 130),
]):
    cls = u.EditorAssetLibrary.load_blueprint_class("/Game/BluePrints/Characters/" + path)
    actor = actors.spawn_actor_from_class(cls, u.Vector(0, index * 260 - 390, height))
    actor.set_actor_location(u.Vector(0, index * 260 - 390, height), False, True)
    actor.set_actor_label("StunStars_" + name)
    actor.set_editor_property("auto_possess_ai", u.AutoPossessAI.DISABLED)
    actor.set_editor_property("tags", ["Enemy", "StunStarsPreview", name])
    actor.get_component_by_class(u.CharacterMovementComponent).set_movement_mode(u.MovementMode.MOVE_NONE)
actors.clear_actor_selection_set()
u.EditorLevelLibrary.set_level_viewport_camera_info(u.Vector(1000, 0, 580), u.Rotator(pitch=-24, yaw=180, roll=0))
u.log("STUN_STARS_PREVIEW_READY")
