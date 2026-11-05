"""Clone spear rendering resources; match the existing Actor socket and strike clip."""
import unreal as u,json,uuid,shutil
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve();out=root/'Saved/GAAuthoring/MassGoblinArmed'/uuid.uuid4().hex;out.mkdir(parents=True)
assert not u.EditorLevelLibrary.get_game_world(),'Stop PIE first'
assert all(p.get_path_name()=='/Game/RoguelikeGeneration/Gameplay/DA_DungeonMassMelee' for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()),'Unsaved unrelated assets'
dest='/Game/PureMass/Rendering'
def clone(source,path):
 if u.EditorAssetLibrary.does_asset_exist(path):
  obj=u.load_asset(path)
  assert u.EditorAssetLibrary.get_metadata_tag(obj,'AuthoringOwner')=='MassGoblinArmedV1',path
  return obj
 obj=u.EditorAssetLibrary.duplicate_asset(source,path);assert obj
 u.EditorAssetLibrary.set_metadata_tag(obj,'AuthoringOwner','MassGoblinArmedV1');return obj
def material_copy(mat):
 copy=clone(mat.get_path_name(),dest+'/'+mat.get_name()+'_MassSpear')
 if isinstance(copy,u.MaterialInstanceConstant):
  u.MaterialEditingLibrary.set_material_instance_parent(copy,material_copy(mat.get_editor_property('parent')))
 else:
  for usage in [u.MaterialUsage.MATUSAGE_SKELETAL_MESH,u.MaterialUsage.MATUSAGE_INSTANCED_SKINNED_MESH]:u.MaterialEditingLibrary.set_material_usage(copy,usage)
  u.MaterialEditingLibrary.recompile_material(copy)
 assert u.EditorAssetLibrary.save_loaded_asset(copy,False)
 return copy
cdo=u.get_default_object(u.EditorAssetLibrary.load_blueprint_class('/Game/BluePrints/Characters/Goblin/BP_GoblinSpear'))
weapon=cdo.get_editor_property('weapon');source=weapon.get_editor_property('skeletal_mesh_asset')
mesh=clone(source.get_path_name(),dest+'/SKM_MassGoblinSpear')
slots=list(source.get_editor_property('materials'))
for slot in slots:slot.set_editor_property('material_interface',material_copy(slot.get_editor_property('material_interface')))
mesh.set_editor_property('materials',slots)
editor=u.get_editor_subsystem(u.SkeletalMeshEditorSubsystem)
for i in range(editor.get_lod_count(mesh)):
 settings=editor.get_lod_build_settings(mesh,i);settings.set_editor_property('optimize_for_instancing',True);editor.set_lod_build_settings(mesh,i,settings)
assert u.EditorAssetLibrary.save_loaded_asset(mesh,False)
clip=u.load_asset('/Game/Assets/Enemies/Goblin/Animations/Spear/Attack_Spear')
rows=[]
for path in ['/Game/PureMass/DA_MassGoblin','/Game/RoguelikeGeneration/Gameplay/DA_DungeonGoblin','/Game/RoguelikeGeneration/Gameplay/DA_DungeonMassMelee']:
 t=u.load_asset(path);assert t
 shutil.copy2(root/'Content'/(path.removeprefix('/Game/')+'.uasset'),out/(t.get_name()+'.uasset'))
 before={'health':t.health,'damage':t.attack_damage,'period':t.attack_period}
 t.set_editor_property('weapon_mesh',mesh);t.set_editor_property('weapon_socket',weapon.get_attach_socket_name())
 t.set_editor_property('weapon_offset',u.Transform(location=weapon.get_editor_property('relative_location'),rotation=weapon.get_editor_property('relative_rotation'),scale=weapon.get_editor_property('relative_scale3d')))
 t.set_editor_property('attack_contact_time',.29);t.set_editor_property('attack_clip_duration',clip.get_play_length())
 if t.attack_windup_seconds<=0:t.set_editor_property('attack_windup_seconds',.35)
 assert u.EditorAssetLibrary.save_loaded_asset(t,False)
 rows.append({'path':path,'health':t.health,'before':before,'weapon':t.weapon_mesh.get_path_name(),'socket':str(t.weapon_socket),'clip_duration':t.attack_clip_duration,'contact':t.attack_contact_time,'windup':t.attack_windup_seconds})
(out/'assets.json').write_text(json.dumps(rows,indent=2));(out.parent/'latest-assets.txt').write_text(str(out))
