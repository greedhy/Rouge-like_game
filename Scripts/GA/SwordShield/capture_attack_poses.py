import unreal as u,json,time
from pathlib import Path
w=u.EditorLevelLibrary.get_game_world();p=u.GameplayStatics.get_player_pawn(w,0);pc=u.GameplayStatics.get_player_controller(w,0)
asc=p.get_editor_property('player_state').get_component_by_class(u.MyAbilitySystemComponent)
mesh=p.get_component_by_class(u.SkeletalMeshComponent);anim=mesh.get_anim_instance()
arm=p.get_component_by_class(u.SpringArmComponent);old_length=arm.target_arm_length;arm.target_arm_length=420.
u.SystemLibrary.execute_console_command(w,'viewmode unlit',pc)
names=['Combo','Combo','Combo','Spin'];index=0;start=None;captured=False;wall_start=time.monotonic()
report={'status':'running','poses':[]};folder=Path(u.Paths.project_dir())/'Saved/GAAuthoring/SwordShield'
u.GameplayStatics.set_global_time_dilation(w,.15)
def tick(dt):
 global index,start,captured
 try:
  now=u.GameplayStatics.get_time_seconds(w)
  if start is None:
   name=names[index];cls=u.load_class(None,'/Game/BluePrints/Characters/SwordShield/GA_Sword_'+name+'.GA_Sword_'+name+'_C')
   assert asc.try_activate_ability_by_class(cls),'Rejected capture ability'
   start=now;captured=False
  m=anim.get_current_active_montage()
  contact=[.15,.2,.2166667,.2][index]
  if not captured and m and anim.montage_get_position(m)>=contact:
   filename=str(folder/('pose-'+str(index+1)+'.png')).replace('\\','/')
   u.SystemLibrary.execute_console_command(w,'HighResShot 1280x720 filename="'+filename+'"',pc)
   report['poses'].append({'montage':m.get_name(),'contact':contact,'position':anim.montage_get_position(m),'sword_location':str(p.get_editor_property('sword_visual').get_world_location()),'screenshot':filename});captured=True
  if now-start>.7:
   index+=1;start=None
   if index==len(names):report.update(status='complete',passed=len(report['poses'])==4)
  if time.monotonic()-wall_start>60:raise RuntimeError('Capture timeout')
 except Exception as e:report.update(status='error',passed=False,error=str(e))
 if report['status']!='running':
  arm.target_arm_length=old_length;u.GameplayStatics.set_global_time_dilation(w,1.);u.unregister_slate_post_tick_callback(handle)
 (folder/'poses.json').write_text(json.dumps(report,indent=2))
handle=u.register_slate_post_tick_callback(tick)
