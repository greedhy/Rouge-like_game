"""Create an unsaved, isolated PIE fixture, preserving the user's map and dirty assets."""
import json
from pathlib import Path
import unreal as u
root = Path(u.Paths.convert_relative_path_to_full(u.Paths.project_dir()))
out = root / 'Saved/GAAuthoring/AbilityTimeline/recovery-investigation'
assert not u.EditorLevelLibrary.get_game_world()
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
editor = u.get_editor_subsystem(u.UnrealEditorSubsystem)
location, rotation = u.EditorLevelLibrary.get_level_viewport_camera_info()
(out / 'restore.json').write_text(json.dumps({'map': editor.get_editor_world().get_path_name().split('.')[0],
    'throttle': u.get_default_object(u.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).get_editor_property('bThrottleCPUWhenNotForeground'),
    'dirty': [x.get_path_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
    'location': [location.x, location.y, location.z], 'rotation': [rotation.pitch, rotation.yaw, rotation.roll]}))
u.EditorLoadingAndSavingUtils.new_blank_map(False)
world = editor.get_editor_world()
world.get_world_settings().set_editor_property('default_game_mode', u.load_class(None, '/Game/BluePrints/Characters/Stander/BP_StanderGameMode.BP_StanderGameMode_C'))
actors = u.get_editor_subsystem(u.EditorActorSubsystem)
floor = actors.spawn_actor_from_class(u.StaticMeshActor, u.Vector(0, 0, -5))
floor.static_mesh_component.set_static_mesh(u.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(u.Vector(30, 30, .1))
floor.set_actor_location(u.Vector(0, 0, -5), False, True)
start = actors.spawn_actor_from_class(u.TravelPlayerStart, u.Vector(0, 0, 100))
start.set_editor_property('player_start_tag', 'RG_Entrance')
start.set_editor_property('default_entry_group', True)
actors.clear_actor_selection_set()
print('RECOVERY_PROBE_SETUP ' + world.get_path_name())
