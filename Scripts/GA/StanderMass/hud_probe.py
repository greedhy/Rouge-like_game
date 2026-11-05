import unreal as u,json
from pathlib import Path
r={};w=u.EditorLevelLibrary.get_game_world();pc=u.GameplayStatics.get_player_controller(w,0);hud=pc.get_hud();r['hud_class']=hud.get_class().get_path_name()
for key in ['overlay_widget_controller','spell_menu_widget_controller','overlay_widget']:
 try:
  obj=hud.get_editor_property(key);r[key]=str(obj)
  if obj and 'controller' in key:r[key+'_catalog']=str(obj.get_editor_property('ability_info'))
 except Exception as e:r[key]=str(e)
imc=u.load_asset('/Game/BluePrints/Input/IMC_PlayerContext');r['input_mapping']=imc.get_editor_property('default_key_mappings').export_text()
ic=u.load_asset('/Game/BluePrints/Input/DA_MyInputConfig');r['input_actions']=[x.export_text() for x in ic.get_editor_property('ability_input_actions')]
r['widgets']=[]
for x in u.ObjectIterator(u.UserWidget):
 if x.get_world()==w:r['widgets'].append({'class':x.get_class().get_path_name(),'visible':x.is_visible(),'name':x.get_name()})
(Path(u.Paths.project_dir())/'Saved/GAAuthoring/StanderMass/hud-runtime.json').write_text(json.dumps(r,indent=2))
