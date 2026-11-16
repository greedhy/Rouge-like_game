"""Read Niagara rapid iteration constants without changing the system."""
import json
from pathlib import Path
import unreal as u

out = Path(u.Paths.project_dir()) / "Saved/GAAuthoring/StunStars/20261116"
report = {}
for name in ["SpawnScript", "UpdateScript"]:
    script = u.load_object(None, "/Game/Assets/Effects/Stun/NS_Stars.NS_Stars:Fountain." + name)
    try:
        report[name] = script.get_editor_property("RapidIterationParameters").export_text()
    except Exception as error:
        report[name] = str(error)
(out / "rapid-parameters.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
world = u.EditorLevelLibrary.get_game_world()
if world:
    assert world.get_path_name().startswith("/Temp/")
    assert any(a.actor_has_tag("StunStarsPreview") for a in u.GameplayStatics.get_all_actors_of_class(world, u.EnemyCharacter))
    u.get_editor_subsystem(u.LevelEditorSubsystem).editor_request_end_play()
