"""Read-only Niagara reflection and known source assets; no package saves."""
import json
from pathlib import Path
import unreal as u
root=Path(u.Paths.project_dir()).resolve();out={}
paths=['/Game/Art/CompositionEffects/NS_HunterSentry_Core','/Game/Art/CompositionEffects/NS_FireSeed_Field','/Game/Art/CompositionEffects/NS_StormSeal_Field','/Game/Art/CompositionEffects/NS_Aftershock_Ring','/Game/Art/TerrainSurfaces/Effects/NS_Frost_Surface','/Game/Art/TerrainSurfaces/Effects/NS_Frost_Status']
for path in paths:
 obj=u.load_asset(path);assert obj,path
 row={'methods':[x for x in dir(obj) if any(t in x.lower() for t in ['parameter','emitter','system','bound','expos'])]}
 for prop in ['exposed_parameters','fixed_bounds','warmup_time','warmup_tick_delta','effect_type','asset_import_data']:
  try:row[prop]=str(obj.get_editor_property(prop))
  except Exception as e:row[prop]={'error':str(e)}
 out[path]=row
(root/'Saved/GAAuthoring/CompositionBatch6/vfx-inspect.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf-8')
