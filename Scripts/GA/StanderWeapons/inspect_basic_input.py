import unreal as u,json
from pathlib import Path
r={'pie':bool(u.EditorLevelLibrary.get_game_world()),'dirty':[x.get_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()],'dirty_maps':[x.get_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()],'characters':[]}
for a in u.AssetRegistryHelpers.get_asset_registry().get_assets_by_path('/Game/BluePrints',True):
 if str(a.asset_name) not in ['BP_StanderPlayer','BP_Player','BP_PlayerCharacter','BP_MyPlayerController','BP_PureMassPlayerController']:continue
 obj=a.get_asset();cls=obj.generated_class() if isinstance(obj,u.Blueprint) else None
 if not cls:continue
 c=u.get_default_object(cls);item={'asset':str(a.package_name)}
 if isinstance(c,u.Character):
  item['components']=[{'name':x.get_name(),'overlaps':x.get_editor_property('generate_overlap_events'),'profile':str(x.get_collision_profile_name())} for x in c.get_components_by_class(u.PrimitiveComponent)]
 for field in ['input_config','mass_input_config','player_context','move_action','mass_move_action']:
  try:item[field]=str(c.get_editor_property(field))
  except:pass
 r['characters'].append(item)
for path in ['/Game/BluePrints/Input/DA_MyInputConfig']:
 r[path]=[x.export_text() for x in u.load_asset(path).get_editor_property('ability_input_actions')]
(Path(u.Paths.project_dir())/'Saved/GAAuthoring/StanderWeapons/basic-input-before.json').write_text(json.dumps(r,indent=2))

