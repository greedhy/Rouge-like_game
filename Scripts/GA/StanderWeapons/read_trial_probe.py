import unreal as u,json
from pathlib import Path
w=u.EditorLevelLibrary.get_game_world();assert w
p=u.GameplayStatics.get_player_pawn(w,0)
ids=[a.get_component_by_class(u.WorldPickupComponent).get_editor_property('item_definition_id').export_text() for a in u.GameplayStatics.get_all_actors_of_class(w,u.EquipmentSoulPickup)]
manifest=json.loads((Path(u.Paths.project_dir())/'Scripts/GA/StanderWeapons/weapon-manifest.json').read_text())
expected=['BackpackItem:'+f['item_id'] for f in manifest['families']]
report={'world':w.get_path_name(),'pawn':p.get_class().get_path_name() if p else None,'pickups':ids,'passed':bool(p and isinstance(p,u.StanderPlayer) and all(any(e in i for i in ids) for e in expected))}
(Path(u.Paths.project_dir())/'Saved/GAAuthoring/StanderWeapons/trial-entrance.json').write_text(json.dumps(report,indent=2))
assert report['passed'],report
