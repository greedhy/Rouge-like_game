"""MCP editor read-only export of actual icons for human/agent visual selection."""
from pathlib import Path
import unreal as u
out=Path(u.Paths.project_dir()).resolve()/'Saved/GAAuthoring/CompositionBatch6/icons'
out.mkdir(parents=True,exist_ok=True)
for i in range(1,51):
 name='T_Skill_Icons__'+str(i).zfill(2)
 tex=u.load_asset('/Game/Skill_Icons/Textures/128x128/'+name)
 assert tex,name
 task=u.AssetExportTask();task.object=tex;task.filename=str(out/(name+'.tga'))
 task.automated=True;task.prompt=False;task.replace_identical=True
 assert u.Exporter.run_asset_export_task(task),name
