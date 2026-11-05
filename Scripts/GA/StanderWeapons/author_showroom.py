"""Create a dedicated review map only; do not replace existing run levels."""
import unreal as u,json
from pathlib import Path
assert not u.EditorLevelLibrary.get_game_world()
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages() and not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
root=Path(u.Paths.project_dir());manifest=json.loads((root/'Scripts/GA/StanderWeapons/weapon-manifest.json').read_text(encoding='utf-8'));path='/Game/BluePrints/Characters/Stander/Weapons/L_StanderWeapons'
assert not u.EditorAssetLibrary.does_asset_exist(path),'Map already exists: inspect it instead of overwriting'
u.EditorLoadingAndSavingUtils.new_blank_map(False);world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world();actors=u.get_editor_subsystem(u.EditorActorSubsystem)
world.get_world_settings().set_editor_property('default_game_mode',u.EditorAssetLibrary.load_blueprint_class('/Game/BluePrints/Characters/Stander/BP_StanderGameMode'))
floor=actors.spawn_actor_from_class(u.StaticMeshActor,u.Vector(0,0,-10));floor.static_mesh_component.set_static_mesh(u.load_asset('/Engine/BasicShapes/Cube'));floor.set_actor_scale3d(u.Vector(35,30,.2));floor.set_actor_location(u.Vector(0,0,-10),False,True)
start=actors.spawn_actor_from_class(u.TravelPlayerStart,u.Vector(-600,0,100));start.set_actor_location(u.Vector(-600,0,100),False,True);start.set_editor_property('player_start_tag','RG_Entrance');start.set_editor_property('default_entry_group',True)
for i,f in enumerate(manifest['families']):
 loc=u.Vector((i%4)*350-350,(i//4)*350-350,50);pickup=actors.spawn_actor_from_class(u.EquipmentSoulPickup,loc);pickup.set_actor_location(loc,False,True);pickup.set_actor_label('Stander weapon '+f['family']);component=pickup.get_editor_property('pickup')
 component.set_editor_property('item_definitions',u.load_asset('/Game/BluePrints/DA/DA_BackpackItemDefinitions'))
 identity=u.PrimaryAssetId();identity.import_text('BackpackItem:'+f['item_id']);assert f['item_id'] in identity.export_text();component.set_editor_property('item_definition_id',identity);component.set_editor_property('quantity',1)
assert u.EditorLoadingAndSavingUtils.save_map(world,path)
(root/'Saved/GAAuthoring/StanderWeapons/showroom-result.json').write_text(json.dumps({'map':path,'pickups':len(manifest['families'])},indent=2))
