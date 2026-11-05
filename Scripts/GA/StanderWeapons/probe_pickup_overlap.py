import unreal as u,json
from pathlib import Path
w=u.EditorLevelLibrary.get_game_world();p=u.GameplayStatics.get_player_pawn(w,0);assert isinstance(p,u.StanderPlayer)
pc=u.GameplayStatics.get_player_controller(w,0);ps=p.get_editor_property('player_state');original=ps.export_backpack_snapshot()
for old in u.GameplayStatics.get_all_actors_of_class(w,u.EquipmentSoulPickup):old.destroy_actor()
transform=u.Transform(location=p.get_actor_location()+u.Vector(30,0,0));a=u.get_default_object(u.GameplayStatics).call_method('BeginDeferredActorSpawnFromClass',(w,u.EquipmentSoulPickup.static_class(),transform,u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None));u.get_default_object(u.GameplayStatics).call_method('FinishSpawningActor',(a,transform));pickup=a.get_component_by_class(u.WorldPickupComponent)
asset=u.PrimaryAssetId();asset.import_text('BackpackItem:StanderWeaponHammer');pickup.set_editor_property('item_definition_id',asset)
capsule=p.get_component_by_class(u.CapsuleComponent);mesh=p.get_component_by_class(u.SkeletalMeshComponent)
r={'controller':pc.get_class().get_path_name(),'physics':str(mesh.get_editor_property('skeletal_mesh_asset').get_editor_property('physics_asset')),'before_capsule':capsule.get_editor_property('generate_overlap_events'),'before_overlap':a.get_component_by_class(u.SphereComponent).is_overlapping_actor(p)}
pc.call_method('ServerRequestPickup');r['before_collected']=not u.SystemLibrary.is_valid(a)
if u.SystemLibrary.is_valid(a):
 capsule.call_method('SetGenerateOverlapEvents',(True,));p.set_actor_location(p.get_actor_location()+u.Vector(1,0,0),False,True)
 r['after_overlap']=a.get_component_by_class(u.SphereComponent).is_overlapping_actor(p)
 pc.call_method('ServerRequestPickup');r['after_collected']=not u.SystemLibrary.is_valid(a)
capsule.call_method('SetGenerateOverlapEvents',(r['before_capsule'],));ps.import_backpack_snapshot(original)
if u.SystemLibrary.is_valid(a):a.destroy_actor()
(Path(u.Paths.project_dir())/'Saved/GAAuthoring/StanderWeapons/pickup-root-cause.json').write_text(json.dumps(r,indent=2))




