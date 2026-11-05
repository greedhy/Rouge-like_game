"""Configure only the new QA map to use the original Actor/GAS player framework."""
import unreal as u
A=u.EditorAssetLibrary
base='/Game/CompositionBatch6/QA'
path=base+'/BP_ActorQAGameMode'
bp=u.load_asset(path) if A.does_asset_exist(path) else u.AssetToolsHelpers.get_asset_tools().duplicate_asset('BP_ActorQAGameMode',base,u.load_asset('/Game/BluePrints/Game/BP_MyGameMode'))
assert bp
u.BlueprintEditorLibrary.compile_blueprint(bp)
cls=A.load_blueprint_class(path);cdo=u.get_default_object(cls)
cdo.set_editor_property('default_pawn_class',A.load_blueprint_class('/Game/BluePrints/Characters/Player/BP_Player'))
cdo.set_editor_property('player_controller_class',A.load_blueprint_class('/Game/BluePrints/Player/BP_PlayerController'))
cdo.set_editor_property('ability_info',u.load_asset('/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo'))
assert A.save_loaded_asset(bp,False)
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert levels.load_level(base+'/L_Batch6Test')
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property('default_game_mode',cls)
assert levels.save_current_level()
u.log('BATCH6_ACTOR_QA_READY')
