"""Execute every authored weapon action in a temporary PIE fixture via real equipment grants."""
import unreal as u,json,time,uuid,traceback
from pathlib import Path
root=Path(u.Paths.project_dir());w=u.EditorLevelLibrary.get_game_world();assert w and w.get_path_name().startswith('/Temp/')
p=u.GameplayStatics.get_player_pawn(w,0);assert isinstance(p,u.StanderPlayer)
ps=p.get_editor_property('player_state');asc=ps.get_component_by_class(u.MyAbilitySystemComponent);eq=ps.get_equipment_component()
original=ps.export_backpack_snapshot();manifest=json.loads((root/'Scripts/GA/StanderWeapons/weapon-manifest.json').read_text(encoding='utf-8'))
base='/Game/BluePrints/Characters/Stander/Weapons';cases=[]
for f in sorted(manifest['families'],key=lambda f:0 if f['family'] in ['Bow','Pistol','Magic','Staff'] else 1):
 for a in f['actions']:cases.append((f,a))
targets={n:next(a for a in u.GameplayStatics.get_all_actors_of_class(w,u.EnemyCharacter) if a.actor_has_tag(n)) for n in ['Target','Friend','Outside']}
def attr(actor):return (asc if actor==p else actor.get_component_by_class(u.MyAbilitySystemComponent)).get_attribute_set(u.MyAttributeSet)
def value(actor,name):return attr(actor).get_editor_property(name).get_editor_property('current_value')
def assign(actor,name,x):
 field=attr(actor).get_editor_property(name);field.import_text('(BaseValue='+str(x)+',CurrentValue='+str(x)+')');attr(actor).set_editor_property(name,field)
def tag(name):
 t=u.GameplayTag();t.import_text('(TagName="'+name+'")');return t
for actor in targets.values():
 actor.get_component_by_class(u.CharacterMovementComponent).disable_movement()
 actor.get_component_by_class(u.CapsuleComponent).set_collision_response_to_channel(u.CollisionChannel.ECC_PAWN,u.CollisionResponseType.ECR_IGNORE)
 for name,x in [('max_health',100000),('health',100000),('armor',0),('block_chance',0),('physics_resistance',0),('magic_resistance',0)]:
  try:assign(actor,name,x)
  except Exception:pass
sm=next(x for x in u.ObjectIterator(u.PureMassSubsystem) if x.get_world()==w);sm.enable();template=u.load_asset('/Game/PureMass/DA_MassQAGoblin');mt=u.new_object(u.PureMassMonsterType)
for key in ['mesh','animation_provider','mesh_offset','idle_animation','run_animation','attack_animation','radius','half_height']:mt.set_editor_property(key,template.get_editor_property(key))
for key,x in dict(health=100000.,speed=0.,attack_damage=0.,armor=0.,block_chance=0.,attack_period=10.).items():mt.set_editor_property(key,x)
run=uuid.uuid4().hex;out=root/'Saved/GAAuthoring/StanderWeapons'/('runtime-'+run+'.json');latest=root/'Saved/GAAuthoring/StanderWeapons/runtime-latest.json'
report={'run_id':run,'expected':len(cases),'status':'running','cases':[]};current=None;index=0;family_name=None;started=time.monotonic()
def flush():out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');latest.write_text(json.dumps({'report':str(out),'status':report['status'],'completed':len(report['cases'])}))
def install(f):
 snapshot=u.BackpackSnapshot();snapshot.set_editor_property('capacity',1)
 slot=u.BackpackSnapshotSlot();slot.import_text('(ItemDefinitionId="BackpackItem:'+f['item_id']+'",ItemInstanceId='+u.GuidLibrary.new_guid().export_text()+',Quantity=1)')
 snapshot.set_editor_property('slots',[slot]);snapshot.set_editor_property('equipment',[])
 assert ps.import_backpack_snapshot(snapshot),'Import '+f['family']
 eq.request_equip(0,tag('Equipment.Slot.Weapon'))
 assert eq.get_weapon_set() and str(eq.get_weapon_set().get_editor_property('family'))==f['family'],'Equip '+f['family']
def tick(dt):
 global current,index,family_name
 try:
  now=u.GameplayStatics.get_time_seconds(w)
  if current is None:
   f,a=cases[index]
   if f['family']!=family_name:install(f);family_name=f['family']
   asc.remove_active_gameplay_effect_by_source_effect(u.SwordTimedEffect,asc,-1)
   for name,x in [('max_health',10000),('health',10000),('max_mana',10000),('mana',10000),('critical_hit_chance',0)]:assign(p,name,x)
   p.set_actor_location(u.Vector(0,0,98),False,True);p.get_component_by_class(u.CharacterMovementComponent).set_movement_mode(u.MovementMode.MOVE_WALKING)
   ga=u.load_class(None,base+'/'+family_name+'/GA_'+a['name']+'.GA_'+a['name']+'_C');cfg=u.get_default_object(ga).get_editor_property('action_config');montage=cfg.get_editor_property('montage');events=cfg.get_editor_property('events')
   accepted=asc.try_activate_ability_by_class(ga);assert accepted,'Activation rejected '+family_name+'/'+a['name']
   forward=p.get_actor_forward_vector();right=p.get_actor_right_vector()
   attacks=[e for e in events if e.get_editor_property('kind') in [u.StanderWeaponActionKind.MELEE,u.StanderWeaponActionKind.PROJECTILE]]
   contact=attacks[0].get_editor_property('time') if attacks else 0
   moves=[e for e in events if e.get_editor_property('kind')==u.StanderWeaponActionKind.MOVE]
   displacement=sum(e.get_editor_property('move_distance')*max(0,min(1,(contact-e.get_editor_property('time'))/e.get_editor_property('duration'))) for e in moves)
   distance=displacement+(260 if a['kind']=='Projectile' else min(110,a['radius']*.6))
   target_location=p.get_actor_location()+forward*distance;target_location.z=98
   mass=index%2==1 and bool(attacks)
   entity=sm.spawn_monster(mt,target_location,'None') if mass else 0
   targets['Target'].set_actor_location(u.Vector(1300,1300,98) if mass else target_location,False,True)
   targets['Friend'].set_actor_location(target_location+right*50,False,True);targets['Outside'].set_actor_location(p.get_actor_location()+forward*1300,False,True)
   current={'family':family_name,'action':a['name'],'kind':a['kind'],'representation':'Mass' if mass else 'Actor','start':now,'entity':entity,'hp':value(targets['Target'],'health'),'friend_hp':value(targets['Friend'],'health'),'outside_hp':value(targets['Outside'],'health'),'duration':montage.get_play_length()/cfg.get_editor_property('play_rate')+1.4,'mana':cfg.get_editor_property('mana_cost'),'saw_montage':False,'saw_guard':False,'peak_height':98.,'base_position':p.get_actor_location(),'repeat_rejected':not asc.try_activate_ability_by_class(ga)}
  anim=p.get_component_by_class(u.SkeletalMeshComponent).get_anim_instance();current['saw_montage']|=bool(anim.get_current_active_montage());current['saw_guard']|=asc.has_matching_gameplay_tag(tag('State.SwordShield.Guarding'));current['peak_height']=max(current['peak_height'],p.get_actor_location().z)
  if now-current['start']>current['duration']:
   snapshots=sm.get_snapshots();view=next((x for x in snapshots if x.id==current['entity']),None)
   loss=(100000-view.health if view else 100000) if current['entity'] else current['hp']-value(targets['Target'],'health')
   movement=(p.get_actor_location()-current.pop('base_position')).length()
   safe=current['friend_hp']==value(targets['Friend'],'health') and current['outside_hp']==value(targets['Outside'],'health')
   passed=current['saw_montage'] and current['repeat_rejected'] and safe and abs((10000-value(p,'mana'))-current['mana'])<.1 and not asc.has_matching_gameplay_tag(tag('State.Stander.Busy'))
   if current['kind']=='Guard':passed=passed and current['saw_guard'] and not asc.has_matching_gameplay_tag(tag('State.SwordShield.Guarding'))
   elif current['kind']=='Evade':passed=passed and movement>50
   else:passed=passed and loss>0
   if 'airborne' in cases[index][1]['flags']:passed=passed and current['peak_height']>135 and p.get_actor_location().z<110
   current.update(loss=loss,movement=movement,passed=passed)
   if current['entity']:sm.remove_monster(current['entity'],u.PureMassRemovalReason.RECYCLED)
   report['cases'].append(current);index+=1;current=None;flush()
   if index==len(cases):report.update(status='complete',passed=all(c['passed'] for c in report['cases']))
  if time.monotonic()-started>2400:raise RuntimeError('Runtime suite timeout')
 except Exception as exc:report.update(status='error',passed=False,error=str(exc),traceback=traceback.format_exc())
 if report['status']!='running':
  u.unregister_slate_post_tick_callback(handle)
  if current and current.get('entity'):sm.remove_monster(current['entity'],u.PureMassRemovalReason.RECYCLED)
  ps.import_backpack_snapshot(original);flush()
handle=u.register_slate_post_tick_callback(tick);flush()

