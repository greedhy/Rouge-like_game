import unreal as u,json
from pathlib import Path
assert not u.EditorLevelLibrary.get_game_world()
f=Path(u.Paths.project_dir())/'Saved/GAAuthoring/Stander'
r=json.loads((f/'restore.json').read_text());assert u.EditorLoadingAndSavingUtils.load_map(r['map'])
u.EditorLevelLibrary.set_level_viewport_camera_info(u.Vector(*r['location']),u.Rotator(pitch=r['rotation'][0],yaw=r['rotation'][1],roll=r['rotation'][2]))
