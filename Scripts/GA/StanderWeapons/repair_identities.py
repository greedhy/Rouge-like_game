import unreal as u,json,shutil,uuid
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve();assert not u.EditorLevelLibrary.get_game_world();assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
m=json.loads((root/'Scripts/GA/StanderWeapons/weapon-manifest.json').read_text(encoding='utf-8'));run=root/'Saved/GAAuthoring/StanderWeapons'/('identity-'+uuid.uuid4().hex);run.mkdir(parents=True)
cat=u.load_asset('/Game/BluePrints/Characters/Stander/DA_SwordAbilityInfo');rows=list(cat.get_editor_property('ability_information'));shutil.copy2(root/'Content/BluePrints/Characters/Stander/DA_SwordAbilityInfo.uasset',run/'catalog-before.uasset')
def tag(name):
 t=u.GameplayTag();t.import_text('(TagName="'+name+'")');assert name in t.export_text(),'Unregistered '+name;return t
# Validate registration for the entire batch before changing a single asset.
for f in m['families']:
 for a in f['actions']:
  for prefix in ['Abilities','Cooldown']:tag(prefix+'.StanderWeapons.'+f['family']+'.'+a['name'])
updated=[]
for f in m['families']:
 for a in f['actions']:
  folder='/Game/BluePrints/Characters/Stander/Weapons/'+f['family'];cfg=u.load_asset(folder+'/DA_'+a['name']);bp=u.load_asset(folder+'/GA_'+a['name']);assert cfg and bp
  for obj in [cfg,bp]:
   assert u.EditorAssetLibrary.get_metadata_tag(obj,'AuthoringOwner')=='StanderWeaponsV1'
   package=obj.get_path_name().split('.')[0];shutil.copy2(root/'Content'/(package.removeprefix('/Game/')+'.uasset'),run/(package.replace('/','_')+'.uasset'))
  identity=tag('Abilities.StanderWeapons.'+f['family']+'.'+a['name']);cooldown=tag('Cooldown.StanderWeapons.'+f['family']+'.'+a['name'])
  cfg.set_editor_property('action_tag',identity);cfg.set_editor_property('cooldown_tag',cooldown);assert u.EditorAssetLibrary.save_loaded_asset(cfg,False)
  cdo=u.get_default_object(bp.generated_class());cdo.initialize_action_identity();u.BlueprintEditorLibrary.compile_blueprint(bp);assert u.EditorAssetLibrary.save_loaded_asset(bp,False)
  matches=[i for i,row in enumerate(rows) if row.get_editor_property('ability')==bp.generated_class()];assert len(matches)==1
  row=rows[matches[0]];row.import_text('(AbilityTag='+identity.export_text()+',CooldownTag='+cooldown.export_text()+')');rows[matches[0]]=row;updated.append(cfg.get_path_name())
cat.set_editor_property('ability_information',rows);assert u.EditorAssetLibrary.save_loaded_asset(cat,False)
(run/'result.json').write_text(json.dumps({'updated':updated,'complete':True},indent=2));print('Identity repair complete',len(updated))
