import unreal as u,json
from pathlib import Path
r=[]
for a in u.ObjectIterator(u.EnemyCharacter):
 if 'UEDPIE_' in a.get_path_name():r.append({'path':a.get_path_name(),'tags':[str(t) for t in a.tags],'encounter':a.get_editor_property('bDungeonEncounter')})
Path(u.Paths.project_dir(),'Saved/GAAuthoring/DungeonHybridFix/actor-tags.json').write_text(json.dumps(r,indent=2))
