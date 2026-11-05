import unreal as u
from pathlib import Path
folder=Path(u.Paths.project_dir())/'Saved/GAAuthoring/SwordShield/icons';folder.mkdir(exist_ok=True)
paths=['/Game/KiroshoSkIconsPk0/Textures/T_DeepCut','/Game/KiroshoSkIconsPk0/Textures/T_StormAttack']+['/Game/Skill_Icons/Textures/512x512/T_Skill_Icons__'+str(i).zfill(2) for i in range(1,51)]
for path in paths:
 a=u.load_asset(path)
 if not a:continue
 task=u.AssetExportTask();task.object=a;task.filename=str(folder/(a.get_name()+'.png'));task.automated=True;task.prompt=False;task.replace_identical=True;task.exporter=u.TextureExporterPNG()
 assert u.Exporter.run_asset_export_task(task)
