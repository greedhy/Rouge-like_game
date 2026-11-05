import unreal as u,json
from pathlib import Path
root=Path(u.Paths.project_dir());out=root/'Saved/GAAuthoring/SwordShield';out.mkdir(parents=True,exist_ok=True)
options=u.AnimPoseEvaluationOptions(evaluation_type=u.AnimDataEvalType.RAW,should_retarget=False,extract_root_motion=False)
report={'clips':[],'meshes':[]}
for name in ['Attack01','Attack02','Attack03','Attack04','Defend']:
 a=u.load_asset('/Game/RPGHeroSquad/Animation/TinyHero/Anim_'+name+'_TinyHero')
 row={'name':name,'duration':a.get_play_length(),'samples':[]}
 for i in range(33):
  t=a.get_play_length()*i/32;p=u.AnimPoseExtensions.get_anim_pose_at_time(a,t,options);sample={'time':t}
  for bone in ['root','hand_r','hand_l','weapon_r','weapon_l','head','foot_l']:
   tr=u.AnimPoseExtensions.get_bone_pose(p,bone,u.AnimPoseSpaces.WORLD)
   sample[bone]={'position':str(tr.translation),'rotation':str(tr.rotation.rotator())}
   # Source Sword template rotates local +Z by +90 degrees around X.
   # This is a bone-space candidate; socket-local offsets still need preview.
   if bone=='weapon_r':sample['blade_tip_candidate']=str(tr.transform_location(u.Vector(0,-120,0)))
  row['samples'].append(sample)
 report['clips'].append(row)
for path in ['/Game/RPGHeroSquad/Mesh/Character/SK_TinyHeroPolyart','/Game/RPGHeroSquad/Mesh/Weapon/SM_TinyHero_Sword01_Polyart','/Game/RPGHeroSquad/Mesh/Weapon/SM_TinyHero_Shield01_Polyart']:
 a=u.load_asset(path);report['meshes'].append({'path':path,'bounds':str(a.get_bounds())})
for path in ['/Game/RPGHeroSquad/CharacterBP/BP_TinyHeroPolyart','/Game/RoguelikeGeneration/Gameplay/BP_DungeonPlayer']:
 cls=u.load_class(None,path+'.'+path.rsplit('/',1)[-1]+'_C');cdo=u.get_default_object(cls)
 row={'path':path,'components':[]}
 for c in cdo.get_components_by_class(u.SceneComponent):
  cr={'name':c.get_name(),'class':c.get_class().get_name()}
  for k in ['relative_location','relative_rotation','relative_scale3d','skeletal_mesh_asset','static_mesh','anim_class','capsule_half_height','capsule_radius']:
   try:cr[k]=str(c.get_editor_property(k))
   except Exception:pass
  row['components'].append(cr)
 report['meshes'].append(row)
report['weapon_templates']=[]
for c in u.ObjectIterator(u.StaticMeshComponent):
 if 'BP_TinyHeroPolyart' in c.get_path_name():
  report['weapon_templates'].append({'path':c.get_path_name(),'socket':str(c.get_attach_socket_name()),'mesh':str(c.get_editor_property('static_mesh')),'transform':str(c.get_relative_transform())})
(out/'animation-inspection.json').write_text(json.dumps(report,indent=2))
print('SwordShield inspection complete')
