import unreal as u,json
from pathlib import Path
w=u.EditorLevelLibrary.get_game_world();p=u.GameplayStatics.get_player_pawn(w,0);mesh=p.get_editor_property('mesh')
r={'mesh':mesh.get_path_name(),'anim':mesh.get_anim_instance().get_path_name(),'overrides':[str(m) for m in mesh.get_editor_property('override_materials')],'weapons':[]}
for name in ['stander_sword','stander_shield']:
 c=p.get_editor_property(name);r['weapons'].append({'name':name,'parent':str(c.get_attach_parent()),'socket':str(c.get_attach_socket_name()),'relative':str(c.get_relative_transform()),'world':str(c.get_world_transform())})
o=u.AnimPoseEvaluationOptions(evaluation_type=u.AnimDataEvalType.RAW,should_retarget=False,extract_root_motion=False)
r['weapon_paths']=[{'path':str(a.package_name),'type':str(a.asset_class_path.asset_name)} for a in u.AssetRegistryHelpers.get_asset_registry().get_assets_by_path('/Game/PlatformAnimation/Materials',True)]
(Path(u.Paths.project_dir())/'Saved/GAAuthoring/Stander/runtime-mesh.json').write_text(json.dumps(r,indent=2))

