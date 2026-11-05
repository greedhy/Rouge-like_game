import unreal as u,json
from pathlib import Path
root=Path(u.Paths.project_dir());manifest=json.loads((root/'Scripts/GA/StanderWeapons/weapon-manifest.json').read_text(encoding='utf-8'));updated=[]
assert not u.EditorLevelLibrary.get_game_world()
for f in manifest['families']:
 for a in f['actions']:
  if a['kind']!='Projectile':continue
  cfg=u.load_asset('/Game/BluePrints/Characters/Stander/Weapons/'+f['family']+'/DA_'+a['name']);assert cfg
  events=list(cfg.get_editor_property('events'))
  for e in events:
   if e.get_editor_property('kind')==u.StanderWeaponActionKind.PROJECTILE:e.set_editor_property('radius',a['projectile_radius'])
  cfg.set_editor_property('events',events);assert u.EditorAssetLibrary.save_loaded_asset(cfg,False);updated.append(cfg.get_path_name())
(root/'Saved/GAAuthoring/StanderWeapons/projectile-radius-repair.json').write_text(json.dumps({'updated':updated},indent=2))
