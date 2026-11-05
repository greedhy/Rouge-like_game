import unreal as u,json,uuid,shutil
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve();out=root/'Saved/GAAuthoring/DungeonHybridFix'/uuid.uuid4().hex;out.mkdir();r={}
fx=u.load_asset('/Game/PureMass/Effects/NS_DungeonElectricBeam');assert fx
for n in ['Beam','Chain']:
 path='/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/DA_Prototype_'+n
 a=u.load_asset(path);r[n]={'before':str(a.get_editor_property('loop_vfx'))}
 shutil.copy2(root/(path.replace('/Game/','Content/')+'.uasset'),out/(n+'.uasset'))
 a.set_editor_property('loop_vfx',fx);assert u.EditorAssetLibrary.save_loaded_asset(a,False)
 assert a.get_editor_property('loop_vfx')==fx;r[n]['after']=fx.get_path_name()
(out/'references.json').write_text(json.dumps(r,indent=2));u.log(str(out))
