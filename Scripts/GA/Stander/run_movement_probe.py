import unreal as u,json,time
from pathlib import Path
w=u.EditorLevelLibrary.get_game_world();p=u.GameplayStatics.get_player_pawn(w,0)
assert isinstance(p,u.StanderPlayer)
mesh=p.get_component_by_class(u.SkeletalMeshComponent);movement=p.get_component_by_class(u.CharacterMovementComponent)
start=p.get_actor_location();started=u.GameplayStatics.get_time_seconds(w)
report={'status':'running','samples':[]};out=Path(u.Paths.project_dir())/'Saved/GAAuthoring/Stander/movement-runtime.json'
def tick(dt):
 try:
  elapsed=u.GameplayStatics.get_time_seconds(w)-started
  if elapsed<1.2:p.add_movement_input(u.Vector(0,1,0),1.,True)
  loc=p.get_actor_location();bone=mesh.get_socket_transform('foot_l',u.RelativeTransformSpace.RTS_COMPONENT).translation
  report['samples'].append({'t':elapsed,'speed':p.get_velocity().length(),'location':[loc.x,loc.y,loc.z],'foot':[bone.x,bone.y,bone.z]})
  if elapsed>1.6:
   samples=[s for s in report['samples'] if .3<s['t']<1.1]
   report.update(status='complete',passed=bool(samples) and max(s['speed'] for s in samples)>100 and max(s['foot'][1] for s in samples)-min(s['foot'][1] for s in samples)>5)
 except Exception as e:report.update(status='error',passed=False,error=str(e))
 if report['status']!='running':u.unregister_slate_post_tick_callback(handle)
 out.write_text(json.dumps(report,indent=2))
handle=u.register_slate_post_tick_callback(tick)

