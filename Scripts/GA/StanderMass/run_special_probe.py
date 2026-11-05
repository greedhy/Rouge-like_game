"""Reactive counter and swept dash contracts against real damage and geometry."""
import unreal as u,json,time
from pathlib import Path
root=Path(u.Paths.project_dir())
exec((root/'Scripts/GA/Stander/run_combat_probe.py').read_text().split("cases=['Combo'")[0])
classes['Dash']=u.load_class(None,'/Game/BluePrints/Characters/Stander/GA_Stander_Dash.GA_Stander_Dash_C')
wall=next(a for a in u.GameplayStatics.get_all_actors_of_class(w,u.StaticMeshActor) if a.actor_has_tag('SwordWall'));wall.static_mesh_component.set_mobility(u.ComponentMobility.MOVABLE)
anim=p.get_component_by_class(u.SkeletalMeshComponent).get_anim_instance();movement=p.get_component_by_class(u.CharacterMovementComponent)
cases=['counter','dash_clear','dash_wall','dash_edge','dash_interrupt'];index=0;case=None;began=time.monotonic()
report={'status':'running','cases':[]};out=root/'Saved/GAAuthoring/StanderMass/actor-special-runtime.json'
u.GameplayStatics.set_global_time_dilation(w,.5)
def tick(dt):
 global index,case
 try:
  now=u.GameplayStatics.get_time_seconds(w)
  if case is None:
   name=cases[index];wall.set_actor_enable_collision(False);asc.remove_active_gameplay_effect_by_source_effect(u.SwordTimedEffect,asc,-1)
   assign(p,'mana',100.);p.set_actor_location(u.Vector(0,0,98),False,True);movement.set_movement_mode(u.MovementMode.MOVE_WALKING)
   accepted=asc.try_activate_ability_by_class(classes['Guard' if name=='counter' else 'Dash']);assert accepted,'Rejected '+name
   forward=p.get_actor_forward_vector()
   # Use an edge in the locked direction, preserving the same facing.
   if name=='dash_edge':p.set_actor_location(forward*1450+u.Vector(0,0,98),False,True)
   origin=p.get_actor_location()
   for a in targets:a.set_actor_location(origin+forward*1100,False,True)
   targets[0].set_actor_location(origin+forward*(120 if name=='counter' else 600),False,True)
   if name=='dash_wall':
    wall.set_actor_rotation(forward.rotator(),False);wall.set_actor_location(origin+forward*250,False,True);wall.set_actor_enable_collision(True)
   case={'name':name,'start':now,'origin':[origin.x,origin.y,origin.z],'hp_before':val(targets[0],'health'),'player_hp':val(p,'health'),'samples':[],'acted':False}
  elapsed=now-case['start'];name=case['name']
  if elapsed>.12 and not case['acted']:
   case['acted']=True
   if name=='counter':
    params=u.DamageEffectParams();tag=u.GameplayTag();tag.import_text('(TagName="Damage.Physics")')
    for k,v in dict(source_ability_system_component=targets[0].get_component_by_class(u.MyAbilitySystemComponent),target_ability_system_component=asc,damage_gameplay_effect_class=u.load_class(None,'/Game/BluePrints/AbilitySystem/Player/Effect/GE_Damage.GE_Damage_C'),damage_type=tag,base_damage=10.,ability_level=1.,debuff_chance=0.).items():params.set_editor_property(k,v)
    u.MyAbilitySystemLibrary.apply_damage_effect(params)
   elif name=='dash_interrupt':anim.montage_stop(0.)
  m=anim.get_current_active_montage();loc=p.get_actor_location();origin=u.Vector(*case['origin'])
  case['samples'].append({'t':elapsed,'distance':(loc-origin).length(),'hp':val(targets[0],'health'),'montage':m.get_name() if m else None,'guard':asc.has_matching_gameplay_tag(guard_tag)})
  if elapsed>1.8:
   ss=case['samples'];distance=ss[-1]['distance'];loss=case['hp_before']-ss[-1]['hp'];events=sum(a['hp']>b['hp'] for a,b in zip(ss,ss[1:]))
   case.update(distance=distance,loss=loss,damage_events=events,mana_spent=100.-val(p,'mana'))
   if name=='counter':passed=loss>0 and events==1 and any(x['montage']=='AM_Stander_CounterStrike' for x in ss) and val(p,'health')<case['player_hp'] and not ss[-1]['guard']
   elif name=='dash_clear':passed=abs(distance-500)<3 and loss>0 and events==1 and case['mana_spent']==12.
   elif name=='dash_wall':passed=100<distance<250 and loss==0
   elif name=='dash_edge':passed=distance<400 and loss==0
   else:passed=distance<100 and loss==0
   case['passed']=passed and movement.movement_mode!=u.MovementMode.MOVE_NONE
   report['cases'].append(case);index+=1;case=None
   if index==len(cases):report.update(status='complete',passed=all(x['passed'] for x in report['cases']))
  if time.monotonic()-began>90:raise RuntimeError('Timeout')
 except Exception as e:report.update(status='error',passed=False,error=str(e))
 if report['status']!='running':
  wall.set_actor_enable_collision(False);u.GameplayStatics.set_global_time_dilation(w,1.);u.unregister_slate_post_tick_callback(handle)
 out.write_text(json.dumps(report,indent=2))
handle=u.register_slate_post_tick_callback(tick)
