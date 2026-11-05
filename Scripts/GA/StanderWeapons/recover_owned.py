import unreal as u
p='/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/DA_WeaponSet';a=u.load_asset(p);assert a
u.EditorAssetLibrary.set_metadata_tag(a,'AuthoringOwner','StanderWeaponsV1');assert u.EditorAssetLibrary.save_loaded_asset(a,False)
