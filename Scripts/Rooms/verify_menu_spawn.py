import unreal as u,json
from pathlib import Path
mode=u.EditorAssetLibrary.load_blueprint_class('/Game/BluePrints/Game/BP_LoadScreenGameMode')
d=u.get_default_object(mode)
w=d.get_editor_property('default_map')
tag=str(d.get_editor_property('default_player_start_tag'))
assert u.get_editor_subsystem(u.LevelEditorSubsystem).load_level(w.get_path_name().split('.')[0])
starts=[a for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors() if isinstance(a,u.PlayerStart)]
entries=[{'name':s.get_name(),'tag':str(s.get_editor_property('player_start_tag')),'slot':s.get_editor_property('player_slot') if isinstance(s,u.TravelPlayerStart) else 0} for s in starts]
matched=[s for s in entries if s['tag']==tag and s['slot']==0]
r={'menu_tag':tag,'target':w.get_path_name(),'starts':entries,'passed':len(matched)==1}
Path(u.Paths.project_saved_dir()+'MainMenuDefault/spawn_regression.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
assert r['passed'],json.dumps(r)
