"""Controlled Actor damage-number presentation workload; no health or combat changes.
Slate callback intervals are diagnostic frame proxies, not packaged frame timings.
"""
import unreal as u,time,json,uuid
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve();out=root/'Saved/Performance/DamageNumbers'/uuid.uuid4().hex;out.mkdir(parents=True)
pcs=[p for p in u.ObjectIterator(u.MyPlayerController) if p.get_world() and 'UEDPIE_' in p.get_world().get_path_name() and p.is_local_controller()]
assert len(pcs)==1,'One local PIE controller required'
pc=pcs[0];pawn=pc.get_controlled_pawn();assert pawn
pool=next(s for s in u.ObjectIterator(u.DamageNumberPool) if s.get_world()==pc.get_world())
settings=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'));old=settings.get_editor_property('bThrottleCPUWhenNotForeground');settings.set_editor_property('bThrottleCPUWhenNotForeground',False)
previous={key:u.SystemLibrary.get_console_variable_float_value(key) for key in ['t.MaxFPS','r.VSync','DamageNumbers.Pool','DamageNumbers.Capacity','DamageNumbers.MergeSeconds','DamageNumbers.Reserved','DamageNumbers.Diagnostic']}
for key in ['t.MaxFPS','r.VSync']:u.SystemLibrary.execute_console_command(pc,key+' 0')
cases=[(mode,rate) for repeat in range(3) for rate in [100,500] for mode in [0,1]]
for k in ['DamageNumbers.MergeSeconds','DamageNumbers.Reserved','DamageNumbers.Diagnostic']:u.SystemLibrary.execute_console_command(pc,k+' 0')
u.SystemLibrary.execute_console_command(pc,'DamageNumbers.Capacity 1024')
u.SystemLibrary.execute_console_command(pc,'DamageNumbers.Pool 0')
pool.reset_counters()
state={'case':0,'began':time.monotonic(),'last':time.monotonic(),'sent':0,'rows':[],'frames':[],'dispatch':[],'sample_sent':0}
def finish(error=None):
 u.unregister_slate_post_tick_callback(handle);settings.set_editor_property('bThrottleCPUWhenNotForeground',old)
 for key,value in previous.items():u.SystemLibrary.execute_console_command(pc,key+' '+str(value))
 (out/'comparison.json').write_text(json.dumps({'cases':state['rows'],'error':error,'note':'PIE Slate callback interval; dispatch includes Python/reflection; not exclusive native CPU time'},indent=2));(out.parent/'latest-comparison.txt').write_text(str(out))
def tick(dt):
 try:
  now=time.monotonic();age=now-state['began'];frame=now-state['last'];state['last']=now
  mode,rate=cases[state['case']];desired=int(min(age,8)*rate);count=desired-state['sent']
  begin=time.perf_counter()
  for i in range(count):pc.call_method('ShowDamageNumber',(10.,pawn,False,(state['sent']+i)%10==0))
  elapsed=time.perf_counter()-begin;state['sent']+=count
  if 2<=age<3 and 'verified' not in state:
   live=[c for c in u.ObjectIterator(u.DamageTextComponent) if c.get_world()==pc.get_world() and c.get_owner() and c in c.get_owner().get_components_by_class(u.DamageTextComponent) and c.is_visible()]
   state['verified']=len(live);assert rate==0 or len(live)>0,'No visible number components'
  if 3<=age<8:
   state['frames'].append(frame*1000);state['dispatch'].append(elapsed*1000);state['sample_sent']+=count
  if age<10:return
  active=[c for c in u.ObjectIterator(u.DamageTextComponent) if c.get_world()==pc.get_world() and c.get_owner() and c in c.get_owner().get_components_by_class(u.DamageTextComponent)]
  state['rows'].append({'rate':rate,'total_sent':state['sent'],'sample_sent':state['sample_sent'],'frame_ms':state['frames'],'dispatch_ms':state['dispatch'],'registered_after_drain':len(active),'mode':mode,'native':json.loads(pool.get_report())})
  state.pop('verified',None)
  state['case']+=1
  if state['case']==len(cases):finish();return
  u.SystemLibrary.execute_console_command(pc,'DamageNumbers.Pool '+str(cases[state['case']][0]));pool.reset_counters()
  state.update(began=now,last=now,sent=0,frames=[],dispatch=[],sample_sent=0)
 except Exception as e:finish(repr(e))
handle=u.register_slate_post_tick_callback(tick)



