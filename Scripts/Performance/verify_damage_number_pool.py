"""Run after comparison in single-player PIE: capacity, reuse, Mass route and cleanup."""
import unreal as u,json,time
from pathlib import Path
pc=next(p for p in u.ObjectIterator(u.MyPlayerController) if p.get_world() and 'UEDPIE_' in p.get_world().get_path_name() and p.is_local_controller())
pool=next(s for s in u.ObjectIterator(u.DamageNumberPool) if s.get_world()==pc.get_world());pawn=pc.get_controlled_pawn()
old={k:u.SystemLibrary.get_console_variable_float_value(k) for k in ['DamageNumbers.Pool','DamageNumbers.Capacity','DamageNumbers.MergeSeconds','DamageNumbers.Reserved','DamageNumbers.Diagnostic']}
for k in ['DamageNumbers.MergeSeconds','DamageNumbers.Reserved','DamageNumbers.Diagnostic']:u.SystemLibrary.execute_console_command(pc,k+' 0')
# Use existing retained pool; capacity applies to active entries, not retained count.
u.SystemLibrary.execute_console_command(pc,'DamageNumbers.Pool 1');u.SystemLibrary.execute_console_command(pc,'DamageNumbers.Capacity 2');pool.reset_counters()
for i in range(3):pc.call_method('ShowDamageNumber',(10.+i,pawn,False,i==0))
checks={};phase=0;began=time.monotonic();evidence=[]
def finish(error=None):
 u.unregister_slate_post_tick_callback(handle)
 for k,v in old.items():u.SystemLibrary.execute_console_command(pc,k+' '+str(v))
 Path(u.Paths.project_dir(),'Saved/Performance/DamageNumbers/lifecycle.json').write_text(json.dumps({'checks':checks,'error':error,'reports':evidence},indent=2))
def tick(dt):
 global phase,began
 try:
  age=time.monotonic()-began;r=json.loads(pool.get_report())
  if phase==0 and age>.2:
   checks['capacity']=r['active']==2 and r['dropped']==1;evidence.append(r)
   u.SystemLibrary.execute_console_command(pc,'PureMass.QA.CaptureUI 0 DamageNumbers_Pooled')
   phase=1
  elif phase==1 and age>1.5:
   checks['expired']=r['active']==0;evidence.append(r)
   pc.call_method('ShowDamageNumber',(37.,pawn,False,False));phase=2;began=time.monotonic()
  elif phase==2 and age>.2:
   checks['reuse']=r['reused']>=3 and r['active']==1;evidence.append(r)
   widgets=[c.get_user_widget_object() for c in u.ObjectIterator(u.DamageTextComponent) if c.get_world()==pc.get_world() and c.is_visible() and c.get_user_widget_object()]
   checks['text_reset']=any(str(w.get_editor_property('Text_Damage').get_text())=='37' for w in widgets)
   phase=3
  elif phase==3 and age>1.5:
   sim=next(s for s in u.ObjectIterator(u.PureMassSubsystem) if s.get_world()==pc.get_world())
   ident=sim.spawn_monster(u.load_asset('/Game/Performance/DA_BenchmarkGoblin'),pawn.get_actor_location()+u.Vector(100,0,0))
   damage=u.PureMassDamage();damage.import_text('(Source="'+pawn.get_path_name()+'")');damage.amount=10000.;sim.apply_damage(ident,damage);phase=4;began=time.monotonic()
  elif phase==4 and age>.4:
   checks['mass_route']=r['active']==1;evidence.append(r);phase=5
  elif phase==5 and age>2:
   checks['final_expiry']=r['active']==0;finish()
 except Exception as e:finish(repr(e))
handle=u.register_slate_post_tick_callback(tick)
