"""Read-only PIE observation: execute while casting in L_RoguelikeRun (no saves)."""
import unreal as u,json,time
from pathlib import Path
r={'timestamp':time.time(),'players':[],'effects':[]}
for p in u.ObjectIterator(u.PlayerCharacter):
 if 'UEDPIE_' not in p.get_path_name():continue
 row={'path':p.get_path_name(),'location':str(p.get_actor_location()),'startup':[]}
 for a in p.get_editor_property('startup_ability'):
  c=u.get_default_object(a)
  row['startup'].append({'class':a.get_path_name(),'input':c.get_editor_property('startup_input_tag').export_text()})
 r['players'].append(row)
for a in u.ObjectIterator(u.PureMassRuntime):
 if 'UEDPIE_' not in a.get_path_name():continue
 cfg=a.get_editor_property('prototype')
 if not cfg:continue
 row={'path':a.get_path_name(),'prototype':cfg.get_path_name(),'kind':str(cfg.get_editor_property('kind')),'start':str(a.get_actor_location()),'end':str(a.get_editor_property('beam_end')),'owner':str(a.get_owner().get_actor_location()) if a.get_owner() else None}
 r['effects'].append(row)
path=Path(u.Paths.project_dir(),'Saved/GAAuthoring/DungeonHybridFix/runtime-'+str(time.time_ns())+'.json');path.write_text(json.dumps(r,indent=2));u.log(str(path))
