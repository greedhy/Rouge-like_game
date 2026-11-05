import unreal as u,json,time
from pathlib import Path
pcs=[p for p in u.ObjectIterator(u.PureMassPlayerController) if p.get_world() and 'UEDPIE_' in p.get_world().get_path_name()]
remote=next(p for p in pcs if not p.is_local_controller());host=next(p for p in pcs if p.is_local_controller() and p.get_local_role()==u.NetRole.ROLE_AUTHORITY);client=next(p for p in pcs if p.is_local_controller() and p.get_local_role()==u.NetRole.ROLE_AUTONOMOUS_PROXY)
sp=next(s for s in u.ObjectIterator(u.DamageNumberPool) if s.get_world()==host.get_world());cp=next(s for s in u.ObjectIterator(u.DamageNumberPool) if s.get_world()==client.get_world());sim=next(s for s in u.ObjectIterator(u.PureMassSubsystem) if s.get_world()==host.get_world())
checks={};phase=0;start=time.monotonic();sp.reset_counters();cp.reset_counters()
def hit(pc):
 pawn=pc.get_controlled_pawn();ident=sim.spawn_monster(u.load_asset('/Game/Performance/DA_BenchmarkGoblin'),pawn.get_actor_location()+u.Vector(100,0,0));d=u.PureMassDamage();d.import_text('(Source="'+pawn.get_path_name()+'")');d.amount=10000.;sim.apply_damage(ident,d)
hit(remote)
def tick(dt):
 global phase,start
 try:
  age=time.monotonic()-start;s=json.loads(sp.get_report());c=json.loads(cp.get_report())
  if phase==0 and age>.4:
   checks['remote_only']=c['active']==1 and s['active']==0
   u.SystemLibrary.execute_console_command(host,'PureMass.QA.CaptureUI 1 DamageNumbers_Remote');phase=1
  elif phase==1 and age>2:
   checks['remote_expired']=c['active']==0;hit(host);phase=2;start=time.monotonic()
  elif phase==2 and age>.4:
   checks['host_only']=s['active']==1 and c['active']==0;phase=3
  elif phase==3 and age>2:
   checks['host_expired']=s['active']==0;hit(remote);phase=4;start=time.monotonic()
  elif phase==4 and age>.4:
   checks['remote_reused']=c['reused']==1 and c['created']==1;phase=5
  elif phase==5 and age>2:
   checks['final_expired']=s['active']==0 and c['active']==0
   source=remote.player_state.get_component_by_class(u.MyAbilitySystemComponent);target=host.player_state.get_component_by_class(u.MyAbilitySystemComponent)
   params=u.DamageEffectParams();params.set_editor_property('world_context_object',remote.get_controlled_pawn());params.set_editor_property('source_ability_system_component',source);params.set_editor_property('target_ability_system_component',target);params.set_editor_property('damage_gameplay_effect_class',u.load_class(None,'/Game/BluePrints/AbilitySystem/Player/Effect/GE_Damage.GE_Damage_C'));params.set_editor_property('base_damage',1.)
   tag=u.GameplayTag();tag.import_text('(TagName="Damage.Fire")');params.set_editor_property('damage_type',tag)
   assert u.DamageNumberTestLibrary.schedule_damage_pair(params)
   phase=6;start=time.monotonic()
  elif phase==6 and age>.4:
   if c['actor_inputs']<2 and age<3:return
   checks['actor_merge_remote_only']=c['actor_inputs']==2 and c['merged']==1 and c['active']==1 and s['active']==0;phase=7
  elif phase==7 and age>2:
   checks['actor_merge_expired']=c['active']==0 and c['pending']==0;finish()
 except Exception as e:finish(repr(e))
def finish(error=None):
 u.unregister_slate_post_tick_callback(handle);Path(u.Paths.project_dir(),'Saved/Performance/DamageNumbers/network.json').write_text(json.dumps({'checks':checks,'error':error,'server':json.loads(sp.get_report()),'client':json.loads(cp.get_report())},indent=2))
handle=u.register_slate_post_tick_callback(tick)

