"""PIE benchmark. CSV scopes are engine measurements; Python only orchestrates phases."""
import unreal as u,time,json,uuid,math
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve();out=root/'Saved/Performance'/uuid.uuid4().hex;out.mkdir()
settings=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
old=settings.get_editor_property('bThrottleCPUWhenNotForeground');settings.set_editor_property('bThrottleCPUWhenNotForeground',False)
state={'out':str(out),'phase':'setup','case':0,'cases':[],'started':time.monotonic()}
cases=[('empty',0,100),('spread100',100,120),('spread200',200,120),('dense200',200,70)]
def command(s):u.SystemLibrary.execute_console_command(world,s)
def finish(error=None):
 u.unregister_slate_post_tick_callback(handle)
 if 'world' in globals():
  if state['phase']=='capture':command('csvprofile stop')
  for key,value in state.get('previous_cvars',{}).items():command(key+' '+str(value))
 settings.set_editor_property('bThrottleCPUWhenNotForeground',old)
 state['error']=error;state['passed']=error is None
 (out/'run.json').write_text(json.dumps(state,indent=2));(root/'Saved/Performance/latest-run.txt').write_text(str(out))
def tick(dt):
 global world,sim,pawn,origin
 try:
  now=time.monotonic()
  if now-state['started']>240:raise RuntimeError('benchmark timeout')
  if state['phase']=='setup':
   sims=[s for s in u.ObjectIterator(u.PureMassSubsystem) if s.get_world() and 'UEDPIE_' in s.get_world().get_path_name() and u.GameplayStatics.get_game_mode(s.get_world())]
   if not sims:return
   assert len(sims)==1,'Standalone PIE only'
   sim=sims[0];world=sim.get_world();pawn=u.GameplayStatics.get_player_pawn(world,0)
   if not pawn:return
   state['previous_cvars']={key:u.SystemLibrary.get_console_variable_float_value(key) for key in ['t.MaxFPS','r.VSync']}
   origin=u.Vector(0,-2000,100);pawn.set_actor_location(origin,False,True)
   command('CsvCategory MonsterPerf enable');command('t.MaxFPS 0');command('r.VSync 0');state['phase']='spawn'
  if state['phase']=='spawn':
   for v in sim.get_snapshots():sim.remove_monster(v.id)
   name,count,spacing=cases[state['case']];cols=max(1,math.ceil(math.sqrt(count)))
   monster=u.load_asset('/Game/Performance/DA_BenchmarkGoblin')
   positions=[]
   for i in range(count):
    pos=origin+u.Vector((i%cols-cols/2)*spacing,(i//cols-cols/2)*spacing,0)
    nav=u.NavigationSystemV1.project_point_to_navigation(world,pos,None,None,u.Vector(200,200,500))
    assert nav is not None,'No navigation at spawn'
    pos=nav+u.Vector(0,0,monster.half_height+1)
    assert sim.spawn_monster(monster,pos)>0
    positions.append([pos.x,pos.y,pos.z])
   state['cases'].append({'name':name,'count':count,'positions':positions,'warmup_seconds':5,'sample_seconds':15})
   state['deadline']=now+5;state['phase']='warmup';return
  if state['phase']=='warmup' and now>=state['deadline']:
   assert len(sim.get_snapshots())==cases[state['case']][1]
   state['csv_before']=[str(p) for p in (root/'Saved/Profiling/CSV').glob('*.csv')]
   command('csvprofile start');state['deadline']=now+15;state['phase']='capture';return
  if state['phase']=='capture' and now>=state['deadline']:
   command('csvprofile stop');state['cases'][-1]['actual_count']=len(sim.get_snapshots())
   state['cases'][-1]['moving_count_end']=sum(v.velocity.length()>1 for v in sim.get_snapshots())
   state['deadline']=now+3;state['phase']='flush';return
  if state['phase']=='flush' and now>=state['deadline']:
   files=[str(p) for p in (root/'Saved/Profiling/CSV').glob('*.csv') if str(p) not in state['csv_before']]
   assert len(files)==1,files
   state['cases'][-1]['csv']=files[0]
   state['case']+=1
   if state['case']==len(cases):finish();return
   state['phase']='spawn'
 except Exception as e:finish(repr(e))
handle=u.register_slate_post_tick_callback(tick)
