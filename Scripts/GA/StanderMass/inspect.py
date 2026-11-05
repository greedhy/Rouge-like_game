import unreal as u,json
from pathlib import Path
f=Path(u.Paths.project_dir())/'Saved/GAAuthoring/StanderMass';f.mkdir(parents=True,exist_ok=True)
r={'pie':bool(u.EditorLevelLibrary.get_game_world()),'dirty':[x.get_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()],'dirty_maps':[x.get_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()],'types':[hasattr(u,n) for n in ['StanderLiftAbility','StanderShieldBashAbility']]}
r['clips']=[]
o=u.AnimPoseEvaluationOptions(evaluation_type=u.AnimDataEvalType.RAW,should_retarget=False,extract_root_motion=False)
for name in ['Skill1_Float','Skill4_Push']:
 a=u.load_asset('/Game/PlatformAnimation/Animation/01_Sword_Shield/Stander_Sword_Shield_'+name)
 row={'name':name,'length':a.get_play_length(),'samples':[]}
 for i in range(25):
  t=a.get_play_length()*i/24;p=u.AnimPoseExtensions.get_anim_pose_at_time(a,t,o);item={'t':t}
  for bone in ['hand_r','hand_l','root']:
   v=u.AnimPoseExtensions.get_bone_pose(p,bone,u.AnimPoseSpaces.WORLD).translation;item[bone]=[v.x,v.y,v.z]
  row['samples'].append(item)
 r['clips'].append(row)
R='/Game/BluePrints/Characters/Stander/'
mode=u.get_default_object(u.EditorAssetLibrary.load_blueprint_class(R+'BP_StanderGameMode'))
hud=mode.get_editor_property('hud_class');r['hud']=hud.get_path_name()
h=u.get_default_object(hud);r['controllers']={}
for k in ['overlay_widget_controller_class','spell_menu_widget_controller_class']:
 try:
  c=h.get_editor_property(k);r['controllers'][k]={'class':c.get_path_name(),'catalog':str(u.get_default_object(c).get_editor_property('ability_info'))}
 except Exception as e:r['controllers'][k]=str(e)
r['assets']=[str(x.package_name) for x in u.AssetRegistryHelpers.get_asset_registry().get_assets_by_path('/Game',True) if any(y in str(x.asset_name) for y in ['IMC','InputConfig','Shield','Upper','Bash'])]
(f/'inspect.json').write_text(json.dumps(r,indent=2));print('StanderMass inspection complete')

