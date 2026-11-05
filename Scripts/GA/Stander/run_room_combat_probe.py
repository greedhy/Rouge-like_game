"""Clear an actual generated combat room using the new GA and low-health test targets."""
import unreal as u,json,time
from pathlib import Path
w=u.EditorLevelLibrary.get_game_world();assert 'L_RoguelikeRun' in w.get_path_name()
d=next(iter(u.GameplayStatics.get_all_actors_of_class(w,u.DungeonRunDirector)))
p=u.GameplayStatics.get_player_pawn(w,0);assert isinstance(p,u.StanderPlayer)
asc=p.get_editor_property('player_state').get_component_by_class(u.MyAbilitySystemComponent)
initial=json.loads(d.get_run_report());room=next(r for r in initial['rooms'] if r['role']=='Combat' and r['phase']=='Discovered')
p.set_actor_location(u.Vector(room['x'],room['y'],100),False,True)
cls=u.load_class(None,'/Game/BluePrints/Characters/Stander/GA_Stander_Combo.GA_Stander_Combo_C')
out=Path(u.Paths.project_dir())/'Saved/GAAuthoring/Stander/room-combat-runtime.json'
report={'status':'running','attacks':0,'prepared':[],'snapshots':[]};started=time.monotonic();last=-100;seen=set()
def tick(dt):
 global last
 try:
  now=u.GameplayStatics.get_time_seconds(w);r=json.loads(d.get_run_report())
  if r['error']:raise RuntimeError(r['error'])
  by_path={a.get_path_name():a for a in u.GameplayStatics.get_all_actors_of_class(w,u.EnemyCharacter)}
  active=[by_path[t['path']] for t in r['targets'] if t['kind']=='actor' and t['path'] in by_path]
  for a in active:
   identity=a.get_path_name()+':'+str(a.get_dungeon_pool_lease())
   if identity not in seen:
    seen.add(identity);report['prepared'].append(identity)
    c=a.get_controller();brain=c.get_component_by_class(u.BrainComponent) if c else None
    if brain:brain.stop_logic('Stander isolated room test')
    anim=a.get_component_by_class(u.SkeletalMeshComponent).get_anim_instance()
    if anim:anim.montage_stop(0.)
    a.get_component_by_class(u.CharacterMovementComponent).disable_movement()
    a.get_component_by_class(u.CapsuleComponent).set_collision_response_to_channel(u.CollisionChannel.ECC_PAWN,u.CollisionResponseType.ECR_IGNORE)
    at=a.get_component_by_class(u.MyAbilitySystemComponent).get_attribute_set(u.MyAttributeSet)
    h=at.get_editor_property('health');h.import_text('(BaseValue=1,CurrentValue=1)');at.set_editor_property('health',h)
  if active and now-last>.8:
   if asc.try_activate_ability_by_class(cls):report['attacks']+=1;last=now
  forward=p.get_actor_forward_vector();origin=p.get_actor_location()
  for a in active:a.set_actor_location(origin+forward*110,False,True)
  rr=next(x for x in r['rooms'] if x['index']==room['index'])
  if rr['phase']=='Cleared':
   report['snapshots'].append(r);report.update(status='complete',passed=report['attacks']>0 and len(seen)>0 and r['phase']=='Exploring' and rr['remaining_required']==0)
  if time.monotonic()-started>90:raise RuntimeError('Room clear timeout')
 except Exception as e:report.update(status='error',passed=False,error=str(e))
 if report['status']!='running':u.unregister_slate_post_tick_callback(handle)
 out.write_text(json.dumps(report,indent=2))
handle=u.register_slate_post_tick_callback(tick)
