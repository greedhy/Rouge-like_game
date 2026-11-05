import unreal as u,json
from pathlib import Path
r={};w=u.EditorLevelLibrary.get_game_world()
t=u.load_asset('/Game/PureMass/DA_MassQAGoblin');mesh=t.get_editor_property('mesh');r['mesh']=mesh.get_path_name();pa=mesh.get_editor_property('physics_asset');r['physics']=str(pa);r['bodies']=[str(x.get_editor_property('bone_name')) for x in [x for x in u.ObjectIterator(u.Object) if x.get_outer()==pa and x.get_class().get_name()=="SkeletalBodySetup"]]
r['controllers']=[{'class':x.get_class().get_path_name(),'catalog':str(x.get_editor_property('ability_info'))} for x in u.ObjectIterator(u.OverlayWidgetController) if x.get_world()==w]
r['images']=[]
for x in u.ObjectIterator(u.Image):
 if x.get_world()==w:
  b=x.get_editor_property('brush');obj=b.get_editor_property('resource_object')
  if obj:r['images'].append({'name':x.get_path_name(),'resource':obj.get_path_name()})
(Path(u.Paths.project_dir())/'Saved/GAAuthoring/StanderMass/physics-hud.json').write_text(json.dumps(r,indent=2))


