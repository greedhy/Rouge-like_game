import unreal as u,json
from pathlib import Path
s=u.load_asset('/Game/Assets/Effects/Shock/NS_ElectricBeam')
r=[x.get_path_name() for x in u.ObjectIterator(u.NiagaraScript) if s.get_path_name() in x.get_path_name()]
Path(u.Paths.project_dir(),'Saved/GAAuthoring/DungeonHybridFix/beam-scripts.json').write_text(json.dumps(r,indent=2))
