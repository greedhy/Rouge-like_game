import unreal as u,json
from pathlib import Path
r={}
for path in ['/Game/RoguelikeGeneration/Gameplay/BP_DungeonPlayer','/Game/PureMass/Abilities/GA_Prototype_Burst','/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Burst']:
 c=u.get_default_object(u.EditorAssetLibrary.load_blueprint_class(path));d={}
 for p in ['startup_ability','prototype','startup_input_tag']:
  try:d[p]=str(c.get_editor_property(p))
  except:pass
 r[path]=d
for a in u.AssetRegistryHelpers.get_asset_registry().get_assets_by_class(u.TopLevelAssetPath('/Script/test_25d','AbilityInfo'),True):
 r[str(a.package_name)]=[x.export_text() for x in a.get_asset().get_editor_property('ability_information') if 'Prototype.' in x.export_text()]
for n in ['Burst','Beam']:
 a=u.load_asset('/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/DA_Prototype_'+n)
 r[n]={p:str(a.get_editor_property(p)) for p in ['kind','loop_vfx','impact_vfx','target_self']}
Path(u.Paths.project_dir(),'Saved/GAAuthoring/DungeonHybridFix/inspect.json').write_text(json.dumps(r,indent=2))
