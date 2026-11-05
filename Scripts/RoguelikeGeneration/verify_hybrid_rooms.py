"""Two-client real-room integration; direct server damage tests lifecycle, not input RPC."""
import unreal as u,json,time,uuid
from pathlib import Path
out=Path(u.Paths.project_dir())/'Saved/GAAuthoring/DungeonHybrid'/uuid.uuid4().hex;out.mkdir(parents=True)
report={'passed':False,'checks':{},'snapshots':[],'phase':'ready'}
start=time.monotonic();phase='ready';last=start;room=None;wave_seen=set();ids=set();peak=[0,0]
def check(k,v):report['checks'][k]=bool(v);assert v,k
def finish(error=None):
 u.unregister_slate_post_tick_callback(handle);report.update(passed=error is None,error=error)
 (out/'rooms.json').write_text(json.dumps(report,indent=2));(out.parent/'latest-rooms.txt').write_text(str(out))
def kill_actor(a,pc):
 p=u.DamageEffectParams();tag=u.GameplayTag();tag.import_text('(TagName="Damage.Physics")')
 for k,v in {'source_ability_system_component':pc.player_state.get_component_by_class(u.MyAbilitySystemComponent),'target_ability_system_component':a.get_component_by_class(u.MyAbilitySystemComponent),'damage_gameplay_effect_class':u.load_class(None,'/Game/BluePrints/AbilitySystem/Player/Effect/GE_Damage.GE_Damage_C'),'damage_type':tag,'base_damage':1000000.,'ability_level':1.,'debuff_chance':0.}.items():p.set_editor_property(k,v)
 u.MyAbilitySystemLibrary.apply_damage_effect(p)
def tick(dt):
 global phase,last,room
 try:
  now=time.monotonic();assert now-start<120,'Timeout '+phase
  sims=[s for s in u.ObjectIterator(u.PureMassSubsystem) if s.get_world() and 'UEDPIE_' in s.get_world().get_path_name()]
  servers=[s for s in sims if u.GameplayStatics.get_game_mode(s.get_world())]
  if len(servers)!=1 or len(sims)!=2:return
  sim=servers[0];w=sim.get_world();directors=u.GameplayStatics.get_all_actors_of_class(w,u.DungeonRunDirector)
  if not directors:return
  d=directors[0];r=json.loads(d.get_run_report());assert not r['error'],r['error']
  pcs=u.GameplayStatics.get_all_actors_of_class(w,u.DungeonRunController)
  if len(pcs)!=2 or any(not p.get_controlled_pawn() for p in pcs):return
  crowds=[u.GameplayStatics.get_all_actors_of_class(s.get_world(),u.PureMassCrowd) for s in sims]
  if not all(len(c)==1 for c in crowds):return
  for i,c in enumerate(crowds):peak[i]=max(peak[i],c[0].get_active_death_count())
  if phase=='ready':
   if r['phase']!='Exploring':return
   generations=[g for s in sims for g in u.GameplayStatics.get_all_actors_of_class(s.get_world(),u.DungeonGenerationRuntime)]
   if len(generations)!=2 or not all(g.is_local_ready() for g in generations):return
   room=next(x for x in r['rooms'] if x['role']=='Combat' and x['phase']=='Discovered')
   for i,p in enumerate(pcs):p.get_controlled_pawn().set_actor_location(u.Vector(room['x']+i*100,room['y'],110),False,True)
   phase='spawn';last=now;return
  paths={t['path'] for t in r['targets'] if t['kind']=='actor'}
  active=[a for a in u.GameplayStatics.get_all_actors_of_class(w,u.EnemyCharacter) if a.get_path_name() in paths]
  # Stop Actor attacks only for controlled ledger test; Mass remains live and warning-enabled.
  for a in active:
   c=a.get_controller();brain=c.get_component_by_class(u.BrainComponent) if c else None
   if brain:brain.stop_logic('Hybrid ledger verification')
  if phase=='spawn':
   if r['phase']!='Encounter' or r['pending_enemy_spawns'] or now-last<1:return
   current=next(x for x in r['rooms'] if x['index']==room['index']);wave=current['wave']
   views=sim.get_snapshots()
   if not all(c[0].get_rendered_instance_count()==len(views) for c in crowds):return
   check('wave'+str(wave)+'_mass25',len(views)==25)
   check('wave'+str(wave)+'_health10',all(v.max_health==10 and v.health==10 for v in views))
   check('wave'+str(wave)+'_actor_species_preserved',bool(active) and all('GoblinSpear' not in a.get_class().get_path_name() for a in active))
   check('wave'+str(wave)+'_client_replication',all(len(s.get_snapshots())==25 for s in sims))
   wave_seen.add(wave);ids.update(v.id for v in views);report['snapshots'].append(r)
   damage=u.PureMassDamage();damage.set_editor_property('amount',1000000.)
   for v in views:sim.apply_damage(v.id,damage)
   phase='corpses';last=now;return
  if phase=='corpses' and now-last>.25:
   check('bounded_corpses',all(c[0].get_allocated_death_count()<=12 for c in crowds))
   check('kill_cosmetic_both_clients',all(p>0 for p in peak))
   check('actors_hold_room_open',r['phase']=='Encounter' and bool(active))
   u.SystemLibrary.execute_console_command(w,'PureMass.QA.CaptureUI 1 DungeonHybrid_Deaths')
   for a in active:kill_actor(a,pcs[0])
   phase='advance';last=now;return
  if phase=='advance' and now-last>2:
   if r['phase']=='Encounter':phase='spawn';last=now;return
   if r['phase']=='Exploring':
    check('exactly50_mass_per_room',len(ids)==50 and wave_seen=={1,2})
    check('room_cleared',next(x for x in r['rooms'] if x['index']==room['index'])['phase']=='Cleared')
    check('no_living_mass',all(len(s.get_snapshots())==0 for s in sims))
    check('corpses_expired',all(c[0].get_active_death_count()==0 for c in crowds))
    report['peak_corpses']=peak;report['snapshots'].append(r);finish()
 except Exception as e:finish(repr(e))
handle=u.register_slate_post_tick_callback(tick)
