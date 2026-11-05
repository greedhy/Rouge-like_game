import unreal as u,json,time
from pathlib import Path
w=u.EditorLevelLibrary.get_game_world();p=u.GameplayStatics.get_player_pawn(w,0);d=u.GameplayStatics.get_all_actors_of_class(w,u.DungeonRunDirector)[0]
asc=p.get_editor_property('player_state').get_component_by_class(u.MyAbilitySystemComponent);movement=p.get_component_by_class(u.CharacterMovementComponent)
room=d.get_room_index(p.get_actor_location());r=next(x for x in json.loads(d.get_run_report())['rooms'] if x['index']==room)
center=u.Vector(r['x'],r['y'],798);p.set_actor_location(center,False,True);movement.set_movement_mode(u.MovementMode.MOVE_WALKING)
cls=u.load_class(None,'/Game/BluePrints/Characters/Stander/GA_Stander_Dash.GA_Stander_Dash_C');asc.remove_active_gameplay_effect_by_source_effect(u.SwordTimedEffect,asc,-1);assert asc.try_activate_ability_by_class(cls)
f=p.get_actor_forward_vector();distance=0
while distance<10000 and d.get_room_index(center+f*distance)==room:distance+=10
assert distance<10000
start=center+f*(distance-80);p.set_actor_location(start,False,True)
t=u.Transform(location=start+u.Vector(0,0,-103),scale=u.Vector(20,20,.1))
# Reuse one procedural static mesh in this disposable PIE; no asset/map is saved. Stop PIE after this probe to restore geometry.
floor=next(c for a in u.GameplayStatics.get_all_actors_of_class(w,u.Actor) for c in a.get_components_by_class(u.StaticMeshComponent) if a!=p and c.get_editor_property('static_mesh'))
floor.set_mobility(u.ComponentMobility.MOVABLE);floor.set_static_mesh(u.load_asset('/Engine/BasicShapes/Cube'));floor.set_world_transform(t,False,True)
floor.set_collision_object_type(u.CollisionChannel.ECC_WORLD_STATIC);floor.set_collision_enabled(u.CollisionEnabled.QUERY_AND_PHYSICS)
report={'status':'running','room':room,'edge_distance':distance,'samples':[]};out=Path(u.Paths.project_dir())/'Saved/GAAuthoring/Stander/room-boundary-runtime.json';began=time.monotonic()
def tick(dt):
 try:
  loc=p.get_actor_location();report['samples'].append({'distance':(loc-start).length(),'room':d.get_room_index(loc)})
  if time.monotonic()-began>2:
   report.update(status='complete',passed=all(s['room']==room for s in report['samples']) and 1<report['samples'][-1]['distance']<100)
 except Exception as e:report.update(status='error',passed=False,error=str(e))
 if report['status']!='running':u.unregister_slate_post_tick_callback(handle)
 out.write_text(json.dumps(report,indent=2))
handle=u.register_slate_post_tick_callback(tick)
