"""Observe changing client snapshots, not interpolated render transforms. PIE only."""
import unreal as u, time, json
from pathlib import Path
_rate_start=time.monotonic();_rate_last=None;_rate_key=None;_rate_deltas=[];_rate_frames=0
def _rate_tick(dt):
 global _rate_last,_rate_key,_rate_frames
 now=time.monotonic();_rate_frames+=1
 for sim in u.ObjectIterator(u.PureMassSubsystem):
  w=sim.get_world()
  if not w or 'UEDPIE_' not in w.get_path_name() or u.GameplayStatics.get_game_mode(w):continue
  key=tuple((v.id,round(v.location.x,1),round(v.location.y,1),v.state) for v in sim.get_snapshots())
  if key and key!=_rate_key:
   if _rate_last is not None:_rate_deltas.append(now-_rate_last)
   _rate_last=now;_rate_key=key
 if now-_rate_start>=5:
  u.unregister_slate_post_tick_callback(_rate_handle)
  values=sorted(_rate_deltas)
  result={'duration':now-_rate_start,'sample_frames':_rate_frames,'changed_snapshots':len(values),
          'mean_change_ms':1000*sum(values)/len(values) if values else None,
          'median_change_ms':1000*values[len(values)//2] if values else None,
          'note':'Observed changed client snapshots; callback sampling, simulation rate and stationary entities constrain this measurement. Not a packet capture.'}
  Path(u.Paths.project_dir(),'Saved/GAAuthoring/DungeonHybrid/snapshot-rate.json').write_text(json.dumps(result,indent=2))
_rate_handle=u.register_slate_post_tick_callback(_rate_tick)
