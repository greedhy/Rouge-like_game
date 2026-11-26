"""Author batch four independently; preserve existing tuning and unrelated catalog rows."""
import unreal as u, json, uuid, shutil
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve()
RUN=ROOT/'Saved/GAAuthoring/Batch4'/uuid.uuid4().hex
RUN.mkdir(parents=True)
PROTO='/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes'
COMBO='/Game/BluePrints/AbilitySystem/Player/Ability/Compositions'
CATALOG='/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo'
TOOLS=u.AssetToolsHelpers.get_asset_tools()
created=[]

def tag(name):
    t=u.GameplayTag();t.import_text('(TagName="'+name+'")');return t
def save(a):
    if isinstance(a,u.Blueprint):u.BlueprintEditorLibrary.compile_blueprint(a)
    assert u.EditorAssetLibrary.save_loaded_asset(a,False),a.get_path_name()
def backup(path):
    src=ROOT/'Content'/(path.removeprefix('/Game/')+'.uasset')
    if src.exists():
        dest=RUN/'backup'/src.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dest)
def node(folder,name,source,**values):
    path=folder+'/'+name
    if u.EditorAssetLibrary.does_asset_exist(path):a=u.load_asset(path)
    else:
        a=TOOLS.duplicate_asset(name,folder,u.load_asset(PROTO+'/DA_Prototype_'+source));assert a,path
        # A new graph never inherits the source node's connections or fees.
        defaults=dict(on_hit=None,on_expire=None,on_period=None,mana_cost=0.0,cooldown=0.0,cooldown_tag=tag(''))
        defaults.update(values)
        for k,v in defaults.items():a.set_editor_property(k,v)
        save(a);created.append(path)
    assert isinstance(a,u.AbilityPrototype) and a.is_valid_configuration(),path
    return a
def ga(folder,name,config,source,identity):
    path=folder+'/'+name;backup(path)
    if u.EditorAssetLibrary.does_asset_exist(path):bp=u.load_asset(path)
    else:bp=TOOLS.duplicate_asset(name,folder,u.load_asset(PROTO+'/GA_Prototype_'+source));created.append(path)
    cls=u.EditorAssetLibrary.load_blueprint_class(path);cdo=u.get_default_object(cls)
    assert isinstance(cdo,u.PrototypeAbility),path
    cdo.set_editor_property('prototype',config)
    tags=u.GameplayTagContainer();tags.import_text('(GameplayTags=((TagName="'+identity+'")))')
    cdo.set_editor_property('ability_tags',tags);cdo.set_editor_property('startup_input_tag',tag(''))
    cdo.set_editor_property('net_execution_policy',u.GameplayAbilityNetExecutionPolicy.SERVER_ONLY if config.get_editor_property('target_self') else u.GameplayAbilityNetExecutionPolicy.LOCAL_PREDICTED)
    save(bp);return cls

report={'run_id':RUN.name,'success':False}
try:
    shutil.copy2(ROOT/'docs/superpowers/plans/2026-10-23-ga-prototypes-batch4.md',RUN/'spec.md')
    halo=u.load_asset('/Game/Assets/Effects/Stun/NS_Halo');sparks=u.load_asset('/Game/Assets/Effects/Shock/NS_Sparks')
    assert halo and sparks
    delayed=node(PROTO,'DA_Prototype_DelayBurst','Burst',display_name='Delayed Burst',damage=30.0,damage_per_level=5.0,radius=300.0)
    ring=dict(kind=u.AbilityPrototypeKind.RING,target_self=True,radius=360.0,inner_radius=100.0,shape_half_height=120.0,duration=.6,loop_vfx=halo,impact_vfx=None)
    line=dict(kind=u.AbilityPrototypeKind.LINE,target_self=False,range=650.0,width=140.0,shape_half_height=120.0,duration=.5,loop_vfx=None,impact_vfx=sparks)
    delay=dict(kind=u.AbilityPrototypeKind.DELAY,target_self=True,arm_delay=1.0,duration=2.0,damage=0.0,damage_per_level=0.0,loop_vfx=halo,impact_vfx=None)
    configs={};classes={};identities={}
    specs={
        'Delay':('Burst',dict(delay,display_name='Delayed Pulse',on_expire=delayed,mana_cost=14.0,cooldown=4.0)),
        'Ring':('Burst',dict(ring,display_name='Arcane Ring',damage=30.0,mana_cost=16.0,cooldown=4.0)),
        'Line':('Projectile',dict(line,display_name='Arcane Lance',damage=35.0,damage_type=tag('Damage.Magic'),mana_cost=14.0,cooldown=3.0)),
    }
    for name,(source,values) in specs.items():
        configs[name]=node(PROTO,'DA_Prototype_'+name,source,cooldown_tag=tag('Cooldown.Prototype.'+name),**values)
        identities[name]='Abilities.Prototype.'+name
        classes[name]=ga(PROTO,'GA_Prototype_'+name,configs[name],source,identities[name])
    # Every composition owns its child nodes, so tuning a sample does not change another GA.
    afterburst=node(COMBO,'DA_Aftershock_Burst','Burst',display_name='Aftershock Blast',damage=12.0,damage_per_level=2.0,radius=90.0)
    afterring=node(COMBO,'DA_Aftershock_Ring','Burst',**dict(ring,display_name='Aftershock Ring',radius=420.0,damage=24.0,damage_per_level=4.0,on_hit=afterburst))
    frost=node(COMBO,'DA_FrostFault_Ground','Surface',display_name='Frost Patch',radius=130.0,duration=3.0)
    wave=node(COMBO,'DA_RippleMine_Wave','Wave',display_name='Ripple Wave',radius=450.0,speed=400.0,duration=2.0,damage=28.0,damage_per_level=4.0)
    mine_delay=node(COMBO,'DA_RippleMine_Delay','Burst',**dict(delay,display_name='Ripple Fuse',arm_delay=.6,on_expire=wave))
    hunterblast=node(COMBO,'DA_HunterSentry_Burst','Burst',display_name='Hunter Blast',damage=10.0,damage_per_level=2.0,radius=100.0)
    huntershot=node(COMBO,'DA_HunterSentry_Shot','Homing',display_name='Hunter Bolt',damage=18.0,damage_per_level=3.0,range=900.0,speed=650.0,duration=3.0,on_hit=hunterblast)
    combinations={
        'Aftershock':('Burst',dict(delay,display_name='Aftershock',on_expire=afterring,mana_cost=22.0,cooldown=6.0)),
        'FrostFault':('Projectile',dict(line,display_name='Frost Fault',damage=22.0,damage_per_level=4.0,damage_type=tag('Damage.Ice'),on_hit=frost,mana_cost=20.0,cooldown=5.0)),
        'RippleMine':('Trap',dict(display_name='Ripple Mine',damage=0.0,damage_per_level=0.0,radius=220.0,duration=12.0,on_expire=mine_delay,mana_cost=20.0,cooldown=6.0)),
        'HunterSentry':('Burst',dict(kind=u.AbilityPrototypeKind.EMITTER,display_name='Hunter Sentry',target_self=True,damage=0.0,damage_per_level=0.0,radius=700.0,period=1.0,count=3,duration=5.0,on_period=huntershot,loop_vfx=halo,impact_vfx=None,mana_cost=24.0,cooldown=7.0)),
    }
    for name,(source,values) in combinations.items():
        configs[name]=node(COMBO,'DA_'+name,source,cooldown_tag=tag('Cooldown.Composition.'+name),**values)
        identities[name]='Abilities.Composition.'+name
        classes[name]=ga(COMBO,'GA_'+name,configs[name],source,identities[name])
    backup(CATALOG);catalog=u.load_asset(CATALOG);rows=list(catalog.get_editor_property('ability_information'))
    old=[r.export_text() for r in rows];template=rows[0]
    for name,cls in classes.items():
        identity=identities[name]
        matches=[i for i,r in enumerate(rows) if r.get_editor_property('ability_tag').export_text()==tag(identity).export_text()]
        assert len(matches)<=1,identity
        row=u.MyAbilityInfo();row.import_text(template.export_text())
        row.import_text('(AbilityTag=(TagName="'+identity+'"),InputTag=(TagName=""),StatusTag=(TagName=""),CooldownTag='+configs[name].get_editor_property('cooldown_tag').export_text()+',AbilityType=(TagName="Abilities.Type.Offensive"),LevelRequirement=1,Ability="/Script/Engine.BlueprintGeneratedClass\''+cls.get_path_name()+'\'",Icon="/Script/Engine.Texture2D\'/Game/Assets/UI/Spells/ArcaneBlast.ArcaneBlast\'")')
        if matches:rows[matches[0]]=row
        else:rows.append(row)
    catalog.set_editor_property('ability_information',rows);save(catalog)
    for oldrow in old:
        if not any(identity+'"' in oldrow for identity in identities.values()):assert oldrow in [r.export_text() for r in rows]
    report.update(success=True,assets={n:c.get_path_name() for n,c in configs.items()},created=created,catalog_count=len(rows))
except Exception as e:report['error']=repr(e);raise
finally:
    (RUN/'authoring.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    (RUN.parent/'latest-authoring.json').write_text(json.dumps(report,indent=2),encoding='utf8')
