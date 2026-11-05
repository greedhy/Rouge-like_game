import unreal as u,json
from pathlib import Path
f=Path(u.Paths.project_dir())/'Saved/GAAuthoring/StanderMass';R='/Game/BluePrints/Characters/Stander/'
mode=u.get_default_object(u.EditorAssetLibrary.load_blueprint_class(R+'BP_StanderGameMode'));catalog=mode.get_editor_property('ability_info');hud=u.get_default_object(mode.get_editor_property('hud_class'));cfg=u.load_asset(R+'DA_Stander')
r={'checks':{},'skills':[],'input':{}}
for field in ['overlay_widget_controller_class','spell_menu_widget_controller_class']:
 cls=hud.get_editor_property(field);r['checks'][field]=u.get_default_object(cls).get_editor_property('ability_info')==catalog
rows=catalog.get_editor_property('ability_information')
for name in ['Combo','Guard','Spin','Dash','Lift','ShieldBash']:
 tag='Abilities.Stander.'+name;matches=[x for x in rows if tag in x.get_editor_property('ability_tag').export_text()];assert len(matches)==1
 row=matches[0];cdo=u.get_default_object(row.get_editor_property('ability'));icon=row.get_editor_property('icon');assert icon
 r['skills'].append({'name':name,'icon':icon.get_path_name(),'class':row.get_editor_property('ability').get_path_name(),'input':cdo.get_editor_property('startup_input_tag').export_text()})
for field in ['lift','shield_bash']:
 s=cfg.get_editor_property(field);r[field]={k:str(s.get_editor_property(k)) for k in ['montage','contact','damage','radius','angle','play_rate']}
imc=u.load_asset('/Game/BluePrints/Input/IMC_PlayerContext');r['input']['mappings']=imc.get_editor_property('default_key_mappings').export_text()
inputconfig=u.load_asset('/Game/BluePrints/Input/DA_MyInputConfig');r['input']['config']=[x.export_text() for x in inputconfig.get_editor_property('ability_input_actions')]
r['checks']['no_dirty_content']=not u.EditorLoadingAndSavingUtils.get_dirty_content_packages();r['checks']['no_dirty_maps']=not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
(f/'readback.json').write_text(json.dumps(r,indent=2));assert all(r['checks'].values());print('StanderMass readback passed')
