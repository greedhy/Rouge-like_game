import unreal as u,json
from pathlib import Path
rows=[]
for w in u.ObjectIterator(u.Image):
 if not w.get_path_name().startswith('/Game/') and 'SpellIcon' in w.get_name():rows.append({'path':w.get_path_name(),'brush':w.get_editor_property('brush').export_text(),'opacity':w.get_render_opacity()})
Path(u.Paths.project_dir(),'Saved/GAAuthoring/DungeonHybridFix/hud-live.json').write_text(json.dumps(rows,indent=2))
