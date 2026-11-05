"""Read-only live inventory. Does not start/stop PIE, open maps, or modify assets."""
import unreal as u,json,re,time
from pathlib import Path
root=Path(u.Paths.project_dir());out=root/'Saved/GAAuthoring/StanderWeapons';out.mkdir(parents=True,exist_ok=True)
registry=u.AssetRegistryHelpers.get_asset_registry()
r={'source':'live Unreal read-only','timestamp':time.time(),'pie':bool(u.EditorLevelLibrary.get_game_world()),'dirty_content':[x.get_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()],'dirty_maps':[x.get_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()],'groups':[],'models':[],'demo':[]}
def vec(v):return [v.x,v.y,v.z]
def tr(t):return {'translation':vec(t.translation),'rotation':[t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w],'scale':vec(t.scale3d)}
for ad in registry.get_assets_by_path('/Game/PlatformAnimation/Models',True):
 if str(ad.asset_class_path.asset_name)=='SkeletalMesh':
  a=ad.get_asset();r['models'].append({'path':a.get_path_name(),'skeleton':a.get_editor_property('skeleton').get_path_name()})
options=u.AnimPoseEvaluationOptions(evaluation_type=u.AnimDataEvalType.RAW,should_retarget=False,extract_root_motion=False)
for group in sorted((root/'Content/PlatformAnimation/Animation').iterdir()):
 if not group.is_dir():continue
 row={'name':group.name,'clips':[]}
 for ad in registry.get_assets_by_path('/Game/PlatformAnimation/Animation/'+group.name,True):
  if str(ad.asset_class_path.asset_name)!='AnimSequence':continue
  a=ad.get_asset();name=a.get_name();attack=any(x in name for x in ['Attack','Skill','Shoot','Fire'])
  c={'path':a.get_path_name(),'name':name,'duration':a.get_play_length(),'skeleton':a.get_editor_property('skeleton').get_path_name(),'attack_candidate':attack,'contact_confirmed':False,'samples':[]}
  if attack:
   for i in range(61):
    t=c['duration']*i/60;p=u.AnimPoseExtensions.get_anim_pose_at_time(a,t,options);sample={'time':t}
    for bone in ['root','hand_r','hand_l','ik_hand_r','ik_hand_l','foot_r','foot_l']:
     sample[bone]=tr(u.AnimPoseExtensions.get_bone_pose(p,bone,u.AnimPoseSpaces.WORLD))
    c['samples'].append(sample)
   # Data-derived maximum hand/root-relative speed is a candidate, not a contact assertion.
   peaks=[]
   for bone in ['hand_r','hand_l','foot_r','foot_l']:
    speeds=[]
    for before,after in zip(c['samples'],c['samples'][1:]):
     dt=after['time']-before['time'];delta=[(after[bone]['translation'][j]-after['root']['translation'][j])-(before[bone]['translation'][j]-before['root']['translation'][j]) for j in range(3)]
     speeds.append((sum(v*v for v in delta)**.5/max(dt,1e-6),after['time']))
    peak=max(speeds);peaks.append({'bone':bone,'speed_cm_s':peak[0],'time':peak[1]})
   c['motion_peak_candidates']=peaks
  row['clips'].append(c)
 r['groups'].append(row)
 # Persist progress so a slow source cannot erase completed observations.
 (out/'inventory-live-progress.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
world=u.load_asset('/Game/PlatformAnimation/Maps/Platform_Animations')
for actor in u.GameplayStatics.get_all_actors_of_class(world,u.Actor):
 for c in actor.get_components_by_class(u.SkeletalMeshComponent):
  mesh=c.get_editor_property('skeletal_mesh_asset')
  if mesh:r['demo'].append({'actor':actor.get_name(),'component':c.get_path_name(),'mesh':mesh.get_path_name(),'parent':str(c.get_attach_parent()),'socket':str(c.get_attach_socket_name()),'relative_transform':tr(c.get_relative_transform())})
r['complete']=True
(out/'inventory.json').write_text(json.dumps(r,indent=2),encoding='utf-8');print('Stander weapon inventory complete')
