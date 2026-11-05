import unreal as u,json,shutil,uuid
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve();out=root/'Saved/GAAuthoring/MassGoblinDefaults'/uuid.uuid4().hex;out.mkdir(parents=True)
assert not u.EditorLevelLibrary.get_game_world()
source=u.load_asset('/Game/PureMass/DA_MassGoblin');assert source.weapon_mesh
rows=[]
for path in ['/Game/Performance/DA_BenchmarkGoblin','/Game/PureMass/DA_MassQAGoblin','/Game/CompositionBatch6/QA/DA_Batch6QAGoblin']:
 obj=u.load_asset(path);assert obj and obj.mesh==source.mesh
 shutil.copy2(root/'Content'/(path.removeprefix('/Game/')+'.uasset'),out/(obj.get_name()+'.uasset'))
 before={'health':obj.health,'attack_damage':obj.attack_damage,'attack_period':obj.attack_period}
 for field in ['weapon_mesh','weapon_socket','weapon_offset','attack_contact_time','attack_clip_duration']:
  obj.set_editor_property(field,source.get_editor_property(field))
 assert u.EditorAssetLibrary.save_loaded_asset(obj,False)
 assert before=={'health':obj.health,'attack_damage':obj.attack_damage,'attack_period':obj.attack_period}
 rows.append({'path':path,'weapon':obj.weapon_mesh.get_path_name(),'unchanged_gameplay':before})
(out/'result.json').write_text(json.dumps(rows,indent=2));(out.parent/'latest.txt').write_text(str(out))
