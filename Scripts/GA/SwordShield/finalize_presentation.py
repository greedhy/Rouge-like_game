import unreal as u
assert not u.EditorLevelLibrary.get_game_world()
root='/Game/BluePrints/Characters/SwordShield/'
bp=u.load_asset(root+'BP_SwordShieldPlayer');assert u.EditorAssetLibrary.get_metadata_tag(bp,'AuthoringOwner')=='SwordShieldPlayerV1'
cdo=u.get_default_object(bp.generated_class())
cdo.get_editor_property('sword_visual').set_editor_property('relative_rotation',u.Rotator(roll=-90.))
u.BlueprintEditorLibrary.compile_blueprint(bp);assert u.EditorAssetLibrary.save_loaded_asset(bp,False)
catalog=u.load_asset(root+'DA_SwordAbilityInfo');rows=list(catalog.get_editor_property('ability_information'))
paths=['/Game/KiroshoSkIconsPk0/Textures/T_DeepCut.T_DeepCut','/Game/Skill_Icons/Textures/512x512/T_Skill_Icons__23.T_Skill_Icons__23','/Game/KiroshoSkIconsPk0/Textures/T_StormAttack.T_StormAttack']
for row,path in zip(rows,paths):
 # Import only the changed field; keep each identity/input/cooldown.
 row.import_text('(Icon="'+path+'")')
catalog.set_editor_property('ability_information',rows);assert u.EditorAssetLibrary.save_loaded_asset(catalog,False)
cfg=u.load_asset(root+'DA_SwordShield');combo=list(cfg.get_editor_property('combo'))
combo[0].set_editor_property('contact',.15);cfg.set_editor_property('combo',combo)
assert u.EditorAssetLibrary.save_loaded_asset(cfg,False)
