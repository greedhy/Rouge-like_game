"""PIE lifecycle sampler. Keeps only strings/scalars between samples, not UObject references.
Run before a scenario. Writes bounded JSONL for 30 seconds, including post-PIE disappearance.
Polling cannot observe objects created and destroyed between samples or establish GC safety.
"""
import unreal as u
import json,time,uuid
from pathlib import Path
out=Path(u.Paths.project_dir())/'Saved/Lifecycle'/uuid.uuid4().hex
out.mkdir(parents=True)
(out.parent/'latest.txt').write_text(str(out))
state={'start':time.monotonic(),'next':0.,'previous':{},'events':0,'samples':0}
classes=[u.Actor,u.DamageTextComponent,u.MyAbilitySystemComponent,u.DamageNumberPool]

def sample():
 result={}
 for cls in classes:
  for obj in u.ObjectIterator(cls):
   if not u.SystemLibrary.is_valid(obj):continue
   world=obj.get_world()
   if not world or 'UEDPIE_' not in world.get_path_name():continue
   path=obj.get_path_name()
   row={'class':obj.get_class().get_path_name(),'world':world.get_path_name()}
   if isinstance(obj,u.Actor):
    row.update(hidden=obj.get_editor_property('bHidden'),tick=obj.is_actor_tick_enabled(),role=str(obj.get_local_role()))
    try:row['life']=obj.get_editor_property('DungeonPoolLife')
    except Exception:pass
   if isinstance(obj,u.DamageTextComponent):row['visible']=obj.is_visible()
   if isinstance(obj,u.DamageNumberPool):row['pool']=json.loads(obj.get_report())
   result[path]=row
 return result

def tick(dt):
 try:
  age=time.monotonic()-state['start']
  if age<state['next']:return
  state['next']=age+.1
  current=sample();events=[]
  for path,row in current.items():
   old=state['previous'].get(path)
   if old!=row:events.append({'time':age,'event':'observed' if old is None else 'changed','path':path,'state':row})
  for path in state['previous'].keys()-current.keys():events.append({'time':age,'event':'no_longer_observed','path':path})
  with (out/'events.jsonl').open('a',encoding='utf8') as f:
   for event in events:f.write(json.dumps(event)+'\n')
  state['events']+=len(events);state['samples']+=1;state['previous']=current
  if age>=30:finish()
 except Exception as e:finish(repr(e))

def finish(error=None):
 u.unregister_slate_post_tick_callback(handle)
 (out/'summary.json').write_text(json.dumps({'error':error,'samples':state['samples'],'events':state['events'],'remaining':len(state['previous']),'limitations':['Polling, not allocation/destructor hooks','Paths are observation keys, not guaranteed unique across recreation','No strong UObject references retained across ticks','No assertion that unobserved means freed memory']},indent=2))
handle=u.register_slate_post_tick_callback(tick)

