"""Create a dedicated Batch6 test copy; do not modify original map population."""
import unreal as u
path='/Game/CompositionBatch6/QA/L_Batch6Test'
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
if not u.EditorAssetLibrary.does_asset_exist(path):
    assert levels.new_level_from_template(path,'/Game/PrototypeExtensions/Batch5/L_PrototypeLab')
else:
    assert levels.load_level(path)
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().split('.')[0]==path
for region in u.GameplayStatics.get_all_actors_of_class(world,u.PureMassSpawnRegion):
    assert u.get_editor_subsystem(u.EditorActorSubsystem).destroy_actor(region)
assert levels.save_current_level()
u.log('BATCH6_QA_MAP_READY '+path)
