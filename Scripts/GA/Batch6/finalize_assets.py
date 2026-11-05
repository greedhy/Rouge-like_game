import runpy
from pathlib import Path
import unreal as u
folder=Path(u.Paths.project_dir()).resolve()/'Scripts/GA/Batch6'
for name in ['author_area_visual.py','author_assets.py','verify_assets.py']:
    runpy.run_path(str(folder/name))
u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property('bThrottleCPUWhenNotForeground',False)
