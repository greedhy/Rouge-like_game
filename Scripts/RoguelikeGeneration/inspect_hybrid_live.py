import unreal as u,json
from pathlib import Path
r=[]
for s in u.ObjectIterator(u.PureMassSubsystem):
 if not s.get_world() or 'UEDPIE_' not in s.get_world().get_path_name():continue
 w=s.get_world();cs=u.GameplayStatics.get_all_actors_of_class(w,u.PureMassCrowd)
 r.append({'world':w.get_path_name(),'entities':len(s.get_snapshots()),'deaths':s.get_death_count(),'crowds':[{'active':c.get_active_death_count(),'allocated':c.get_allocated_death_count(),'warnings':c.get_warning_count()} for c in cs]})
Path(u.Paths.project_dir(),'Saved/GAAuthoring/DungeonHybrid/live.json').write_text(json.dumps(r,indent=2))
