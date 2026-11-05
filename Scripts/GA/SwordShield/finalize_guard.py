import unreal as u
assert not u.EditorLevelLibrary.get_game_world()
m=u.load_asset('/Game/BluePrints/Characters/SwordShield/Animations/AM_Sword_Guard')
assert u.EditorAssetLibrary.get_metadata_tag(m,'AuthoringOwner')=='SwordShieldPlayerV1'
m.set_editor_property('blend_out_trigger_time',0.)
assert u.EditorAssetLibrary.save_loaded_asset(m,False)
