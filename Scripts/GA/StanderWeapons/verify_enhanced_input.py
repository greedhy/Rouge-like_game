"""Exercise actual Enhanced Input action bindings in the owned Stander PIE."""
import unreal as u,json,time,traceback
from pathlib import Path
w=u.EditorLevelLibrary.get_game_world();assert w and (w.get_path_name().startswith('/Temp/') or w.get_path_name().startswith('/Game/RoguelikeGeneration/Maps/UEDPIE_0_L_RoguelikeRun.'))
p=u.GameplayStatics.get_player_pawn(w,0);pc=u.GameplayStatics.get_player_controller(w,0)
sub=next(s for s in u.ObjectIterator(u.EnhancedInputLocalPlayerSubsystem) if not s.get_name().startswith('Default__') and isinstance(s.get_outer(),u.LocalPlayer))
lmb=u.load_asset('/Game/BluePrints/Input/InputActions/IA_LMB')
move=pc.get_editor_property('mass_move_action')
origin=p.get_actor_location();mesh=p.get_component_by_class(u.SkeletalMeshComponent)

state={'start':time.monotonic(),'montage':False,'stage':0};report={'passed':False}
def tick(dt):
 try:
  elapsed=time.monotonic()-state['start']
  if state['stage']==0:
   if elapsed<.35:sub.inject_input_vector_for_action(lmb,u.Vector(1,0,0),[],[])
   state['montage']=state['montage'] or bool(mesh.get_anim_instance().get_current_active_montage())
   if elapsed>1.2:
    report['lmb_montage']=state['montage'];report['lmb_displacement']=(p.get_actor_location()-origin).length()
    assert state['montage'],'LMB binding did not activate an actual montage'
    assert report['lmb_displacement']<5,'LMB caused movement'
    state.update(stage=1,start=time.monotonic(),move_origin=p.get_actor_location())
  else:
   if elapsed<.35:sub.inject_input_vector_for_action(move,u.Vector(0,1,0),[],[])
   if elapsed>.6:
    report['keyboard_displacement']=(p.get_actor_location()-state['move_origin']).length()
    assert report['keyboard_displacement']>15,'Keyboard move action failed'
    report['passed']=True;finish()
 except Exception:
  report['error']=traceback.format_exc();finish()
def finish():
 u.unregister_slate_post_tick_callback(handle)
 p.get_component_by_class(u.CharacterMovementComponent).stop_movement_immediately();p.set_actor_location(origin,False,True)
 (Path(u.Paths.project_dir())/'Saved/GAAuthoring/StanderBasicInput/pie-enhanced-input.json').write_text(json.dumps(report,indent=2))
handle=u.register_slate_post_tick_callback(tick)



