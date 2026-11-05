import unreal as u,json,time
from pathlib import Path
w=u.EditorLevelLibrary.get_game_world();assert w and w.get_path_name().startswith('/Temp/')
p=u.GameplayStatics.get_player_pawn(w,0);assert isinstance(p,u.StanderPlayer),str(p)
asc=p.get_editor_property('player_state').get_component_by_class(u.MyAbilitySystemComponent)
targets=[next(a for a in u.GameplayStatics.get_all_actors_of_class(w,u.EnemyCharacter) if a.actor_has_tag(n)) for n in ['Target','Friend','Outside']]
def attrs(a):return (asc if a==p else a.get_component_by_class(u.MyAbilitySystemComponent)).get_attribute_set(u.MyAttributeSet)
def val(a,n):return attrs(a).get_editor_property(n).get_editor_property('current_value')
def assign(a,n,v):
 f=attrs(a).get_editor_property(n);f.import_text('(BaseValue='+str(v)+',CurrentValue='+str(v)+')');attrs(a).set_editor_property(n,f)
for a in targets:
 a.get_component_by_class(u.CharacterMovementComponent).disable_movement()
 a.get_component_by_class(u.CapsuleComponent).set_collision_response_to_channel(u.CollisionChannel.ECC_PAWN,u.CollisionResponseType.ECR_IGNORE)
 for n,v in [('health',10000.),('max_health',10000.),('armor',0.),('block_chance',0.),('critical_hit_chance',0.),('physics_resistance',0.)]:
  try:assign(a,n,v)
  except Exception:pass
for n,v in [('critical_hit_chance',0.),('mana',100.),('max_mana',100.)]:assign(p,n,v)
p.set_actor_location(u.Vector(0,0,89),False,True);p.set_actor_rotation(u.Rotator(),False)
classes={n:u.load_class(None,'/Game/BluePrints/Characters/Stander/GA_Stander_'+n+'.GA_Stander_'+n+'_C') for n in ['Combo','Guard','Spin','Lift','ShieldBash']}
guard_tag=u.GameplayTag();guard_tag.import_text('(TagName="State.SwordShield.Guarding")')
cases=['Combo','Combo','Combo','Guard','Spin','Lift','ShieldBash'];index=0;case=None;began=time.monotonic()
report={'status':'running','cases':[]};out=Path(u.Paths.project_dir())/'Saved/GAAuthoring/StanderMass/actor-runtime.json'
u.GameplayStatics.set_global_time_dilation(w,.5)
def tick(dt):
 global index,case
 try:
  now=u.GameplayStatics.get_time_seconds(w)
  if case is None:
   name=cases[index];p.set_actor_rotation(u.Rotator(),False)
   case={'name':name,'start':now,'hp_before':[val(a,'health') for a in targets],'mana_before':val(p,'mana'),'accepted':asc.try_activate_ability_by_class(classes[name]),'samples':[]}
   assert case['accepted'],'Rejected '+name
   forward=p.get_actor_forward_vector();right=p.get_actor_right_vector()
   for a,offset in zip(targets,[forward*120,forward*110+right*40,forward*450]):a.set_actor_location(u.Vector(offset.x,offset.y,89),False,True)
   case['repeat_rejected']=not asc.try_activate_ability_by_class(classes[name])
  anim=p.get_component_by_class(u.SkeletalMeshComponent).get_anim_instance();m=anim.get_current_active_montage()
  case['samples'].append({'t':now-case['start'],'hp':[val(a,'health') for a in targets],'mana':val(p,'mana'),'block':val(p,'block_chance'),'guarding':asc.has_matching_gameplay_tag(guard_tag),'montage':m.get_name() if m else None,'position':anim.montage_get_position(m) if m else None,'yaw':p.get_actor_rotation().yaw})
  if now-case['start']>(1.1 if case['name']=='Guard' else 1.55):
   samples=case['samples'];loss=[b-a for b,a in zip(case['hp_before'],samples[-1]['hp'])]
   case['damage_events']=sum(a['hp'][0]>b['hp'][0] for a,b in zip(samples,samples[1:]))
   case['loss']=loss;case['mana_spent']=case['mana_before']-samples[-1]['mana'];case['passed']=case['repeat_rejected'] and loss[1:]==[0.,0.] and any(s['montage'] for s in samples)
   if case['name']=='Guard':case['passed']=case['passed'] and loss[0]==0 and any(s['guarding'] and s['t']>=.7 for s in samples) and not samples[-1]['guarding']
   else:case['passed']=case['passed'] and loss[0]>0 and case['damage_events']==1 and case['mana_spent']==({'Spin':15.,'Lift':18.,'ShieldBash':10.}.get(case['name'],0.))
   report['cases'].append(case);index+=1;case=None
   if index==len(cases):report.update(status='complete',passed=all(c['passed'] for c in report['cases']))
  if time.monotonic()-began>90:raise RuntimeError('Timeout')
 except Exception as e:report.update(status='error',passed=False,error=str(e))
 if report['status']!='running':u.GameplayStatics.set_global_time_dilation(w,1.);u.unregister_slate_post_tick_callback(handle)
 out.write_text(json.dumps(report,indent=2))
handle=u.register_slate_post_tick_callback(tick)
