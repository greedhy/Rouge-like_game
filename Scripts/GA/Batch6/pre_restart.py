import json
from pathlib import Path
import unreal as u
root=Path(u.Paths.project_dir()).resolve();dirty=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
(root/'Saved/GAAuthoring/CompositionBatch6/pre-restart-dirty.json').write_text(json.dumps(dirty,indent=2),encoding='utf-8')
u.log('BATCH6_PRE_RESTART_DIRTY '+json.dumps(dirty))
# All successful Batch6 packages have already been individually saved. The failed halo config is deliberately discarded.
