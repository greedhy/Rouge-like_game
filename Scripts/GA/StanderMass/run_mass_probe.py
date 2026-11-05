import unreal as u,json,time
from pathlib import Path
root=Path(u.Paths.project_dir());exec((root/'Scripts/GA/StanderMass/run_actor_probe.py').read_text().split("cases=['Combo'")[0])
sm=next(x for x in u.ObjectIterator(u.PureMassSubsystem) if x.get_world()==w);sm.enable()
source=u.load_asset('/Game/PureMass/DA_MassQAGoblin');assert source
mt=u.new_object(u.PureMassMonsterType)
for k in ['mesh','animation_provider','mesh_offset','idle_animation','run_animation','attack_animation','radius','half_height'] :mt.set_editor_property(k,source.get_editor_property(k))
for k,v in dict(health=1000.,speed=0.,attack_damage=0.,armor=0.,block_chance=0.,attack_range=200.,attack_period=.2,attack_windup_seconds=0.).items():mt.set_editor_property(k,v)
classes={n:u.load_class(None,'/Game/BluePrints/Characters/Stander/GA_Stander_'+n+'.GA_Stander_'+n+'_C') for n in ['Combo','Guard','Spin','Dash','Lift','ShieldBash']}
for a in targets:a.set_actor_location(u.Vector(1200,1200,89),False,True)
cases=['Combo','Guard','Spin','Dash','Lift','ShieldBash','Lift_cancel','ShieldBash_cancel','Lift_lethal','ShieldBash_lethal'];index=0;case=None;began=time.monotonic();report={'status':'running','cases':[]};out=root/'Saved/GAAuthoring/StanderMass/mass-runtime.json';anim=p.get_component_by_class(u.SkeletalMeshComponent).get_anim_instance()
def tick(dt):
 global index,case
 try:
  now=u.GameplayStatics.get_time_seconds(w)
  if case is None:
   name=cases[index];skill=name.split('_')[0];asc.remove_active_gameplay_effect_by_source_effect(u.SwordTimedEffect,asc,-1);assign(p,'mana',100.);assign(p,'health',1000.);assign(p,'max_health',1000.);assign(p,'block_chance',100.)
   p.set_actor_location(u.Vector(0,0,98),False,True);p.get_component_by_class(u.CharacterMovementComponent).set_movement_mode(u.MovementMode.MOVE_WALKING)
   mt.set_editor_property('health',1. if 'lethal' in name else 1000.);mt.set_editor_property('attack_damage',10. if skill=='Guard' else 0.)
   assert asc.try_activate_ability_by_class(classes[skill]),'Rejected '+name
   forward=p.get_actor_forward_vector();loc=p.get_actor_location()+forward*(600 if skill=='Dash' else 120);entity=sm.spawn_monster(mt,loc,'None');assert entity>0
   case={'name':name,'start':now,'entity':entity,'forward':[forward.x,forward.y,forward.z],'samples':[],'cancelled':False}
  elapsed=now-case['start'];name=case['name']
  if '_cancel' in name and elapsed>.04 and not case['cancelled']:anim.montage_stop(0.);case['cancelled']=True
  view=next((x for x in sm.get_snapshots() if x.id==case['entity']),None);velocities=[]
  for mesh in u.ObjectIterator(u.SkeletalMeshComponent):
   if mesh.get_world()==w and isinstance(mesh.get_owner(),u.PureMassCrowd) and mesh.is_simulating_physics():
    vel=mesh.get_physics_linear_velocity('Hips');velocities.append([vel.x,vel.y,vel.z])
  case['samples'].append({'t':elapsed,'health':view.health if view else 0.,'mana':val(p,'mana'),'player_hp':val(p,'health'),'velocities':velocities,'montage':str(anim.get_current_active_montage())})
  if elapsed>2.:
   loss=(1. if 'lethal' in name else 1000.)-case['samples'][-1]['health'];skill=name.split('_')[0]
   passed=loss==0 if '_cancel' in name else loss>0
   if skill=='Guard':passed=passed and any('CounterStrike' in x['montage'] for x in case['samples']) and val(p,'health')<1000.
   if 'lethal' in name:passed=passed and any(x['velocities'] for x in case['samples'])
   case.update(loss=loss,mana_spent=100.-val(p,'mana'),passed=passed)
   report['cases'].append(case);sm.remove_monster(case['entity'],u.PureMassRemovalReason.RECYCLED);case=None;index+=1
   if index==len(cases):report.update(status='complete',passed=all(x['passed'] for x in report['cases']))
  if time.monotonic()-began>120:raise RuntimeError('Timeout')
 except Exception as e:report.update(status='error',passed=False,error=str(e))
 if report['status']!='running':u.unregister_slate_post_tick_callback(handle)
 out.write_text(json.dumps(report,indent=2))
handle=u.register_slate_post_tick_callback(tick)
