import unreal as u,json
from pathlib import Path
path='/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/Animations/AM_Block';a=u.load_asset(path);r={'before':a.get_play_length()}
r['reload']=str(u.EditorLoadingAndSavingUtils.reload_packages([a.get_outer()]))
a=u.load_asset(path);r['after']=a.get_play_length();u.EditorAssetLibrary.save_loaded_asset(a,False)
(Path(u.Paths.project_dir())/'Saved/GAAuthoring/StanderWeapons/reload.json').write_text(json.dumps(r))
