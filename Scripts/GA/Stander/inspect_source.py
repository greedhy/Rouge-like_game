"""Read-only source pose, model and editor-state audit."""
import unreal as u,json
from pathlib import Path
folder=Path(u.Paths.project_dir())/'Saved/GAAuthoring/Stander';folder.mkdir(exist_ok=True)
r={'pie':bool(u.EditorLevelLibrary.get_game_world()),'dirty_assets':[x.get_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()],'dirty_maps':[x.get_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()],'meshes':[],'clips':[]}
registry=u.AssetRegistryHelpers.get_asset_registry()
for x in registry.get_assets_by_path('/Game/PlatformAnimation/Models',False):
 if str(x.asset_class_path.asset_name)!='SkeletalMesh':continue
 a=x.get_asset();r['meshes'].append({'path':a.get_path_name(),'bounds':str(a.get_bounds())})
mesh=u.load_asset('/Game/PlatformAnimation/Models/Stander');c=u.new_object(u.SkeletalMeshComponent);c.set_skeletal_mesh_asset(mesh)
r['sockets']=[{'name':str(n),'transform':str(c.get_socket_transform(n,u.RelativeTransformSpace.RTS_COMPONENT))} for n in c.get_all_socket_names() if '소켓' in str(n)]
o=u.AnimPoseEvaluationOptions(evaluation_type=u.AnimDataEvalType.RAW,should_retarget=False,extract_root_motion=False)
for name in ['Attack1','Attack2','Attack3','Block_Loop','Block_Parry','Skill7_Around','Skill6_Move']:
 a=u.load_asset('/Game/PlatformAnimation/Animation/01_Sword_Shield/Stander_Sword_Shield_'+name);row={'name':name,'length':a.get_play_length(),'samples':[]}
 for i in range(61):
  t=a.get_play_length()*i/60;p=u.AnimPoseExtensions.get_anim_pose_at_time(a,t,o);s={'t':t}
  for b in ['root','hand_r','hand_l','ik_hand_r','ik_hand_l']:
   tr=u.AnimPoseExtensions.get_bone_pose(p,b,u.AnimPoseSpaces.WORLD);s[b]={'p':[tr.translation.x,tr.translation.y,tr.translation.z],'rotation':str(tr.rotation)}
  row['samples'].append(s)
 r['clips'].append(row)
(folder/'source.json').write_text(json.dumps(r,indent=2))
print('Stander audit',r['pie'],r['dirty_assets'],len(r['clips']))
