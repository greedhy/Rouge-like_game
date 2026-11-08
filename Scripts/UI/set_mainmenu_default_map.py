"""Set the load-menu destination; run with Unreal's PythonScript commandlet."""
import json
from pathlib import Path
import unreal as u

asset = '/Game/BluePrints/Game/BP_LoadScreenGameMode'
target = '/Game/RoguelikeGeneration/Maps/L_RoguelikeRun'
world = u.load_asset(target)
assert world, target
mode = u.EditorAssetLibrary.load_blueprint_class(asset)
cdo = u.get_default_object(mode)
old_map = cdo.get_editor_property('default_map')
old_name = str(cdo.get_editor_property('default_map_name'))
# Retain existing destinations for old saves and map-name lookup.
maps = cdo.get_editor_property('maps')
if old_map and old_name:
    maps[old_name] = old_map
maps['L_RoguelikeRun'] = world
cdo.set_editor_property('maps', maps)
cdo.set_editor_property('default_map', world)
cdo.set_editor_property('default_map_name', 'L_RoguelikeRun')
# Standalone arrival uses the menu-seeded GameInstance tag before the map default.
cdo.set_editor_property('default_player_start_tag', u.Name('RG_Entrance'))
assert u.EditorAssetLibrary.save_asset(asset, only_if_is_dirty=False)
assert cdo.get_editor_property('default_map').get_path_name() == target + '.L_RoguelikeRun'
report = {'asset': asset, 'old_map': old_map.get_path_name() if old_map else None,
          'new_map': cdo.get_editor_property('default_map').get_path_name(),
          'name': str(cdo.get_editor_property('default_map_name')), 'saved': True}
out = Path(u.Paths.project_saved_dir()) / 'MainMenuDefault'
out.mkdir(parents=True, exist_ok=True)
(out / 'changed.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
