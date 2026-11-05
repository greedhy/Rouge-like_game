"""Repair only SpellMenu description templates using an isolated readable style table."""
import json
from pathlib import Path
import unreal as u
root=Path(u.Paths.project_dir()).resolve();out=root/'Saved/GAAuthoring/CompositionBatch6'
source=u.load_asset('/Game/BluePrints/UI/Data/DT_RichTextStyle');assert source
path='/Game/CompositionBatch6/DT_SpellDescriptionStyle'
table=u.load_asset(path) if u.EditorAssetLibrary.does_asset_exist(path) else u.AssetToolsHelpers.get_asset_tools().duplicate_asset('DT_SpellDescriptionStyle','/Game/CompositionBatch6',source)
before=u.DataTableFunctionLibrary.export_data_table_to_json_string(source);rows=json.loads(before)
for row in rows:
 font=row['TextStyle']['Font'];font.update(FontObject="/Script/Engine.Font'/Engine/EngineFonts/Roboto.Roboto'",TypefaceFontName='Bold',Size=22 if row['Name']=='Title' else 18,LetterSpacing=0)
 font['OutlineSettings']['OutlineSize']=0
assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table,json.dumps(rows))
assert u.EditorAssetLibrary.save_loaded_asset(table,False)
bp=u.load_asset('/Game/BluePrints/UI/SpellMenu/WBP_SpellMune');assert bp
changes=[]
for obj in u.ObjectIterator(u.RichTextBlock):
 if obj.get_path_name().startswith(bp.get_path_name()+':') and 'Description' in obj.get_name():
  changes.append({'widget':obj.get_name(),'previous_wrap':obj.get_editor_property('wrap_text_at'),'previous_table':obj.get_editor_property('text_style_set').get_path_name()})
  obj.set_editor_property('wrap_text_at',0.);obj.set_editor_property('auto_wrap_text',True);obj.set_editor_property('text_style_set',table)
assert len(changes)==2,changes
u.BlueprintEditorLibrary.compile_blueprint(bp);assert u.EditorAssetLibrary.save_loaded_asset(bp,False)
# Verify templates and style rows independently, preserve source table exactly.
assert u.DataTableFunctionLibrary.export_data_table_to_json_string(source)==before
checked=[]
for obj in u.ObjectIterator(u.RichTextBlock):
 if obj.get_path_name().startswith(bp.get_path_name()+':') and 'Description' in obj.get_name():
  assert obj.get_editor_property('wrap_text_at')==0 and obj.get_editor_property('auto_wrap_text')
  assert obj.get_editor_property('text_style_set')==table;checked.append(obj.get_name())
for row in json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table)):
 assert row['TextStyle']['Font']['Size']==(22 if row['Name']=='Title' else 18)
 assert 'Roboto.Roboto' in row['TextStyle']['Font']['FontObject']
(out/'menu-description-repair.json').write_text(json.dumps({'passed':True,'changed':changes,'verified':checked,'style':path},indent=2),encoding='utf-8')
u.log('BATCH6_MENU_DESCRIPTION_REPAIRED')
