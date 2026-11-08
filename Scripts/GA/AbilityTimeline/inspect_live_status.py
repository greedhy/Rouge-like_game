"""Read-only live editor status, including unsaved packages."""
import json
from pathlib import Path
import unreal as u
root = Path(u.Paths.convert_relative_path_to_full(u.Paths.project_dir()))
report = {'pie': str(u.EditorLevelLibrary.get_game_world()),
          'dirty': [x.get_path_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()] +
                   [x.get_path_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]}
(root / 'Saved/GAAuthoring/AbilityTimeline/recovery-investigation/live-status-result.json').write_text(json.dumps(report, ensure_ascii=False), encoding='utf-8')
