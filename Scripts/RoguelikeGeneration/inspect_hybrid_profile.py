import unreal as u,json
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve();out=root/'Saved/GAAuthoring/DungeonHybrid';out.mkdir(parents=True,exist_ok=True)
report={}
for asset in u.AssetRegistryHelpers.get_asset_registry().get_assets_by_class(u.TopLevelAssetPath('/Script/test_25d','DungeonRunProfile'),True):
 p=asset.get_asset()
 report[p.get_path_name()]={'roster':[{'id':str(x.id),'class':str(x.enemy_class),'health':x.health} for x in p.normal_roster],'mass':str(p.monster_type)}
monster=u.load_asset('/Game/PureMass/DA_MassGoblin')
report['mass_type']={k:str(monster.get_editor_property(k)) for k in ['health','mesh','radius','half_height','attack_damage','animation_provider']}
report['physics_asset']=str(monster.mesh.get_editor_property('physics_asset'))
(out/'profile-before.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
