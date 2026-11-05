import unreal as u,json
from pathlib import Path
f=Path(u.Paths.project_dir())/'Saved/GAAuthoring/StanderMass';f.mkdir(parents=True,exist_ok=True)
r={'project':u.Paths.project_dir(),'pie':bool(u.EditorLevelLibrary.get_game_world()),'dirty':[x.get_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()],'maps':[x.get_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()],'types':[hasattr(u,n) for n in ['StanderLiftAbility','StanderShieldBashAbility']]}
(f/'state.json').write_text(json.dumps(r,indent=2))
if r['pie']:u.get_editor_subsystem(u.LevelEditorSubsystem).editor_request_end_play()
