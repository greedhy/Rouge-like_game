import unreal as u
pairs={'NS_StarFlameHalo':'/Game/Art/CompositionEffects/NS_FireSeed_Field','NS_SolarFireballFlame':'/Game/Assets/Effects/Projectiles/FireBolt/NS_Fireball','NS_SolarExplosionFlame':'/Game/Assets/Effects/Projectiles/FireExplosion/NS_FireExplosion','NS_GlacialStorm':'/Game/Art/TerrainSurfaces/Effects/NS_Frost_Surface','NS_EarthShockwave':'/Game/Art/CompositionEffects/NS_Aftershock_Ring'}
for name,src in pairs.items():
 path='/Game/CompositionBatch6/'+name
 a=u.load_asset(path)
 if not a:a=u.AssetToolsHelpers.get_asset_tools().duplicate_asset(name,'/Game/CompositionBatch6',u.load_asset(src))
 assert a,path
 assert u.EditorAssetLibrary.save_loaded_asset(a,False)
u.log('BATCH6_VFX_COPIED')
