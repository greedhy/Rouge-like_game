"""Independent saved asset readback, bound to the fresh authoring run."""
import json
from pathlib import Path
import unreal as u
root=Path(u.Paths.project_dir()).resolve();out=root/'Saved/GAAuthoring/CompositionBatch6'
spec=json.loads((root/'Scripts/GA/Batch6/specs.json').read_text(encoding='utf-8'))
authored=json.loads((out/'authored.json').read_text(encoding='utf-8'));run=out/authored['run_id']
baseline=json.loads((run/'catalog-baseline.json').read_text(encoding='utf-8'))
report={'run_id':run.name,'passed':False,'abilities':{},'catalogs':{}}
def check(obj,key,want):
 got=obj.get_editor_property(key)
 if isinstance(want,dict):
  if 'vector' in want:assert all(abs(a-b)<.001 for a,b in zip([got.x,got.y,got.z],want['vector'])),key
  elif 'asset' in want:assert got and got.get_path_name().split('.')[0]==want['asset'],(key,str(got),want)
  elif 'kind' in want:assert got==getattr(u.AbilityPrototypeKind,want['kind']),(key,str(got),want)
  elif 'tag' in want:assert want['tag'] in got.export_text(),(key,str(got),want)
 elif isinstance(want,(int,float)) and not isinstance(want,bool):assert abs(got-want)<.001,(key,got,want)
 elif isinstance(want,bool):assert got==want,(key,got,want)
 else:assert str(got)==str(want),(key,str(got),want)
for s in spec.get('nodes',[])+spec['abilities']:
 config=u.load_asset('/Game/CompositionBatch6/DA_'+s['id']);assert config and config.is_valid_configuration()
 for k,v in s['prototype'].items():check(config,k,v)
 assert str(config.get_editor_property('display_name'))==s['name']
for s in spec['abilities']:
 path='/Game/CompositionBatch6/GA_'+s['id'];cls=u.EditorAssetLibrary.load_blueprint_class(path);assert cls
 assert u.MathLibrary.class_is_child_of(cls,u.load_class(None,s['parent'])),path
 assert cls.get_path_name()==path+'.GA_'+s['id']+'_C',path
 cdo=u.get_default_object(cls);check(cdo,'prototype',{'asset':'/Game/CompositionBatch6/DA_'+s['id']})
 for k,v in s.get('ability',{}).items():check(cdo,k,v)
 assert 'Abilities.Batch6.'+s['id'] in cdo.get_editor_property('ability_tags').export_text()
 assert cdo.get_editor_property('damage_effect_class'),path
 report['abilities'][s['id']]=cls.get_path_name()
for path in spec['catalogs']:
 rows=u.load_asset(path).get_editor_property('ability_information')
 old=[r for r in baseline[path] if 'Abilities.Batch6.' not in r]
 actual=[r.export_text() for r in rows if 'Abilities.Batch6.' not in r.get_editor_property('ability_tag').export_text()]
 assert old==actual,'Non-Batch6 rows changed: '+path
 for s in spec['abilities']:
  found=[r for r in rows if 'Abilities.Batch6.'+s['id'] in r.get_editor_property('ability_tag').export_text()];assert len(found)==1
  row=found[0];check(row,'icon',{'asset':s['icon']})
  assert row.get_editor_property('ability').get_path_name()==report['abilities'][s['id']]
  assert 'Cooldown.Batch6.'+s['id'] in row.get_editor_property('cooldown_tag').export_text()
  assert 'Abilities.Type.Offensive' in row.get_editor_property('ability_type').export_text()
 report['catalogs'][path]=len(rows)
qa=u.load_asset('/Game/CompositionBatch6/QA/DA_Batch6QAGoblin');assert qa
for key,value in dict(health=10000.,speed=20.,attack_damage=0.,attack_range=1.,armor=0.,block_chance=0.,xp_reward=0).items():check(qa,key,value)
report['passed']=True
for dest in [run/'verified-assets.json',out/'verified-assets.json']:dest.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
u.log('BATCH6_ASSETS_VERIFIED')
