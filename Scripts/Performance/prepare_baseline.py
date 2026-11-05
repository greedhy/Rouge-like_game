import unreal as u,json
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve();out=root/'Saved/Performance';out.mkdir(exist_ok=True)
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
(out/'previous-map.txt').write_text(world.get_path_name().split('.')[0])
path='/Game/Performance/L_MonsterBaseline'
assert levels.load_level(path) if u.EditorAssetLibrary.does_asset_exist(path) else levels.new_level_from_template(path,'/Game/CompositionBatch6/QA/L_Batch6Test')
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
for region in u.GameplayStatics.get_all_actors_of_class(world,u.PureMassSpawnRegion):u.get_editor_subsystem(u.EditorActorSubsystem).destroy_actor(region)
world.get_world_settings().set_editor_property('default_game_mode',u.load_class(None,'/Game/PureMass/BP_PureMassGameMode.BP_PureMassGameMode_C'))
source=u.load_asset('/Game/PureMass/DA_MassGoblin')
typepath='/Game/Performance/DA_BenchmarkGoblin'
monster=u.load_asset(typepath) if u.EditorAssetLibrary.does_asset_exist(typepath) else u.AssetToolsHelpers.get_asset_tools().duplicate_asset('DA_BenchmarkGoblin','/Game/Performance',source)
# Controlled survival workload: attacks retain their scheduling but cannot kill the stationary target.
monster.set_editor_property('attack_damage',0.)
u.EditorAssetLibrary.save_loaded_asset(monster,False)
assert levels.save_current_level()
(out/'prepared.json').write_text(json.dumps({'map':path,'type':typepath,'attack_damage':0,'source_type_unchanged':source.attack_damage,'player_starts':[str(a.get_actor_location()) for a in u.GameplayStatics.get_all_actors_of_class(world,u.PlayerStart)]},indent=2))
