"""Run AFTER LaunchYaw reflection has been rebuilt and reloaded. Idempotent."""
import unreal as u,json,uuid,shutil
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve();assert not u.EditorLevelLibrary.get_game_world();assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
run=root/'Saved/GAAuthoring/StanderWeapons'/('radial-'+uuid.uuid4().hex);run.mkdir(parents=True)
manifest=json.loads((root/'Scripts/GA/StanderWeapons/weapon-manifest.json').read_text(encoding='utf-8'));updated=[]
for family in manifest['families']:
 for action in family['actions']:
  if action['kind']!='Projectile' or 'radial' not in action['flags']:continue
  path='/Game/BluePrints/Characters/Stander/Weapons/'+family['family']+'/DA_'+action['name'];cfg=u.load_asset(path);assert cfg and u.EditorAssetLibrary.get_metadata_tag(cfg,'AuthoringOwner')=='StanderWeaponsV1'
  shutil.copy2(root/'Content'/(path.removeprefix('/Game/')+'.uasset'),run/(family['family']+'_'+action['name']+'.uasset'))
  originals=list(cfg.get_editor_property('events'));events=[e for e in originals if e.get_editor_property('kind')!=u.StanderWeaponActionKind.PROJECTILE];times={}
  for e in originals:
   if e.get_editor_property('kind')==u.StanderWeaponActionKind.PROJECTILE:times.setdefault(e.get_editor_property('time'),e)
  assert times
  for time,template in times.items():
   for yaw in range(0,360,45):
    e=u.StanderWeaponActionEvent();e.import_text(template.export_text());e.set_editor_property('launch_yaw',float(yaw));e.set_editor_property('damage',action['damage']/4);events.append(e)
  events.sort(key=lambda e:e.get_editor_property('time'));cfg.set_editor_property('events',events);assert u.EditorAssetLibrary.save_loaded_asset(cfg,False)
  read=list(cfg.get_editor_property('events'));actual=[e for e in read if e.get_editor_property('kind')==u.StanderWeaponActionKind.PROJECTILE];assert len(actual)==len(times)*8
  updated.append({'asset':path,'projectiles':len(actual),'yaws':[e.get_editor_property('launch_yaw') for e in actual],'damage_per_projectile':action['damage']/4})
(run/'result.json').write_text(json.dumps({'updated':updated,'complete':True},indent=2));print('Radial projectile patch complete',len(updated))
