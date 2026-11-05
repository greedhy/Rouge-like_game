"""Aggregation identity, damage totals, target destruction, critical reserve."""
import unreal as u,json,time
from pathlib import Path
pc=next(p for p in u.ObjectIterator(u.MyPlayerController) if p.get_world() and 'UEDPIE_' in p.get_world().get_path_name() and p.is_local_controller());pawn=pc.get_controlled_pawn();pool=next(s for s in u.ObjectIterator(u.DamageNumberPool) if s.get_world()==pc.get_world())
old={k:u.SystemLibrary.get_console_variable_float_value(k) for k in ['DamageNumbers.MergeSeconds','DamageNumbers.Reserved','DamageNumbers.Capacity','DamageNumbers.Diagnostic']}
def cmd(s):u.SystemLibrary.execute_console_command(pc,s)
for k,v in [('DamageNumbers.MergeSeconds',.1),('DamageNumbers.Reserved',0),('DamageNumbers.Capacity',256),('DamageNumbers.Diagnostic',0)]:cmd(k+' '+str(v))
pool.reset_counters()
lib=u.get_default_object(u.GameplayStatics);tf=u.Transform(location=pawn.get_actor_location()+u.Vector(100,0,0))
other=lib.call_method('BeginDeferredActorSpawnFromClass',(pc,u.Character.static_class(),tf,u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));lib.call_method('FinishSpawningActor',(other,tf,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
for damage,blocked,crit in [(10.,False,False),(20.,False,False),(7.,False,True),(5.,True,False)]:pc.call_method('ShowDamageNumber',(damage,pawn,blocked,crit))
pc.call_method('ShowDamageNumber',(40.,other,False,False));other.destroy_actor()
checks={};reports=[];phase=0;began=time.monotonic()
def textdata():
 result={}
 for c in u.ObjectIterator(u.DamageTextComponent):
  if c.get_world()!=pc.get_world() or not c.is_visible():continue
  w=c.get_user_widget_object()
  if w:
   t=w.get_editor_property('Text_Damage');result[str(t.get_text())]=t.get_editor_property('color_and_opacity').export_text()
 return result
def finish(error=None):
 u.unregister_slate_post_tick_callback(handle)
 for k,v in old.items():cmd(k+' '+str(v))
 Path(u.Paths.project_dir(),'Saved/Performance/DamageNumbers/aggregation.json').write_text(json.dumps({'checks':checks,'error':error,'reports':reports},indent=2))
def tick(dt):
 global phase,began
 try:
  age=time.monotonic()-began;r=json.loads(pool.get_report())
  if phase==0 and age>.3:
   texts=textdata();checks['groups_and_total']=set(texts)=={'30','7','5','40'};checks['style_separation']=texts.get('30')!=texts.get('7') and texts.get('30')!=texts.get('5');checks['merge_count']=r['actor_inputs']==5 and r['merged']==1 and r['requests']==4;reports.append({'native':r,'texts':texts});u.SystemLibrary.execute_console_command(pc,'PureMass.QA.CaptureUI 0 DamageNumbers_Aggregated');phase=1
  elif phase==1 and age>1.6:
   checks['expired']=r['active']==0 and r['pending']==0
   cmd('DamageNumbers.MergeSeconds 0');cmd('DamageNumbers.Capacity 4');cmd('DamageNumbers.Reserved 1');pool.reset_counters()
   for i in range(4):pc.call_method('ShowDamageNumber',(10.,pawn,False,False))
   pc.call_method('ShowDamageNumber',(999.,pawn,False,True));phase=2;began=time.monotonic()
  elif phase==2 and age>.3:
   checks['reserved_slot']=r['active']==4 and r['dropped']==1 and '999' in textdata();checks['reused_normal_color']=textdata().get('10')==reports[0]['texts'].get('30');reports.append(r);phase=3
  elif phase==3 and age>1.6:
   checks['final_expiry']=r['active']==0;finish()
 except Exception as e:finish(repr(e))
handle=u.register_slate_post_tick_callback(tick)

