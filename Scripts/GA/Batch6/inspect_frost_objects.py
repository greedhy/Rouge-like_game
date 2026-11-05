import json
from pathlib import Path
import unreal as u
x=u.load_asset('/Game/CompositionBatch6/NS_GlacialStorm')
out=[]
for cls in [u.NiagaraScript,u.NiagaraSpriteRendererProperties]:
 for obj in u.ObjectIterator(cls):
  if obj.get_path_name().startswith(x.get_path_name()+':'):
   row={'path':obj.get_path_name(),'class':obj.get_class().get_name()}
   if isinstance(obj,u.NiagaraSpriteRendererProperties):
    for key in ['material','source_mode','alignment','facing_mode']:
     try:row[key]=str(obj.get_editor_property(key))
     except:pass
   out.append(row)
(Path(u.Paths.project_dir())/'Saved/GAAuthoring/CompositionBatch6/frost-objects.json').write_text(json.dumps(out,indent=2))
