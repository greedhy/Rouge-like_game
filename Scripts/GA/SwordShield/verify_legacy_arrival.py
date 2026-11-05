"""PIE regression for new-save Dungeon1PlayerStart1 -> sword trial travel."""
import unreal as u
w=u.EditorLevelLibrary.get_game_world();assert w
u.SystemLibrary.execute_console_command(w,'test25d.SwordShield.EnterTrial',u.GameplayStatics.get_player_controller(w,0))
