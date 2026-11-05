import json
from pathlib import Path
import unreal as u

out=Path(u.Paths.project_dir()).resolve()/'Saved/GAAuthoring/SkillIconBackground'
out.mkdir(parents=True,exist_ok=True)
report={'widgets':[], 'catalogs':[], 'textures':{}}
paths=['/Game/BluePrints/UI/SpellGlobe/WBP_SpellGlobe','/Game/BluePrints/UI/SpellGlobe/WBP_PictureFrame','/Game/BluePrints/UI/SpellMenu/WBP_My_SpellGLobeButton']
for path in paths:
 bp=u.load_asset(path)
 for w in u.ObjectIterator(u.Widget):
  if w.get_path_name().startswith(bp.get_path_name()+':'):
   d={'path':w.get_path_name(),'class':w.get_class().get_name(),'parent':str(w.get_parent())}
   if isinstance(w,u.Image): d['brush']=w.get_editor_property('brush').export_text()
   if w.slot: d['slot']=str(w.slot)
   report['widgets'].append(d)
textures=set()
for a in u.AssetRegistryHelpers.get_asset_registry().get_assets_by_class(u.TopLevelAssetPath('/Script/test_25d','AbilityInfo'),True):
 obj=a.get_asset(); rows=[]
 for r in obj.get_editor_property('ability_information'):
  icon=r.get_editor_property('icon'); bg=r.get_editor_property('background_material')
  rows.append({'tag':r.get_editor_property('ability_tag').export_text(),'icon':icon.get_path_name() if icon else None,'background':bg.get_path_name() if bg else None})
  if icon: textures.add(icon.get_path_name())
 report['catalogs'].append({'path':obj.get_path_name(),'rows':rows})
for path in sorted(textures):
 tex=u.load_asset(path); props={}
 for key in ['compression_settings','compression_no_alpha','srgb','lod_group','asset_import_data']:
  props[key]=str(tex.get_editor_property(key))
 task=u.AssetExportTask();task.object=tex;task.filename=str(out/(tex.get_name()+'.tga'));task.automated=True;task.prompt=False;task.replace_identical=True
 props['exported']=u.Exporter.run_asset_export_task(task)
 report['textures'][path]=props
(out/'inspection.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('SKILL_ICON_INSPECTED')
