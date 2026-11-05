"""Run through editor MCP after Batch6 native classes load. Does not save levels/GameModes.
Requires reviewed specs.json (including visually selected icon paths) next to this script.
"""
import json, uuid
from pathlib import Path
import unreal as u
ROOT=Path(u.Paths.project_dir()).resolve()
BASE='/Game/CompositionBatch6'
OUT=ROOT/'Saved/GAAuthoring/CompositionBatch6'; OUT.mkdir(parents=True,exist_ok=True)
RUN=OUT/uuid.uuid4().hex; RUN.mkdir()
SPEC=json.loads((ROOT/'Scripts/GA/Batch6/specs.json').read_text(encoding='utf-8'))
A=u.EditorAssetLibrary; T=u.AssetToolsHelpers.get_asset_tools()
def tag(s):
 t=u.GameplayTag();t.import_text('(TagName="'+s+'")');return t
def asset(path):
 a=u.load_asset(path);assert a,path;return a
def resolve(v):
 if isinstance(v,dict) and 'asset' in v:return asset(v['asset'])
 if isinstance(v,dict) and 'tag' in v:return tag(v['tag'])
 if isinstance(v,dict) and 'vector' in v:return u.Vector(*v['vector'])
 if isinstance(v,dict) and 'kind' in v:return getattr(u.AbilityPrototypeKind,v['kind'])
 return v
def save(a):
 if isinstance(a,u.Blueprint):u.BlueprintEditorLibrary.compile_blueprint(a)
 assert A.save_loaded_asset(a,False),a.get_path_name()
# Preflight before any mutation: all native classes and visual selections must exist.
for spec in SPEC['abilities']:
 assert u.load_class(None,spec['parent']),spec['parent']
 assert u.load_class(None,spec.get('prototype_class','/Script/test_25d.AbilityPrototype'))
 assert spec.get('icon_reviewed') is True,'Visually review exported spell textures first'
 asset(spec['icon'])
 for prefix in ['Abilities.Batch6.','Cooldown.Batch6.']:
  expected=prefix+spec['id']; assert expected in tag(expected).export_text(), 'Tag not registered: '+expected
# Resolve external references first; generated node references are resolved in recipe order.
for spec in SPEC.get('nodes',[])+SPEC['abilities']:
 assert u.load_class(None,spec.get('prototype_class','/Script/test_25d.AbilityPrototype'))
 for value in list(spec['prototype'].values())+list(spec.get('ability',{}).values()):
  if isinstance(value,dict) and 'asset' in value and not value['asset'].startswith(BASE+'/DA_'):
   asset(value['asset'])
# Verify catalog paths before creating dependent assets.
for path in SPEC['catalogs']:
 assert asset(path),path
baseline={path:[r.export_text() for r in asset(path).get_editor_property('ability_information')] for path in SPEC['catalogs']}
(RUN/'catalog-baseline.json').write_text(json.dumps(baseline,ensure_ascii=False,indent=2),encoding='utf-8')
classes={}
for spec in SPEC.get('nodes',[])+SPEC['abilities']:
 name=spec['id']; path=BASE+'/DA_'+name
 if A.does_asset_exist(path):
  config=asset(path)
  assert u.MathLibrary.class_is_child_of(config.get_class(),u.load_class(None,spec.get('prototype_class','/Script/test_25d.AbilityPrototype'))),path
 else:
  f=u.DataAssetFactory();f.set_editor_property('data_asset_class',u.load_class(None,spec.get('prototype_class','/Script/test_25d.AbilityPrototype')))
  config=T.create_asset('DA_'+name,BASE,u.DataAsset,f)
 for key,value in spec['prototype'].items():config.set_editor_property(key,resolve(value))
 config.set_editor_property('display_name',spec['name'])
 config.set_editor_property('cooldown_tag',tag('' if spec.get('node_only') else 'Cooldown.Batch6.'+name))
 assert config.is_valid_configuration(),path
 save(config)
 if spec.get('node_only'):continue
 path=BASE+'/GA_'+name
 if A.does_asset_exist(path):
  bp=asset(path)
  assert u.MathLibrary.class_is_child_of(A.load_blueprint_class(path),u.load_class(None,spec['parent'])),path
 else:
  bp=T.duplicate_asset('GA_'+name,BASE,asset('/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Projectile'))
  u.BlueprintEditorLibrary.reparent_blueprint(bp,u.load_class(None,spec['parent']))
 u.BlueprintEditorLibrary.compile_blueprint(bp)
 klass=A.load_blueprint_class(path);cdo=u.get_default_object(klass)
 cdo.set_editor_property('prototype',config)
 for key,value in spec.get('ability',{}).items():cdo.set_editor_property(key,resolve(value))
 tags=u.GameplayTagContainer();tags.import_text('(GameplayTags=((TagName="Abilities.Batch6.'+name+'")))')
 cdo.set_editor_property('ability_tags',tags);cdo.set_editor_property('startup_input_tag',tag(''))
 save(bp);classes[name]=klass
# Update only requested catalog rows; preserve every prior row and its ordering.
for path in SPEC['catalogs']:
 catalog=asset(path); rows=list(catalog.get_editor_property('ability_information'))
 for spec in SPEC['abilities']:
  identity=tag('Abilities.Batch6.'+spec['id'])
  matches=[r for r in rows if r.get_editor_property('ability_tag').export_text()==identity.export_text()]
  assert len(matches)<=1,path
  row=matches[0] if matches else u.MyAbilityInfo()
  row.import_text('(AbilityTag=(TagName="Abilities.Batch6.'+spec['id']+'"),CooldownTag=(TagName="Cooldown.Batch6.'+spec['id']+'"),AbilityType=(TagName="Abilities.Type.Offensive"),LevelRequirement=1,Ability="/Script/Engine.BlueprintGeneratedClass\''+classes[spec['id']].get_path_name()+'\'",Icon="/Script/Engine.Texture2D\''+asset(spec['icon']).get_path_name()+'\'")')
  if not matches:rows.append(row)
 catalog.set_editor_property('ability_information',rows);save(catalog)
 assert [r.export_text() for r in rows if 'Abilities.Batch6.' not in r.get_editor_property('ability_tag').export_text()]==[r for r in baseline[path] if 'Abilities.Batch6.' not in r], 'Existing catalog entries changed: '+path
qa_path=BASE+'/QA/DA_Batch6QAGoblin'
qa=asset(qa_path) if A.does_asset_exist(qa_path) else T.duplicate_asset('DA_Batch6QAGoblin',BASE+'/QA',asset('/Game/PureMass/DA_MassQAGoblin'))
for key,value in dict(health=10000.,speed=20.,attack_damage=0.,attack_range=1.,armor=0.,block_chance=0.,xp_reward=0).items():qa.set_editor_property(key,value)
save(qa)
result={'run_id':RUN.name,'classes':{k:v.get_path_name() for k,v in classes.items()},'catalogs':SPEC['catalogs']}
(RUN/'authored.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
(OUT/'authored.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
u.log('BATCH6_ASSETS_AUTHORED')
