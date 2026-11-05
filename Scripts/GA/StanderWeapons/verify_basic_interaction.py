"""Run in the owned temporary Stander PIE after rebuilding; never changes saved inventory."""
import unreal as u,json,traceback
from pathlib import Path
w=u.EditorLevelLibrary.get_game_world();assert w and w.get_path_name().startswith('/Temp/')
p=u.GameplayStatics.get_player_pawn(w,0);assert isinstance(p,u.StanderPlayer)
pc=u.GameplayStatics.get_player_controller(w,0);ps=p.get_editor_property('player_state');original=ps.export_backpack_snapshot();location=p.get_actor_location()
out=Path(u.Paths.project_dir())/'Saved/GAAuthoring/StanderBasicInput/pie-interaction.json'
report={'passed':False};pickup_actor=None
try:
 capsule=p.get_component_by_class(u.CapsuleComponent)
 assert capsule.get_editor_property('generate_overlap_events'),'Player capsule interaction is disabled'
 report['capsule_overlap']=True
 transform=u.Transform(location=location+u.Vector(30,0,0));lib=u.get_default_object(u.GameplayStatics)
 pickup_actor=lib.call_method('BeginDeferredActorSpawnFromClass',(w,u.EquipmentSoulPickup.static_class(),transform,u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None))
 pickup=pickup_actor.get_component_by_class(u.WorldPickupComponent);asset=u.PrimaryAssetId();asset.import_text('BackpackItem:StanderWeaponHammer');pickup.set_editor_property('item_definition_id',asset)
 lib.call_method('FinishSpawningActor',(pickup_actor,transform))
 # Enter after BeginPlay registered its callbacks, as a real approaching player does.
 p.set_actor_location(location+u.Vector(-400,0,0),False,True);p.set_actor_location(location,False,True)
 assert pickup_actor.get_component_by_class(u.SphereComponent).is_overlapping_actor(p)
 before=ps.export_backpack_snapshot().export_text();pc.call_method('ServerRequestPickup')
 assert not u.SystemLibrary.is_valid(pickup_actor),'Pickup RPC did not collect the nearby weapon'
 after=ps.export_backpack_snapshot().export_text();assert after!=before and 'StanderWeaponHammer' in after
 report['pickup_rpc_and_inventory']=True
 pc.open_backpack_ui();assert pc.is_backpack_ui_open();pc.close_backpack_ui();assert not pc.is_backpack_ui_open()
 report['backpack_ui']=True
 report['input_actions']=[x.export_text() for x in u.load_asset('/Game/BluePrints/Input/DA_MyInputConfig').get_editor_property('ability_input_actions')]
 assert all(any(tag in row for row in report['input_actions']) for tag in ['InputTag.Pickup','InputTag.Backpack','InputTag.LMB'])
 report['passed']=True
except Exception:
 report['error']=traceback.format_exc()
finally:
 pc.close_backpack_ui();ps.import_backpack_snapshot(original);p.set_actor_location(location,False,True)
 if pickup_actor and u.SystemLibrary.is_valid(pickup_actor):pickup_actor.destroy_actor()
 out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
assert report['passed'],report.get('error')
