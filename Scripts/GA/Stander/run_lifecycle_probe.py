"""Real player GA boundary/cancel/down/revive checks in the disposable probe world."""
import unreal as u,json,time
from pathlib import Path
root=Path(u.Paths.project_dir())
exec((root/'Scripts/GA/Stander/run_combat_probe.py').read_text().split("cases=['Combo'")[0])
wall=next(a for a in u.GameplayStatics.get_all_actors_of_class(w,u.StaticMeshActor) if a.actor_has_tag('SwordWall'))
wall.static_mesh_component.set_mobility(u.ComponentMobility.MOVABLE)
anim=p.get_component_by_class(u.SkeletalMeshComponent).get_anim_instance()
movement=p.get_component_by_class(u.CharacterMovementComponent)
cases=['behind','wall','interrupt','no_mana','guard_interrupt','down_revive'];index=0;case=None;started=time.monotonic()
report={'status':'running','cases':[]};out=root/'Saved/GAAuthoring/Stander/lifecycle-runtime.json'
u.GameplayStatics.set_global_time_dilation(w,.5)
def kill():
 params=u.DamageEffectParams();tag=u.GameplayTag();tag.import_text('(TagName="Damage.Physics")')
 for k,v in {'source_ability_system_component':targets[0].get_component_by_class(u.MyAbilitySystemComponent),'target_ability_system_component':asc,'damage_gameplay_effect_class':u.load_class(None,'/Game/BluePrints/AbilitySystem/Player/Effect/GE_Damage.GE_Damage_C'),'damage_type':tag,'base_damage':1000000.,'ability_level':1.,'debuff_chance':0.}.items():params.set_editor_property(k,v)
 u.MyAbilitySystemLibrary.apply_damage_effect(params)
def tick(dt):
 global index,case
 try:
  now=u.GameplayStatics.get_time_seconds(w)
  if case is None:
   name=cases[index];wall.set_actor_enable_collision(False)
   # Cooldown removal is limited to this disposable test world.
   asc.remove_active_gameplay_effect_by_source_effect(u.SwordTimedEffect,asc,-1)
   assign(p,'mana',0. if name=='no_mana' else 100.)
   ability='Guard' if name in ['guard_interrupt','down_revive'] else ('Spin' if name=='no_mana' else 'Combo')
   case={'name':name,'start':now,'hp':val(targets[0],'health'),'accepted':asc.try_activate_ability_by_class(classes[ability]),'acted':False}
   forward=p.get_actor_forward_vector();origin=p.get_actor_location()
   targets[0].set_actor_location(origin+forward*(-120 if name=='behind' else 120),False,True)
   if name=='wall':
    wall.set_actor_rotation(forward.rotator(),False);wall.set_actor_location(origin+forward*60,False,True);wall.set_actor_enable_collision(True)
  elapsed=now-case['start'];name=case['name']
  if elapsed>.04 and not case['acted']:
   case['acted']=True
   if name in ['interrupt','guard_interrupt']:anim.montage_stop(0.)
   if name=='down_revive':
    case['guard_before']=asc.has_matching_gameplay_tag(guard_tag);kill()
    case['down']=p.is_dungeon_downed();case['guard_removed']=not asc.has_matching_gameplay_tag(guard_tag)
    case['down_rejects']=not asc.try_activate_ability_by_class(classes['Combo'])
  if elapsed>1.6:
   case['damage']=case['hp']-val(targets[0],'health')
   case['guard_left']=asc.has_matching_gameplay_tag(guard_tag)
   if name=='down_revive':
    case['revived']=p.revive_for_dungeon(u.Vector(0,0,98),.5)
    case['same_asc']=p.get_editor_property('player_state').get_component_by_class(u.MyAbilitySystemComponent)==asc
    case['post_revive_attack']=asc.try_activate_ability_by_class(classes['Combo']);anim.montage_stop(0.)
    case['passed']=all(case[k] for k in ['guard_before','down','guard_removed','down_rejects','revived','same_asc','post_revive_attack'])
   else:
    case['passed']=case['accepted']==(name!='no_mana') and case['damage']==0 and not case['guard_left'] and movement.movement_mode!=u.MovementMode.MOVE_NONE
   report['cases'].append(case);index+=1;case=None
   if index==len(cases):report.update(status='complete',passed=all(c['passed'] for c in report['cases']))
  if time.monotonic()-started>90:raise RuntimeError('Timeout')
 except Exception as e:report.update(status='error',passed=False,error=str(e))
 if report['status']!='running':
  wall.set_actor_enable_collision(False);u.GameplayStatics.set_global_time_dilation(w,1.);u.unregister_slate_post_tick_callback(handle)
 out.write_text(json.dumps(report,indent=2))
handle=u.register_slate_post_tick_callback(tick)
