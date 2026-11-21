"""Read real asset classes before assigning VFX to the new abilities."""
import json
from pathlib import Path
import unreal as u

paths = [
    '/Game/resources/Knife_light/VFX/NE_attack01',
    '/Game/resources/Knife_light/VFX/NE_attack03',
    '/Game/Assets/Effects/Firenado/NS_Tornado',
    '/Game/Art/BloodFX/Systems/NS_Blood_Hit_Splash',
    '/Game/Art/BloodFX/Systems/NS_Blood_Pool_Large',
]
result = {}
for path in paths:
    asset = u.load_asset(path)
    result[path] = asset.get_class().get_name() if asset else None
out = Path(u.Paths.project_saved_dir()) / 'GAAuthoring/ArtBatch'
out.mkdir(parents=True, exist_ok=True)
(out / 'inspection.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
