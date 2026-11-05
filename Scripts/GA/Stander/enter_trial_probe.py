"""Exercise the same RoomSubsystem entry as the menu, with a dedicated test save name."""
import unreal as u
w=u.EditorLevelLibrary.get_game_world();assert w and w.get_path_name().startswith('/Temp/')
u.SystemLibrary.execute_console_command(w,'test25d.Stander.EnterTrial',u.GameplayStatics.get_player_controller(w,0))

