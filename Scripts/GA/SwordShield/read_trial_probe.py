import unreal as u,json
from pathlib import Path
w=u.EditorLevelLibrary.get_game_world();p=u.GameplayStatics.get_player_pawn(w,0);pc=u.GameplayStatics.get_player_controller(w,0)
report={'world':w.get_path_name(),'pawn':p.get_class().get_path_name() if p else None,'mode':u.GameplayStatics.get_game_mode(w).get_class().get_path_name(),'hud':str(pc.get_hud()) if pc else None}
report['enemies']=len(u.GameplayStatics.get_all_actors_of_class(w,u.EnemyCharacter))
report['directors']=len(u.GameplayStatics.get_all_actors_of_class(w,u.DungeonRunDirector))
report['passed']=isinstance(p,u.SwordShieldPlayer) and 'L_RoguelikeRun' in report['world'] and report['directors']==1
report['widgets']=[{'class':a.get_class().get_name(),'visible':a.is_visible(),'viewport':a.is_in_viewport()} for a in u.ObjectIterator(u.UserWidget) if 'UEDPIE_' in a.get_path_name()]
(Path(u.Paths.project_dir())/'Saved/GAAuthoring/SwordShield/room-runtime.json').write_text(json.dumps(report,indent=2))
u.SystemLibrary.execute_console_command(w,'HighResShot 1280x720',pc)
