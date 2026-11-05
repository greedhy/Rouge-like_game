"""Build BlendSpace interpolation data using the engine asset editor."""
import unreal as u
assert not u.EditorLevelLibrary.get_game_world()
a=u.load_asset('/Game/BluePrints/Characters/SwordShield/Animations/BS_Sword_Move')
assert u.EditorAssetLibrary.get_metadata_tag(a,'AuthoringOwner')=='SwordShieldPlayerV1'
editors=u.get_editor_subsystem(u.AssetEditorSubsystem)
assert editors.open_editor_for_assets([a])
assert u.EditorAssetLibrary.save_loaded_asset(a,False)
editors.close_all_editors_for_asset(a)
