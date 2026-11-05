import unreal as u
from pathlib import Path
x=u.load_asset('/Game/BluePrints/UI/Data/DT_RichTextStyle')
s=u.DataTableFunctionLibrary.export_data_table_to_json_string(x)
(Path(u.Paths.project_dir())/'Saved/GAAuthoring/CompositionBatch6/menu-style-before.json').write_text(s,encoding='utf-8')
u.log('DEFAULT_FONT '+u.get_default_object(u.TextBlock.static_class()).get_editor_property('font').export_text())
