import unreal as u,json
from pathlib import Path
r={};s=u.load_asset('/Game/PlatformAnimation/Models/Stander_Skeleton')
r['sockets']=[]
for x in u.ObjectIterator(u.SkeletalMeshSocket):
 if 'Stander_Skeleton' not in x.get_path_name():continue
 r['sockets'].append({k:str(x.get_editor_property(k)) for k in ['socket_name','bone_name','relative_location','relative_rotation','relative_scale']})
r['clips']=[str(x.package_name) for x in u.AssetRegistryHelpers.get_asset_registry().get_assets_by_path('/Game/PlatformAnimation/Animation/01_Sword_Shield',True)]
(Path(u.Paths.project_dir())/'Saved/GAAuthoring/Stander/details.json').write_text(json.dumps(r,indent=2))

