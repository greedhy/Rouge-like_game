import unreal as u
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve()
u.SystemLibrary.execute_console_command(None,'Trace.Stop')
u.SystemLibrary.execute_console_command(None,'CsvCategory MonsterPerf disable')
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
assert u.get_editor_subsystem(u.LevelEditorSubsystem).load_level((root/'Saved/Performance/previous-map.txt').read_text())
