import unreal as u
assert not u.EditorLevelLibrary.get_game_world(),'PIE must be stopped'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(),'Unsaved content'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(),'Unsaved map'
u.SystemLibrary.execute_console_command(u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world(),'QUIT_EDITOR')
