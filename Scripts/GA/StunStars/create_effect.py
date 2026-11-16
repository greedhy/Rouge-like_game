"""Create an isolated local-space copy; shared source remains untouched."""
import unreal as u
assert not u.EditorLevelLibrary.get_game_world()
path = "/Game/BluePrints/Characters/Effects/NS_Stars_EnemyStun"
asset = u.load_asset(path) if u.EditorAssetLibrary.does_asset_exist(path) else None
if asset:
    assert u.EditorAssetLibrary.get_metadata_tag(asset, "AuthoringOwner") == "StunStars", "Preserve unrelated existing effect"
else:
    asset = u.EditorAssetLibrary.duplicate_asset("/Game/Assets/Effects/Stun/NS_Stars", path)
assert asset
u.EditorAssetLibrary.set_metadata_tag(asset, "AuthoringOwner", "StunStars")
assert u.EditorAssetLibrary.save_loaded_asset(asset, False)
u.log("STUN_STARS_COPY_CREATED")
