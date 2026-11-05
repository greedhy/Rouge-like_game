import unreal as u,json
from pathlib import Path
r={};world=u.load_asset('/Game/PlatformAnimation/Maps/Platform_Animations')
r['actors']=[]
for a in u.GameplayStatics.get_all_actors_of_class(world,u.Actor):
 cs=a.get_components_by_class(u.SkeletalMeshComponent)
 if cs:r['actors'].append({'actor':a.get_name(),'components':[{'mesh':str(c.get_editor_property('skeletal_mesh_asset')),'parent':str(c.get_attach_parent()),'socket':str(c.get_attach_socket_name()),'relative':str(c.get_relative_transform())} for c in cs]})
(Path(u.Paths.project_dir())/'Saved/GAAuthoring/Stander/demo-attachments.json').write_text(json.dumps(r,indent=2))
