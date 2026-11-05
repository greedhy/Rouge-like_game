import unreal as u,json
from pathlib import Path
w=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
r={'map':w.get_path_name(),'pie':bool(u.EditorLevelLibrary.get_game_world()),'dirty_maps':[x.get_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()],'dirty_sword_assets':[x.get_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_content_packages() if '/SwordShield/' in x.get_name()]}
r['passed']=not r['pie'] and not r['dirty_maps'] and not r['dirty_sword_assets'] and 'L_RoguelikeRun' in r['map']
(Path(u.Paths.project_dir())/'Saved/GAAuthoring/SwordShield/restored-editor.json').write_text(json.dumps(r,indent=2))
assert r['passed'],r
