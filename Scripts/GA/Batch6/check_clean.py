import unreal as u, json
from pathlib import Path
dirty=list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
names=[p.get_path_name() for p in dirty]
(Path(u.Paths.project_dir()).resolve()/'Saved/GAAuthoring/CompositionBatch6/restart-dirty.json').write_text(json.dumps(names),encoding='utf-8')
assert not names,names
