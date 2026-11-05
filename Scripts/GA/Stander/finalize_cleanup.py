import unreal as u,json
from pathlib import Path
assert not u.EditorLevelLibrary.get_game_world()
base='/Game/BluePrints/Characters/Stander/Animations/'
for name in ['AM_Stander_Counter','AS_Stander_Counter']:
 path=base+name
 if not u.EditorAssetLibrary.does_asset_exist(path):continue
 a=u.load_asset(path);assert u.EditorAssetLibrary.get_metadata_tag(a,'AuthoringOwner')=='StanderShowcaseV1'
 refs=u.EditorAssetLibrary.find_package_referencers_for_asset(path,True);assert not refs,str(refs)
 assert u.EditorAssetLibrary.delete_asset(path)
r={'map':u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name(),'pie':bool(u.EditorLevelLibrary.get_game_world()),'dirty_maps':[x.get_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()],'dirty_content':[x.get_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}
r['passed']=not r['pie'] and not r['dirty_maps'] and not r['dirty_content'] and 'LoadMenu' in r['map']
(Path(u.Paths.project_dir())/'Saved/GAAuthoring/Stander/restored-editor.json').write_text(json.dumps(r,indent=2));assert r['passed'],r
