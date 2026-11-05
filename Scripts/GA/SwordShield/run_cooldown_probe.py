import unreal as u,json,time
from pathlib import Path
root=Path(u.Paths.project_dir())
exec((root/'Scripts/GA/SwordShield/run_combat_probe.py').read_text().split("cases=['Combo'")[0])
cases=[('Guard',3.),('Spin',5.)];index=0;case=None;began=time.monotonic();next_at=0
report={'status':'running','cases':[]};out=root/'Saved/GAAuthoring/SwordShield/cooldown-runtime.json'
def tick(dt):
 global index,case,next_at
 try:
  now=u.GameplayStatics.get_time_seconds(w)
  if now<next_at:return
  if case is None:
   name,duration=cases[index];asc.remove_active_gameplay_effect_by_source_effect(u.SwordTimedEffect,asc,-1)
   case={'name':name,'duration':duration,'start':now,'accepted':asc.try_activate_ability_by_class(classes[name]),'checked':False}
   assert case['accepted'],'Initial activation rejected'
  elapsed=now-case['start']
  if elapsed>1.3 and not case['checked']:
   case['checked']=True;case['montage_ended']=not p.get_component_by_class(u.SkeletalMeshComponent).get_anim_instance().get_current_active_montage()
   case['cooldown_rejected']=not asc.try_activate_ability_by_class(classes[case['name']])
  if elapsed>case['duration']+.2:
   case['after_cooldown_accepted']=asc.try_activate_ability_by_class(classes[case['name']])
   p.get_component_by_class(u.SkeletalMeshComponent).get_anim_instance().montage_stop(0.)
   case['passed']=all(case[k] for k in ['accepted','montage_ended','cooldown_rejected','after_cooldown_accepted'])
   report['cases'].append(case);index+=1;case=None;next_at=now+.2
   if index==len(cases):report.update(status='complete',passed=all(c['passed'] for c in report['cases']))
  if time.monotonic()-began>30:raise RuntimeError('Cooldown probe timeout')
 except Exception as e:report.update(status='error',passed=False,error=str(e))
 if report['status']!='running':u.unregister_slate_post_tick_callback(handle)
 out.write_text(json.dumps(report,indent=2))
handle=u.register_slate_post_tick_callback(tick)
