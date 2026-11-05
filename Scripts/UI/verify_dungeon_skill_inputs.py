"""Two-client Dungeon PIE: actual startup mapping, HUD brushes, remote input casts.
Transient QA Actor only. Does not equip, replace loadout, alter progression or save assets.
"""
import unreal as u,json,time,uuid
from pathlib import Path
_ds_out=Path(u.Paths.project_dir(),'Saved/GAAuthoring/DungeonHybridFix',uuid.uuid4().hex);_ds_out.mkdir()
_ds={'phase':'setup','start':time.monotonic(),'mapping':[],'hud':[],'casts':[],'index':0,'passed':False}
_ds_owned=[]
_ds_health=[]
_ds_perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
_ds_old_throttle=_ds_perf.get_editor_property('bThrottleCPUWhenNotForeground')
_ds_perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
def _ds_write():(_ds_out/'input-pie.json').write_text(json.dumps(_ds,indent=2),encoding='utf8')
def _ds_phase(p):_ds.update(phase=p,at=time.monotonic());_ds_write()
def _ds_end(error=None):
 u.unregister_slate_post_tick_callback(_ds_handle)
 _ds_perf.set_editor_property('bThrottleCPUWhenNotForeground',_ds_old_throttle)
 for asc,handle in _ds_health:
  if u.SystemLibrary.is_valid(asc):asc.remove_active_gameplay_effect(handle,-1)
 for a in _ds_owned:
  if u.SystemLibrary.is_valid(a):a.destroy_actor()
 _ds['checks']={'FireBolt_projectile_observed':any(c['projectiles'] for c in _ds['casts'] if 'GA_FireBolt.' in c['ability']['class']),'Burst_runtime_observed':any(c['runtimes'] for c in _ds['casts'] if 'GA_Prototype_Burst.' in c['ability']['class']),'Beam_endpoints_observed':any(c['samples'] for c in _ds['casts'])}
 samples=[v for c in _ds['casts'] for v in c['samples']]
 _ds['checks']['Beam_origin_tracks_owner']=bool(samples) and all(sum((v['start'][i]-v['owner'][i]-(30 if i==2 else 0))**2 for i in range(3))<100 for v in samples)
 _ds['checks']['Beam_end_matches_target']=bool(samples) and all(sum((v['end'][i]-v['target'][i])**2 for i in range(3))<10000 for v in samples)
 _ds['checks']['Beam_visual_both_worlds']=len({v['world'] for v in samples if v['visual_active']})==2
 _ds.update(error=error,passed=error is None and all(_ds['checks'].values()));_ds_write();u.log('DUNGEON_SKILL_TEST '+str(_ds_out))
def _ds_vec(v):return [v.x,v.y,v.z]
def _ds_tick(dt):
 try:
  assert time.monotonic()-_ds['start']<70,'Timeout '+_ds['phase']
  pcs=[p for p in u.ObjectIterator(u.MyPlayerController) if 'UEDPIE_' in p.get_path_name() and p.get_controlled_pawn()]
  cp=next((p for p in pcs if p.is_local_controller() and p.get_local_role()==u.NetRole.ROLE_AUTONOMOUS_PROXY),None)
  sp=next((p for p in pcs if not p.is_local_controller() and p.get_local_role()==u.NetRole.ROLE_AUTHORITY),None)
  if not cp or not sp:return
  sw,cw=sp.get_world(),cp.get_world();age=time.monotonic()-_ds.get('at',time.monotonic())
  if _ds['phase']=='setup':
   generations=[g for w in [sw,cw] for g in u.GameplayStatics.get_all_actors_of_class(w,u.DungeonGenerationRuntime)]
   if len(generations)!=2 or not all(g.is_local_ready() for g in generations):return
   directors=u.GameplayStatics.get_all_actors_of_class(sw,u.DungeonRunDirector)
   if not directors:return
   report=json.loads(directors[0].get_run_report())
   assert not report['error'],report['error']
   if not _ds.get('health_applied'):
    for controller in u.GameplayStatics.get_all_actors_of_class(sw,u.DungeonRunController):
     asc=controller.player_state.get_component_by_class(u.MyAbilitySystemComponent)
     ge=u.load_class(None,'/Game/CompositionBatch6/QA/GE_Batch6QAHealth.GE_Batch6QAHealth_C')
     handle=asc.call_method('BP_ApplyGameplayEffectToSelf',(ge,1.,asc.make_effect_context()));_ds_health.append((asc,handle))
    _ds['health_applied']=True
   if report['phase']=='Exploring':
    room=next(r for r in report['rooms'] if r['role']=='Combat' and r['phase']=='Discovered')
    for i,controller in enumerate(u.GameplayStatics.get_all_actors_of_class(sw,u.DungeonRunController)):
     controller.get_controlled_pawn().set_actor_location(u.Vector(room['x']+i*100,room['y'],110),False,True)
    return
   if report['phase']!='Encounter' or report['pending_enemy_spawns']:return
   paths={t['path'] for t in report['targets'] if t['kind']=='actor'}
   targets=[a for a in u.GameplayStatics.get_all_actors_of_class(sw,u.EnemyCharacter) if a.get_path_name() in paths]
   if not targets:return
   _ds['target_path']=targets[0].get_path_name()
   _ds['initial_encounter']=report
   pawn=sp.get_controlled_pawn();_ds['pawn']=pawn.get_path_name();_ds['worlds']=[sw.get_path_name(),cw.get_path_name()]
   for cls in pawn.get_editor_property('startup_ability'):
    c=u.get_default_object(cls);_ds['mapping'].append({'class':cls.get_path_name(),'input':c.get_editor_property('startup_input_tag').export_text()})
   for w in u.ObjectIterator(u.Image):
    if not w.get_path_name().startswith('/Game/') and 'SpellIcon' in w.get_name():
     _ds['hud'].append({'path':w.get_path_name(),'brush':w.get_editor_property('brush').export_text(),'opacity':w.get_render_opacity()})
   enemy=targets[0]
   for controlled in targets:
    controller=controlled.get_controller();brain=controller.get_component_by_class(u.BrainComponent) if controller else None
    if brain:brain.stop_logic('Temporary dungeon skill verification')
    movement=controlled.get_component_by_class(u.CharacterMovementComponent)
    if movement:movement.disable_movement()
   asc=enemy.get_component_by_class(u.MyAbilitySystemComponent);ge=u.load_class(None,'/Game/CompositionBatch6/QA/GE_Batch6QAHealth.GE_Batch6QAHealth_C')
   handle=asc.call_method('BP_ApplyGameplayEffectToSelf',(ge,1.,asc.make_effect_context()));_ds_health.append((asc,handle))
   # Use the director's living managed enemy; no fixture escapes encounter filtering.
   combat=next(r for r in report['rooms'] if r['phase']=='Active' or (r['role']=='Combat' and r['wave']>0))
   center=u.Vector(combat['x'],combat['y'],110)
   pawn.set_actor_location(center,False,True)
   enemy.set_actor_location(center+u.Vector(220,0,0),False,True)
   u.get_default_object(u.load_class(None,'/Script/UMG.WidgetBlueprintLibrary')).call_method('SetInputMode_GameOnly',(cp,False))
   _ds['all_mapping']=list(_ds['mapping'])
   _ds['mapping']=[m for name in ['GA_Prototype_Beam.','GA_FireBolt.','GA_Prototype_Burst.'] for m in _ds['mapping'] if name in m['class']]
   _ds_phase('settle');return
  enemy=next((a for a in u.GameplayStatics.get_all_actors_of_class(sw,u.EnemyCharacter) if a.get_path_name()==_ds['target_path']),None)
  assert enemy,'Managed target no longer alive'
  if _ds['phase']=='settle':
   if age<1:return
   targets=[a for a in u.GameplayStatics.get_all_actors_of_class(cw,u.EnemyCharacter) if (a.get_actor_location()-enemy.get_actor_location()).length()<5]
   if not targets:return
   screen=cp.project_world_location_to_screen(targets[0].get_actor_location());cp.set_mouse_location(int(screen.x),int(screen.y))
   _ds_phase('aim');return
  mapping=_ds['mapping'][_ds['index']];tag=mapping['input'].split('"')[1];suffix=tag.split('.')[-1]
  action=u.load_asset('/Game/BluePrints/Input/InputActions/IA_'+('Input'+suffix if suffix.isdigit() else suffix))
  lib=u.get_default_object(u.load_class(None,'/Script/Engine.SubsystemBlueprintLibrary'))
  sub=lib.call_method('GetLocalPlayerSubSystemFromPlayerController',(cp,u.EnhancedInputLocalPlayerSubsystem.static_class()))
  if _ds['phase']=='aim':
   if age<.25:return
   _ds['casts'].append({'ability':mapping,'samples':[],'runtimes':[],'projectiles':[]});_ds_phase('cast');return
  if _ds['phase']=='cast':
   screen=cp.project_world_location_to_screen(enemy.get_actor_location());cp.set_mouse_location(int(screen.x),int(screen.y))
   sub.inject_input_vector_for_action(action,u.Vector(1,0,0) if suffix!='1' or not _ds['casts'][-1].get('injected') else u.Vector(0,0,0),[],[])
   _ds['casts'][-1]['injected']=True
   if age>.35 and not _ds['casts'][-1].get('capture_requested'):
    u.SystemLibrary.execute_console_command(cw,'PureMass.QA.CaptureUI 1 DungeonSkills_'+suffix)
    _ds['casts'][-1]['capture_requested']=True
   cast=_ds['casts'][-1]
   for world in [sw,cw]:
    for actor in u.GameplayStatics.get_all_actors_of_class(world,u.PureMassRuntime):
     if not actor.get_owner():continue
     cfg=actor.get_editor_property('prototype')
     if actor.get_path_name() not in cast['runtimes']:cast['runtimes'].append(actor.get_path_name())
     if cfg and cfg.get_editor_property('kind')==u.AbilityPrototypeKind.BEAM:
      cast['samples'].append({'world':world.get_path_name(),'at':age,'start':_ds_vec(actor.get_actor_location()),'end':_ds_vec(actor.get_editor_property('beam_end')),'owner':_ds_vec(actor.get_owner().get_actor_location()),'target':_ds_vec(enemy.get_actor_location()),'visual_active':any(v.is_active() for v in actor.get_components_by_class(u.NiagaraComponent))})
    for actor in u.GameplayStatics.get_all_actors_of_class(world,u.MyProjectile):
     if 'FireBolt' in actor.get_class().get_path_name() and actor.get_path_name() not in cast['projectiles']:cast['projectiles'].append(actor.get_path_name())
   if age>2.0:
    sub.inject_input_vector_for_action(action,u.Vector(0,0,0),[],[])
    _ds['index']+=1
    if _ds['index']>=len(_ds['mapping']):_ds_end();return
    _ds_phase('settle')
 except Exception as e:_ds_end(repr(e))
_ds_handle=u.register_slate_post_tick_callback(_ds_tick)
_ds_write()
