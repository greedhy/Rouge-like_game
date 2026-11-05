"""Merged-mesh two-client regression: no separate weapon instances; capture real animation and corpses."""
import unreal as u,json,time,uuid
from pathlib import Path
out=Path(u.Paths.project_dir())/'Saved/GAAuthoring/MassMergedGoblin'/uuid.uuid4().hex;out.mkdir()
start=time.monotonic();phase='ready';began=start;effects=[];seen=[set(),set()];captures=set();checks={};counts=[]
perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'));old=perf.get_editor_property('bThrottleCPUWhenNotForeground');perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
def finish(error=None):
 u.unregister_slate_post_tick_callback(handle);perf.set_editor_property('bThrottleCPUWhenNotForeground',old)
 for asc,h in effects:
  if u.SystemLibrary.is_valid(asc):asc.remove_active_gameplay_effect(h,-1)
 (out/'runtime.json').write_text(json.dumps({'passed':not error and all(checks.values()),'error':error,'checks':checks,'states':[sorted(x) for x in seen],'counts':counts},indent=2));(out.parent/'latest-runtime.txt').write_text(str(out))
def tick(dt):
 global phase,began
 try:
  now=time.monotonic();assert now-start<90,'Timeout '+phase
  sims=[s for s in u.ObjectIterator(u.PureMassSubsystem) if s.get_world() and 'UEDPIE_' in s.get_world().get_path_name()]
  if len(sims)!=2:return
  sim=next(s for s in sims if u.GameplayStatics.get_game_mode(s.get_world()));w=sim.get_world()
  ds=u.GameplayStatics.get_all_actors_of_class(w,u.DungeonRunDirector)
  if not ds:return
  r=json.loads(ds[0].get_run_report());assert not r['error'],r['error']
  pcs=u.GameplayStatics.get_all_actors_of_class(w,u.DungeonRunController)
  if len(pcs)!=2 or any(not p.get_controlled_pawn() for p in pcs):return
  # Transient fixture health only; PIE teardown discards these values.
  for pc in pcs:
   attrs=pc.player_state.get_component_by_class(u.MyAbilitySystemComponent).get_attribute_set(u.MyAttributeSet)
   for n in ['max_health','health']:
    value=attrs.get_editor_property(n);value.import_text('(BaseValue=1000000,CurrentValue=1000000)');attrs.set_editor_property(n,value)
  for a in u.GameplayStatics.get_all_actors_of_class(w,u.EnemyCharacter):
   c=a.get_controller();brain=c.get_component_by_class(u.BrainComponent) if c else None
   if brain:brain.stop_logic('Mass weapon visual verification')
  if phase=='ready':
   gs=[g for s in sims for g in u.GameplayStatics.get_all_actors_of_class(s.get_world(),u.DungeonGenerationRuntime)]
   if len(gs)!=2 or not all(g.is_local_ready() for g in gs) or r['phase']!='Exploring':return
   ge=u.load_class(None,'/Game/CompositionBatch6/QA/GE_Batch6QAHealth.GE_Batch6QAHealth_C')
   room=next(x for x in r['rooms'] if x['role']=='Combat' and x['phase']=='Discovered')
   for i,p in enumerate(pcs):
    asc=p.player_state.get_component_by_class(u.MyAbilitySystemComponent);effects.append((asc,asc.call_method('BP_ApplyGameplayEffectToSelf',(ge,1.,asc.make_effect_context()))))
    p.get_controlled_pawn().set_actor_location(u.Vector(room['x']+i*120,room['y'],110),False,True)
   phase='observe';began=now;return
  crowds=[u.GameplayStatics.get_all_actors_of_class(s.get_world(),u.PureMassCrowd)[0] for s in sims]
  views=[s.get_snapshots() for s in sims]
  for i,v in enumerate(views):seen[i].update(x.state for x in v)
  age=now-began
  if phase=='observe':
   if r['pending_enemy_spawns']:return
   counts.append([[c.get_rendered_instance_count(),c.get_weapon_instance_count()] for c in crowds])
   for at in [2,3,4,5,6]:
    if age>at and at not in captures:
     captures.add(at)
     for client in [0,1]:u.SystemLibrary.execute_console_command(w,'PureMass.QA.CaptureUI '+str(client)+' MergedGoblin_C'+str(client)+'_'+str(at))
   if age>8:
    checks['both_clients_25_merged_bodies']=all(c.get_weapon_instance_count()==0 and c.get_rendered_instance_count()==25 for c in crowds)
    checks['merged_asset_in_use']=all('_Armed' in x.type.mesh.get_name() and not x.type.weapon_mesh for vs in views for x in vs)
    checks['both_clients_warn_and_attack']=all(4 in x and 2 in x for x in seen)
    d=u.PureMassDamage();d.amount=100000.
    for v in sim.get_snapshots()[:5]:sim.apply_damage(v.id,d)
    phase='death';began=now
  elif phase=='death' and age>.4:
   checks['dead_merged_bodies_removed']=all(c.get_rendered_instance_count()==20 and c.get_weapon_instance_count()==0 for c in crowds)
   checks['corpses_both_clients']=all(c.get_active_death_count()>0 for c in crowds)
   for client in [0,1]:u.SystemLibrary.execute_console_command(w,'PureMass.QA.CaptureUI '+str(client)+' MergedGoblin_C'+str(client)+'_Death')
   phase='expiry';began=now
  elif phase=='expiry' and age>2:
   checks['corpses_expired']=all(c.get_active_death_count()==0 for c in crowds);finish()
 except Exception as e:finish(repr(e))
handle=u.register_slate_post_tick_callback(tick)
