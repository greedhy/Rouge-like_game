import unreal as u,json,uuid,shutil
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve();assert not u.EditorLevelLibrary.get_game_world();assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
manifest=json.loads((root/'Scripts/GA/StanderWeapons/weapon-manifest.json').read_text(encoding='utf-8'));run=root/'Saved/GAAuthoring/StanderWeapons'/('icons-'+uuid.uuid4().hex);run.mkdir(parents=True)
cat=u.load_asset('/Game/BluePrints/Characters/Stander/DA_SwordAbilityInfo');shutil.copy2(root/'Content/BluePrints/Characters/Stander/DA_SwordAbilityInfo.uasset',run/'catalog-before.uasset');rows=list(cat.get_editor_property('ability_information'));report=[]
for f in manifest['families']:
 default_icons=[]
 weapon=u.load_asset('/Game/BluePrints/Characters/Stander/Weapons/'+f['family']+'/DA_WeaponSet');default_classes=[e.get_editor_property('ability') for e in weapon.get_editor_property('abilities') if e.get_editor_property('input_tag').export_text()!='(TagName="")']
 for a in f['actions']:
  cls=u.EditorAssetLibrary.load_blueprint_class('/Game/BluePrints/Characters/Stander/Weapons/'+f['family']+'/GA_'+a['name']);matches=[i for i,r in enumerate(rows) if r.get_editor_property('ability')==cls];assert len(matches)==1
  texture=u.load_asset(a['icon']);assert isinstance(texture,u.Texture2D)
  row=rows[matches[0]];row.import_text('(Icon="'+texture.get_path_name()+'")');rows[matches[0]]=row
  if cls in default_classes:default_icons.append(texture.get_path_name())
 assert len(default_icons)==6 and len(set(default_icons))==6,(f['family'],default_icons)
 report.append({'family':f['family'],'default_icons':default_icons})
cat.set_editor_property('ability_information',rows);assert u.EditorAssetLibrary.save_loaded_asset(cat,False)
actual=cat.get_editor_property('ability_information')
for f in manifest['families']:
 for a in f['actions']:
  cls=u.EditorAssetLibrary.load_blueprint_class('/Game/BluePrints/Characters/Stander/Weapons/'+f['family']+'/GA_'+a['name']);row=next(r for r in actual if r.get_editor_property('ability')==cls);assert row.get_editor_property('icon')==u.load_asset(a['icon'])
(run/'result.json').write_text(json.dumps({'families':report,'skills':sum(len(f['actions']) for f in manifest['families']),'passed':True},indent=2));print('Skill icon patch passed')
