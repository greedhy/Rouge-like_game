import unreal as u,json
from pathlib import Path
assert not u.EditorLevelLibrary.get_game_world()
f=Path(u.Paths.project_dir())/'Saved/GAAuthoring/StanderMass'
r=json.loads((f/'restore.json').read_text());assert u.EditorLoadingAndSavingUtils.load_map(r['map'])
u.EditorLevelLibrary.set_level_viewport_camera_info(u.Vector(*r['location']),u.Rotator(pitch=r['rotation'][0],yaw=r['rotation'][1],roll=r['rotation'][2]))

r={'map':u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name(),'pie':bool(u.EditorLevelLibrary.get_game_world()),'dirty':[x.get_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()],'dirty_maps':[x.get_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]}
(f/'restored-editor.json').write_text(json.dumps(r,indent=2));assert not r['pie'] and not r['dirty'] and not r['dirty_maps']
