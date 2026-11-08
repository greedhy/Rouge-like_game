def main():
    import unreal as u
    import json,uuid
    from pathlib import Path
    root=Path(u.Paths.project_dir()).resolve()
    widget_path='/Game/BluePrints/UI/Rooms/WBP_ModeAndRoom'
    widget_class=u.EditorAssetLibrary.load_blueprint_class(widget_path)
    assert widget_class and isinstance(u.get_default_object(widget_class),u.RoomMenuWidget)
    assert u.get_default_object(widget_class).get_editor_property('is_focusable')
    hud=u.get_default_object(u.EditorAssetLibrary.load_blueprint_class('/Game/BluePrints/UI/HUD/BP_LoadScreenHud'))
    assert hud.get_editor_property('room_menu_widget_class')==widget_class
    gi=u.get_default_object(u.EditorAssetLibrary.load_blueprint_class('/Game/BluePrints/Game/BP_MyGameInstance'))
    lobby=gi.get_editor_property('room_lobby_map')
    assert lobby and lobby.get_path_name()=='/Game/levels/RoomLobby.RoomLobby'
    assert lobby.get_world_settings().get_editor_property('default_game_mode')==u.RoomLobbyGameMode.static_class()
    gm=u.get_default_object(u.EditorAssetLibrary.load_blueprint_class('/Game/BluePrints/Game/BP_LoadScreenGameMode'))
    assert gm.get_editor_property('default_map').get_path_name()=='/Game/RoguelikeGeneration/Maps/L_RoguelikeRun.L_RoguelikeRun'
    assert str(gm.get_editor_property('default_player_start_tag'))=='RG_Entrance'
    dirty=list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    assert not dirty,[p.get_name() for p in dirty]
    world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    result={'run_id':uuid.uuid4().hex,'widget':widget_path,'lobby':lobby.get_path_name(),'default_map':'/Game/RoguelikeGeneration/Maps/L_RoguelikeRun','editor_map':world.get_path_name(),'dirty_packages':[],'passed':True}
    (root/'Saved/Rooms/asset_validation.json').write_text(json.dumps(result,indent=2))
    u.log('ROOM_ASSETS_VALIDATED '+json.dumps(result))
main()
