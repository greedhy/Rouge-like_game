"""Restore the editor map/performance setting after temporary PIE verification."""
import json
from pathlib import Path
import unreal as u
folder=Path(u.Paths.project_dir()).resolve()/'Saved/GAAuthoring/CompositionBatch6'
state=json.loads((folder/'pie-performance.json').read_text(encoding='utf-8'))
u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',state['throttle'])
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(),'Preserve unsaved editor map'
previous=json.loads((folder/'pie-previous-map.json').read_text(encoding='utf-8'))['map']
assert u.get_editor_subsystem(u.LevelEditorSubsystem).load_level(previous)
(folder/'editor-restored.json').write_text(json.dumps({'restored':True,'map':previous,'throttle':state['throttle']}),encoding='utf-8')
