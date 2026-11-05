"""Run after the damage suite: equipment locomotion, live menu selection and slot metadata."""
import unreal as u,json,time,uuid,traceback
from pathlib import Path
root=Path(u.Paths.project_dir());w=u.EditorLevelLibrary.get_game_world();assert w and w.get_path_name().startswith('/Temp/')
p=u.GameplayStatics.get_player_pawn(w,0);ps=p.get_editor_property('player_state');asc=ps.get_component_by_class(u.MyAbilitySystemComponent);eq=ps.get_equipment_component();original=ps.export_backpack_snapshot();pc=u.GameplayStatics.get_player_controller(w,0)
manifest=json.loads((root/'Scripts/GA/StanderWeapons/weapon-manifest.json').read_text(encoding='utf-8'));families=manifest['families'];index=0;state=None;report={'status':'running','cases':[]};out=root/'Saved/GAAuthoring/StanderWeapons/locomotion-ui.json';started=time.monotonic();menu=None
for enemy in u.GameplayStatics.get_all_actors_of_class(w,u.EnemyCharacter):enemy.set_actor_location(u.Vector(1300,1300,100),False,True)
def tag(name):
 t=u.GameplayTag();t.import_text('(TagName="'+name+'")');return t
def install(f):
 snapshot=u.BackpackSnapshot();snapshot.set_editor_property('capacity',1);slot=u.BackpackSnapshotSlot();slot.import_text('(ItemDefinitionId="BackpackItem:'+f['item_id']+'",ItemInstanceId='+u.GuidLibrary.new_guid().export_text()+',Quantity=1)');snapshot.set_editor_property('slots',[slot]);snapshot.set_editor_property('equipment',[])
 assert ps.import_backpack_snapshot(snapshot);eq.request_equip(0,tag('Equipment.Slot.Weapon'));assert eq.get_weapon_set()
def tick(dt):
 global index,state,menu
 try:
  now=u.GameplayStatics.get_time_seconds(w)
  if state is None:
   f=families[index];install(f);p.set_actor_location(u.Vector(-400,-500,98),False,True);move=p.get_component_by_class(u.CharacterMovementComponent);move.set_movement_mode(u.MovementMode.MOVE_WALKING)
   mesh=p.get_component_by_class(u.SkeletalMeshComponent);assert mesh.get_anim_instance().get_class()==eq.get_weapon_set().get_editor_property('anim_class')
   state={'family':f['family'],'start':now,'speed':0.,'feet':[]}
  p.add_movement_input(u.Vector(1,0,0),1.,True);state['speed']=max(state['speed'],p.get_velocity().length());mesh=p.get_component_by_class(u.SkeletalMeshComponent);foot=mesh.get_socket_location('foot_l')-p.get_actor_location();state['feet'].append([foot.x,foot.y,foot.z])
  if now-state['start']>1.1:
   p.get_component_by_class(u.CharacterMovementComponent).stop_movement_immediately()
   if menu is None:
    cls=u.load_class(None,'/Game/BluePrints/UI/SpellMenu/WBP_SpellMune.WBP_SpellMune_C');menu=u.get_default_object(u.load_class(None,'/Script/UMG.WidgetBlueprintLibrary')).call_method('Create',(w,cls,pc));menu.set_widget_controller(u.MyAbilitySystemLibrary.get_spell_menu_widget_controller(w));menu.add_to_viewport()
   tree=next(x for x in u.ObjectIterator(u.PrototypeSpellTree) if x.get_path_name().startswith(menu.get_path_name()+'.'));tree.call_method('ShowWeapons');menu.force_layout_prepass();f=families[index];grids=[g for g in u.ObjectIterator(u.UniformGridPanel) if g.get_path_name().startswith(tree.get_path_name()+'.') and g.get_children_count()==len(f['actions']) and all(isinstance(e,u.PrototypeSpellEntry) for e in g.get_all_children())];assert len(grids)==1,'Missing live weapon grid';entries=list(grids[0].get_all_children())
   assert len(entries)==len(f['actions']),'Weapon list missing actions '+f['family']
   cfg=u.load_asset('/Game/BluePrints/Characters/Stander/Weapons/'+f['family']+'/DA_'+f['actions'][-1]['name']);label=str(cfg.get_editor_property('display_name'))
   entries[-1].call_method('Select');menu.force_layout_prepass()
   assert any(label in str(t.get_text()) for t in u.ObjectIterator(u.RichTextBlock) if t.get_path_name().startswith(menu.get_path_name()+'.')),'Selected weapon description missing'
   controller=u.MyAbilitySystemLibrary.get_spell_menu_widget_controller(w);controller.equip_button_pressed();controller.spell_row_globe_pressed(tag('InputTag.4'),tag('Abilities.Type.Offensive'))
   actual_slot=asc.get_slot_from_ability_tag(cfg.get_editor_property('action_tag')).export_text();assert actual_slot==tag('InputTag.4').export_text(),'Extra action was not assigned to key 4: '+actual_slot
   values=state.pop('feet');span=sum((max(v[i] for v in values)-min(v[i] for v in values))**2 for i in range(3))**.5
   state.update(foot_motion=span,menu_entries=len(entries),selected=label,slot='InputTag.4',passed=state['speed']>150 and span>5)
   report['cases'].append(state);index+=1;state=None;out.write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
   if index==len(families):report.update(status='complete',passed=all(c['passed'] for c in report['cases']))
  if time.monotonic()-started>90:raise RuntimeError('UI locomotion timeout')
 except Exception as exc:report.update(status='error',passed=False,error=str(exc),traceback=traceback.format_exc())
 if report['status']!='running':
  u.unregister_slate_post_tick_callback(handle);p.get_component_by_class(u.CharacterMovementComponent).stop_movement_immediately()
  if menu:menu.remove_from_parent()
  ps.import_backpack_snapshot(original);out.write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
handle=u.register_slate_post_tick_callback(tick)

