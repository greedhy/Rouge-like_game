"""Independent live readback, no writes to assets."""
import unreal as u,json
from pathlib import Path
root=Path(u.Paths.project_dir());manifest=json.loads((root/'Scripts/GA/StanderWeapons/weapon-manifest.json').read_text(encoding='utf-8'));base='/Game/BluePrints/Characters/Stander/Weapons';report={'families':[],'errors':[]}
cat=u.load_asset('/Game/BluePrints/Characters/Stander/DA_SwordAbilityInfo');rows=cat.get_editor_property('ability_information');table=u.load_asset('/Game/Equipment/DT_Equipment');items=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table))
for f in manifest['families']:
 family=f['family'];folder=base+'/'+family;set_asset=u.load_asset(folder+'/DA_WeaponSet');assert set_asset
 abilities=set_asset.get_editor_property('abilities');assert len(abilities)==len(f['actions']);assert set_asset.get_editor_property('anim_class')
 assert len([r for r in items if r['Name']==f['item_id']])==1
 checks=[]
 for a in f['actions']:
  cfg=u.load_asset(folder+'/DA_'+a['name']);ga=u.EditorAssetLibrary.load_blueprint_class(folder+'/GA_'+a['name']);assert cfg and ga
  cdo=u.get_default_object(ga);assert cdo.get_editor_property('action_config')==cfg
  tag=cfg.get_editor_property('action_tag').export_text();matching=[r for r in rows if r.get_editor_property('ability_tag').export_text()==tag];assert len(matching)==1 and matching[0].get_editor_property('icon')
  events=cfg.get_editor_property('events');m=cfg.get_editor_property('montage');assert m and events
  assert all(0<=e.get_editor_property('time')<m.get_play_length() for e in events)
  checks.append({'action':a['name'],'events':[e.export_text() for e in events],'montage':m.get_path_name(),'tag':tag})
 report['families'].append({'family':family,'skills':checks,'weapon_set':set_asset.get_path_name()})
report['passed']=True;(root/'Saved/GAAuthoring/StanderWeapons/readback.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
