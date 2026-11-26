"""Create separate learnable compositions. Existing tuning is preserved on rerun."""
import unreal as u, json, uuid, shutil
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve()
RUN=ROOT/'Saved/GAAuthoring/Compositions'/uuid.uuid4().hex
RUN.mkdir(parents=True)
BASE='/Game/BluePrints/AbilitySystem/Player/Ability/Compositions'
SOURCE='/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes'
CATALOG='/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo'
TOOLS=u.AssetToolsHelpers.get_asset_tools()
def tag(name):
    t=u.GameplayTag();t.import_text('(TagName="'+name+'")');return t
def save(a):
    if isinstance(a,u.Blueprint):u.BlueprintEditorLibrary.compile_blueprint(a)
    assert u.EditorAssetLibrary.save_loaded_asset(a,False),a.get_path_name()
def backup(path):
    src=ROOT/'Content'/(path.removeprefix('/Game/')+'.uasset')
    if src.exists():
        dest=RUN/'backup'/src.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dest)
def node(name,source,**values):
    path=BASE+'/DA_'+name
    if u.EditorAssetLibrary.does_asset_exist(path):
        a=u.load_asset(path)
    else:
        a=TOOLS.duplicate_asset('DA_'+name,BASE,u.load_asset(SOURCE+'/DA_Prototype_'+source))
        for k,v in values.items():a.set_editor_property(k,v)
        save(a)
    assert a.is_valid_configuration(),path
    return a
def ga(name,config,source):
    path=BASE+'/GA_'+name
    if u.EditorAssetLibrary.does_asset_exist(path):bp=u.load_asset(path)
    else:bp=TOOLS.duplicate_asset('GA_'+name,BASE,u.load_asset(SOURCE+'/GA_Prototype_'+source))
    backup(path)
    cls=u.EditorAssetLibrary.load_blueprint_class(path);cdo=u.get_default_object(cls)
    cdo.set_editor_property('prototype',config)
    tags=u.GameplayTagContainer();tags.import_text('(GameplayTags=((TagName="Abilities.Composition.'+name+'")))')
    cdo.set_editor_property('ability_tags',tags);cdo.set_editor_property('startup_input_tag',tag(''))
    save(bp);return cls
report={'run_id':RUN.name,'success':False}
try:
    firefield=node('FireSeed_Field','Field',display_name='Fire Patch',duration=4.0,radius=180.0,damage=8.0,damage_per_level=2.0,damage_type=tag('Damage.Fire'),impact_vfx=u.load_asset('/Game/Assets/Effects/Projectiles/FireExplosion/NS_FireExplosion'))
    frost=node('FrostFan_Ground','Surface',duration=4.0,radius=150.0)
    vines=node('BrambleMine_Ground','Surface',display_name='Vines',duration=4.0,radius=220.0,surface_class=u.EditorAssetLibrary.load_blueprint_class('/Game/BluePrints/Actor/Terrain/BP_Terrain_Vines'))
    mineburst=node('BrambleMine_Burst','Burst',damage=25.0,damage_per_level=3.0,radius=220.0,on_expire=vines)
    stormburst=node('StormSeal_Burst','Burst',damage=30.0,damage_per_level=4.0,radius=250.0,damage_type=tag('Damage.Thunder'))
    specs={
      'FireSeed':('Projectile',dict(display_name='Fire Seed',mana_cost=18.0,cooldown=4.0,damage=20.0,on_hit=firefield)),
      'FrostFan':('Multishot',dict(display_name='Frost Fan',mana_cost=20.0,cooldown=5.0,damage=12.0,damage_type=tag('Damage.Ice'),on_hit=frost)),
      'BrambleMine':('Trap',dict(display_name='Bramble Mine',mana_cost=20.0,cooldown=5.0,damage=10.0,radius=220.0,duration=6.0,on_expire=mineburst)),
      'StormSeal':('Field',dict(display_name='Storm Seal',mana_cost=22.0,cooldown=6.0,damage=10.0,duration=3.0,damage_type=tag('Damage.Thunder'),on_expire=stormburst)),
    }
    classes={};configs={}
    for name,(source,values) in specs.items():
        configs[name]=node(name,source,cooldown_tag=tag('Cooldown.Composition.'+name),**values)
        classes[name]=ga(name,configs[name],source)
    backup(CATALOG);catalog=u.load_asset(CATALOG);rows=list(catalog.get_editor_property('ability_information'))
    old=[r.export_text() for r in rows];template=rows[0]
    for name in specs:
        identity='Abilities.Composition.'+name
        matches=[i for i,r in enumerate(rows) if r.get_editor_property('ability_tag').export_text()==tag(identity).export_text()]
        assert len(matches)<=1
        row=u.MyAbilityInfo();row.import_text(template.export_text())
        row.import_text('(AbilityTag=(TagName="'+identity+'"),InputTag=(TagName=""),StatusTag=(TagName=""),CooldownTag=(TagName="Cooldown.Composition.'+name+'"),AbilityType=(TagName="Abilities.Type.Offensive"),LevelRequirement=1,Ability="/Script/Engine.BlueprintGeneratedClass\''+BASE+'/GA_'+name+'.GA_'+name+'_C\'",Icon="/Script/Engine.Texture2D\'/Game/Assets/UI/Spells/ArcaneBlast.ArcaneBlast\'")')
        if matches:rows[matches[0]]=row
        else:rows.append(row)
    catalog.set_editor_property('ability_information',rows);save(catalog)
    for oldrow in old:
        if 'Abilities.Composition.' not in oldrow:assert oldrow in [r.export_text() for r in rows]
    report.update(success=True,assets={n:c.get_path_name() for n,c in configs.items()},catalog_count=len(rows))
except Exception as e:
    report['error']=repr(e);raise
finally:
    (RUN/'authoring.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    (RUN.parent/'latest-authoring.json').write_text(json.dumps(report,indent=2),encoding='utf8')
