import unreal as u,json
from pathlib import Path
assert not u.EditorLevelLibrary.get_game_world()
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
root=Path(u.Paths.project_dir());folder=root/'Saved/GAAuthoring/StanderMass';ed=u.get_editor_subsystem(u.UnrealEditorSubsystem)
loc,rot=u.EditorLevelLibrary.get_level_viewport_camera_info()
(folder/'restore.json').write_text(json.dumps({'map':ed.get_editor_world().get_path_name().split('.')[0],'location':[loc.x,loc.y,loc.z],'rotation':[rot.pitch,rot.yaw,rot.roll]}))
u.EditorLoadingAndSavingUtils.new_blank_map(False)
w=ed.get_editor_world();w.get_world_settings().set_editor_property('default_game_mode',u.load_class(None,'/Game/BluePrints/Characters/Stander/BP_StanderGameMode.BP_StanderGameMode_C'))
ss=u.get_editor_subsystem(u.EditorActorSubsystem)
floor=ss.spawn_actor_from_class(u.StaticMeshActor,u.Vector(0,0,-5));floor.static_mesh_component.set_static_mesh(u.load_asset('/Engine/BasicShapes/Cube'));floor.set_actor_scale3d(u.Vector(30,30,.1));floor.set_actor_location(u.Vector(0,0,-5),False,True)
start=ss.spawn_actor_from_class(u.TravelPlayerStart,u.Vector(0,0,100));start.set_actor_location(u.Vector(0,0,100),False,True)
start.set_editor_property('player_start_tag','RG_Entrance');start.set_editor_property('default_entry_group',True)
for name,pos,team in [('Target',u.Vector(120,0,89),'Enemy'),('Friend',u.Vector(110,40,89),'Player'),('Outside',u.Vector(450,0,89),'Enemy')]:
 a=ss.spawn_actor_from_class(u.EnemyCharacter,pos);a.set_actor_location(pos,False,True);a.set_editor_property('auto_possess_ai',u.AutoPossessAI.DISABLED);a.set_editor_property('tags',[name,team])
wall=ss.spawn_actor_from_class(u.StaticMeshActor,u.Vector(60,0,80));wall.static_mesh_component.set_static_mesh(u.load_asset('/Engine/BasicShapes/Cube'));wall.set_actor_scale3d(u.Vector(.1,3,1.6));wall.set_actor_location(u.Vector(60,0,80),False,True);wall.set_editor_property('tags',['SwordWall']);wall.set_actor_enable_collision(False);wall.set_actor_hidden_in_game(True)
ss.clear_actor_selection_set()
exec((root/'Saved/GAAuthoring/MonsterWave1/unthrottle_probe.py').read_text())
