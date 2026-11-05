"""CSV-backed UI ablation and display-budget experiments; no gameplay damage."""
import unreal as u,time,json,uuid
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve();out=root/'Saved/Performance/DamageNumbers'/uuid.uuid4().hex;out.mkdir(parents=True)
pc=next(p for p in u.ObjectIterator(u.MyPlayerController) if p.get_world() and 'UEDPIE_' in p.get_world().get_path_name() and p.is_local_controller());pawn=pc.get_controlled_pawn();pool=next(s for s in u.ObjectIterator(u.DamageNumberPool) if s.get_world()==pc.get_world())
settings=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'));old=settings.get_editor_property('bThrottleCPUWhenNotForeground')
keys=['t.MaxFPS','r.VSync','DamageNumbers.Pool','DamageNumbers.Capacity','DamageNumbers.MergeSeconds','DamageNumbers.Reserved','DamageNumbers.Diagnostic'];previous={k:u.SystemLibrary.get_console_variable_float_value(k) for k in keys}
def cmd(s):u.SystemLibrary.execute_console_command(pc,s)
cases=[(name,diag,merge,cap) for repeat in range(3) for name,diag,merge,cap in [('normal',0,0,1024),('static',1,0,1024),('hidden',2,0,1024),('merge',0,.1,1024),('budget128',0,0,128)]]
st={'case':0,'phase':'setup','rows':[],'capture':False}
def finish(error=None):
 u.unregister_slate_post_tick_callback(handle)
 if st['capture']:cmd('csvprofile stop')
 for k,v in previous.items():cmd(k+' '+str(v))
 settings.set_editor_property('bThrottleCPUWhenNotForeground',old)
 (out/'ui.json').write_text(json.dumps({'cases':st['rows'],'error':error,'previous':previous},indent=2));(out.parent/'latest-ui.txt').write_text(str(out))
def tick(dt):
 try:
  now=time.monotonic()
  if st['phase']=='setup':
   name,diag,merge,cap=cases[st['case']]
   for k,v in [('DamageNumbers.Pool',1),('DamageNumbers.Capacity',cap),('DamageNumbers.MergeSeconds',merge),('DamageNumbers.Reserved',0),('DamageNumbers.Diagnostic',diag)]:cmd(k+' '+str(v))
   pool.reset_counters();st.update(phase='run',began=now,sent=0,before=set(str(p) for p in (root/'Saved/Profiling/CSV').glob('*.csv')))
  age=now-st['began']
  if st['phase']=='run':
   desired=int(min(age,8)*500)
   for i in range(desired-st['sent']):pc.call_method('ShowDamageNumber',(10.,pawn,False,(st['sent']+i)%10==0))
   st['sent']=desired
   if age>=3 and not st['capture']:cmd('csvprofile start');st['capture']=True
   if age>=8:cmd('csvprofile stop');st['capture']=False;st['phase']='drain'
  elif age>=11:
   csv=[str(p) for p in (root/'Saved/Profiling/CSV').glob('*.csv') if str(p) not in st['before']];assert len(csv)==1,csv
   r=json.loads(pool.get_report());assert r['active']==0 and r['pending']==0,r
   st['rows'].append({'name':cases[st['case']][0],'sent':st['sent'],'native':r,'csv':csv[0]});st['case']+=1
   if st['case']==len(cases):finish();return
   st['phase']='setup'
 except Exception as e:finish(repr(e))
settings.set_editor_property('bThrottleCPUWhenNotForeground',False);cmd('t.MaxFPS 0');cmd('r.VSync 0')
handle=u.register_slate_post_tick_callback(tick)
