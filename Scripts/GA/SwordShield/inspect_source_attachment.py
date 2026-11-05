import unreal as u,json
from pathlib import Path
assert not u.EditorLevelLibrary.get_game_world()
cls=u.load_class(None,'/Game/RPGHeroSquad/CharacterBP/BP_TinyHeroPolyart.BP_TinyHeroPolyart_C')
t=u.Transform(location=u.Vector(0,0,2000))
sub=u.get_editor_subsystem(u.EditorActorSubsystem)
a=sub.spawn_actor_from_class(cls,u.Vector(0,0,2000))
r=[]
for c in a.get_components_by_class(u.StaticMeshComponent):
 r.append({'name':c.get_name(),'socket':str(c.get_attach_socket_name()),'relative':str(c.get_relative_transform()),'mesh':str(c.static_mesh)})
(Path(u.Paths.project_dir())/'Saved/GAAuthoring/SwordShield/source-attachment.json').write_text(json.dumps(r,indent=2))
sub.destroy_actor(a)
