import unreal as u,json
from pathlib import Path
p='/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/Animations/AM_Block';a=u.load_asset(p);r={'before':a.get_play_length()}
e=u.get_editor_subsystem(u.AssetEditorSubsystem);e.open_editor_for_assets([a]);r['after_open']=a.get_play_length();e.close_all_editors_for_asset(a)
try:a.set_editor_property('sequence_length',2.2666667);r['set_length']=a.get_play_length()
except Exception as ex:r['error']=str(ex)
assert u.EditorAssetLibrary.save_loaded_asset(a,False)
(Path(u.Paths.project_dir())/'Saved/GAAuthoring/StanderWeapons/montage-refresh.json').write_text(json.dumps(r))
