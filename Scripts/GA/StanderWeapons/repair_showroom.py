"""Repair only known owned showroom fixture transforms; retain its other actors."""
import unreal as u,json
from pathlib import Path
root=Path(u.Paths.project_dir());assert not u.EditorLevelLibrary.get_game_world();assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages();assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
path='/Game/BluePrints/Characters/Stander/Weapons/L_StanderWeapons';assert u.EditorLoadingAndSavingUtils.load_map(path)
w=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world();actors=u.get_editor_subsystem(u.EditorActorSubsystem);manifest=json.loads((root/'Scripts/GA/StanderWeapons/weapon-manifest.json').read_text(encoding='utf-8'));report={'pickups':[]}
pickups=u.GameplayStatics.get_all_actors_of_class(w,u.EquipmentSoulPickup)
for i,f in enumerate(manifest['families']):
 matches=[a for a in pickups if a.get_actor_label()=='Stander weapon '+f['family']];assert len(matches)==1
 a=matches[0];component=a.get_editor_property('pickup');identity=u.PrimaryAssetId();identity.import_text('BackpackItem:'+f['item_id']);assert f['item_id'] in identity.export_text();component.set_editor_property('item_definition_id',identity);assert f['item_id'] in component.get_editor_property('item_definition_id').export_text();loc=u.Vector((i%4)*350-350,(i//4)*350-350,50);a.set_actor_location(loc,False,True);got=a.get_actor_location();assert (got-loc).length()<.01
 report['pickups'].append({'family':f['family'],'location':[got.x,got.y,got.z],'definition':a.get_editor_property('pickup').get_editor_property('item_definition_id').export_text()})
floors=u.GameplayStatics.get_all_actors_of_class(w,u.StaticMeshActor);assert len(floors)==1;floors[0].set_actor_location(u.Vector(0,0,-10),False,True);floors[0].set_actor_scale3d(u.Vector(35,30,.2))
starts=u.GameplayStatics.get_all_actors_of_class(w,u.TravelPlayerStart);assert len(starts)==1;starts[0].set_actor_location(u.Vector(-600,0,100),False,True)
lights=u.GameplayStatics.get_all_actors_of_class(w,u.DirectionalLight)
light=lights[0] if lights else actors.spawn_actor_from_class(u.DirectionalLight,u.Vector(0,0,800));light.set_actor_label('Stander showroom sunlight');light.set_actor_rotation(u.Rotator(pitch=-55,yaw=-30),False)
light.get_component_by_class(u.DirectionalLightComponent).set_editor_property('intensity',6.)
assert u.EditorLoadingAndSavingUtils.save_map(w,path);report.update(map=path,dirty_maps=[p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()],dirty_content=[p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()])
(root/'Saved/GAAuthoring/StanderWeapons/showroom-readback.json').write_text(json.dumps(report,indent=2));print('Showroom positions repaired')
