import unreal as u,json,uuid,shutil
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve();out=root/'Saved/GAAuthoring/DungeonHybridFix'/uuid.uuid4().hex;out.mkdir()
src=u.load_asset('/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo');dst=u.load_asset('/Game/PureMass/DA_MassAbilityInfo')
rows=list(dst.get_editor_property('ability_information'))
source=[r for r in src.get_editor_property('ability_information') if r.get_editor_property('ability') and '/Fire/FireBolt/GA_FireBolt.' in r.get_editor_property('ability').get_path_name()]
assert len(source)==1
row=source[0];tag=row.get_editor_property('ability_tag').export_text()
assert row.get_editor_property('icon')
before=[r.export_text() for r in rows]
if not any(r.get_editor_property('ability_tag').export_text()==tag for r in rows):
 shutil.copy2(root/'Content/PureMass/DA_MassAbilityInfo.uasset',out/'DA_MassAbilityInfo.uasset')
 copy=u.MyAbilityInfo();copy.import_text(row.export_text());rows.append(copy)
 dst.set_editor_property('ability_information',rows);assert u.EditorAssetLibrary.save_loaded_asset(dst,False)
after=[r.export_text() for r in dst.get_editor_property('ability_information')]
assert all(r in after for r in before)
assert row.export_text() in after
(out/'catalog-verification.json').write_text(json.dumps({'success':True,'added_tag':tag,'before_count':len(before),'after_count':len(after)},indent=2))
u.log('DUNGEON_SKILL_ICON_VERIFIED '+str(out))
