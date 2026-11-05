"""Open the existing dedicated QA map; never save a map or user save slot."""
import unreal as u, json
from pathlib import Path
out=Path(u.Paths.project_dir()).resolve()/'Saved/GAAuthoring/CompositionBatch6'
dirty=list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
assert not dirty, 'Unsaved packages: '+str([p.get_path_name() for p in dirty])
previous=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name().split('.')[0]
(out/'pie-previous-map.json').write_text(json.dumps({'map':previous}),encoding='utf-8')
assert u.get_editor_subsystem(u.LevelEditorSubsystem).load_level('/Game/CompositionBatch6/QA/L_Batch6Test')
perf=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
(out/'pie-performance.json').write_text(json.dumps({'throttle':perf.get_editor_property('bThrottleCPUWhenNotForeground')}),encoding='utf-8')
perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
