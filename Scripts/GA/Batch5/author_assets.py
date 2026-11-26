"""UE Python: create only /Game/PrototypeExtensions/Batch5; preserve prior tuning."""
import unreal as u, json, hashlib
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve()
OUT=ROOT/'Saved/GAAuthoring/Batch5'; OUT.mkdir(parents=True,exist_ok=True)
BASE='/Game/PrototypeExtensions/Batch5'
TOOLS=u.AssetToolsHelpers.get_asset_tools(); ASSET=u.EditorAssetLibrary
created=[]; originals={}; nodes={}; classes={}
def remember(path):
    disk=ROOT/'Content'/(path.split('.')[0].removeprefix('/Game/')+'.uasset')
    if disk.exists(): originals[str(disk.relative_to(ROOT))]=hashlib.sha256(disk.read_bytes()).hexdigest()
def load(path):
    remember(path); a=u.load_asset(path); assert a,path; return a
def save(a):
    assert a.get_path_name().startswith(BASE+'/'),a.get_path_name()
    if isinstance(a,u.Blueprint): u.BlueprintEditorLibrary.compile_blueprint(a)
    assert ASSET.save_loaded_asset(a,False),a.get_path_name()
    return a
def cls(bp): return ASSET.load_blueprint_class(bp.get_path_name())
def duplicate(src,name):
    path=BASE+'/'+name
    if ASSET.does_asset_exist(path): return u.load_asset(path)
    a=TOOLS.duplicate_asset(name,BASE,load(src)); assert a,path; created.append(path); return a
def bp(src,name,parent=None):
    a=duplicate(src,name)
    if parent: u.BlueprintEditorLibrary.reparent_blueprint(a,parent)
    u.BlueprintEditorLibrary.compile_blueprint(a)
    return a,u.get_default_object(cls(a))
def tag(name):
    t=u.GameplayTag();t.import_text('(TagName="'+name+'")');return t
fire=load('/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/DA_Prototype_Projectile')
chain=load('/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/DA_Prototype_Chain')
burst=load('/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/DA_Prototype_Burst')
frost=load('/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/DA_Prototype_Surface')
vines=load('/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/DA_Surface_Vines')
halo=load('/Game/Assets/Effects/Stun/NS_Halo'); sparks=load('/Game/Assets/Effects/Shock/NS_Sparks')

def node(name,behavior,**values):
    path=BASE+'/DA_'+name
    if ASSET.does_asset_exist(path):
        a=u.load_asset(path)
        assert isinstance(a,u.ExtensionNode),path
    else:
        factory=u.DataAssetFactory();factory.set_editor_property('data_asset_class',u.ExtensionNode)
        a=TOOLS.create_asset('DA_'+name,BASE,u.DataAsset,factory);assert a,path
        base=dict(behavior=behavior,kind=u.AbilityPrototypeKind.PROJECTILE,display_name=name,damage=0.0,damage_per_level=0.0,
                  mana_cost=0.0,cooldown=0.0,period=1.0,duration=5.0,range=900.0,damage_type=tag('Damage.Magic'),
                  immune_actor_tags=['Boss','DisplacementImmune'],actor_tag_movement_scale={'Elite':0.35})
        base.update(values)
        for k,v in base.items(): a.set_editor_property(k,v)
        save(a);created.append(path)
    assert a.validate_graph(),path
    nodes[name]=a;return a
K=u.ExtensionKind

def explosion(name,damage,radius=180.0,element='Magic'):
    return node(name,K.BURST,damage=damage,damage_per_level=2.0,radius=radius,damage_type=tag('Damage.'+element),
                impact_vfx=burst.get_editor_property('impact_vfx') if element!='Fire' else fire.get_editor_property('impact_vfx'))
def ground(name,source,duration=3.0):
    return node(name,K.SURFACE,duration=duration,radius=130.0,surface_class=source.get_editor_property('surface_class'),loop_vfx=None)
def stack(name,child,effect): return node(name,K.STACK,effect_id=effect,complete_child=child,damage=0.0)
def projectile(name,child,**values):
    return node(name,K.PROJECTILE,hit_child=child,speed=900.0,duration=3.0,collision_radius=12.0,loop_vfx=fire.get_editor_property('loop_vfx'),impact_vfx=fire.get_editor_property('impact_vfx'),**values)
def rootvals(name,cost,cd,summary):
    return dict(display_name=name,mana_cost=cost,cooldown=cd,summary=summary)

roots={}
attachpulse=explosion('Attach_Pulse',10.0,100.0)
roots['Attach']=node('Prototype_Attach',K.ATTACH,period_child=attachpulse,loop_vfx=halo,**rootvals('附着脉冲',18.0,5.0,'附着敌人，每秒脉冲；死亡转移'))
markblast=explosion('Stack_Blast',35.0)
mark=stack('Stack_Mark',markblast,'Extension.Stack')
roots['Stack']=projectile('Prototype_Stack',mark,damage=12.0,**rootvals('叠层印记',8.0,.8,'命中积累印记，三层引爆'))
roots['Displace']=node('Prototype_Displace',K.DISPLACE,target_self=False,duration=.45,radius=320.0,loop_vfx=halo,**rootvals('牵引脉冲',16.0,5.0,'向指定中心牵引敌人'))
stormchain=node('StormBrand_Chain',K.CHAIN,damage=10.0,damage_per_level=2.0,damage_type=tag('Damage.Thunder'),count=3,period=.15,duration=1.0,radius=350.0,damage_falloff=.75,loop_vfx=chain.get_editor_property('loop_vfx'),impact_vfx=sparks)
roots['StormBrand']=node('StormBrand',K.ATTACH,period_child=stormchain,loop_vfx=sparks,**rootvals('雷霆烙印',24.0,7.0,'附着放出雷链，死亡后转移'))
frostpulse=explosion('FrostParasite_Pulse',8.0,120.0,'Ice')
frostground=ground('FrostParasite_Ground',frost)
roots['FrostParasite']=node('FrostParasite',K.ATTACH,period_child=frostpulse,complete_child=frostground,loop_vfx=load('/Game/Art/TerrainSurfaces/Effects/NS_Frost_Status'),**rootvals('寒霜寄生',20.0,6.0,'周期脉冲，结束留下冰霜地表'))
emberblast=explosion('EmberBrand_Blast',35.0,180.0,'Fire')
embermark=stack('EmberBrand_Mark',emberblast,'Extension.EmberBrand')
roots['EmberBrand']=projectile('EmberBrand',embermark,damage=12.0,damage_per_level=2.0,damage_type=tag('Damage.Fire'),**rootvals('三重焰印',8.0,.8,'火弹命中三次，引爆范围火伤'))
convergeblast=explosion('StormConvergence_Blast',25.0,160.0,'Thunder')
convergemark=stack('StormConvergence_Mark',convergeblast,'Extension.StormConvergence')
roots['StormConvergence']=node('StormConvergence',K.CHAIN,hit_child=convergemark,damage=8.0,damage_per_level=2.0,damage_type=tag('Damage.Thunder'),count=3,period=.2,duration=1.0,radius=350.0,loop_vfx=chain.get_editor_property('loop_vfx'),impact_vfx=sparks,**rootvals('雷链聚爆',18.0,2.0,'雷链叠印，三层触发雷暴'))
gravityblast=explosion('GravityWell_Blast',35.0,230.0)
gravitydelay=node('GravityWell_Delay',K.DELAY,arm_delay=.35,duration=.5,complete_child=gravityblast,loop_vfx=halo)
roots['GravityWell']=node('GravityWell',K.DISPLACE,complete_child=gravitydelay,duration=.45,radius=320.0,loop_vfx=halo,**rootvals('引力塌缩',25.0,7.0,'聚拢敌人，短暂延迟后爆炸'))
brambleground=ground('BrambleRepulse_Ground',vines,2.0)
bramblemove=node('BrambleRepulse_Move',K.DISPLACE,single_target=True,pull=False,move_distance=150.0,duration=.45,complete_child=brambleground)
roots['BrambleRepulse']=node('BrambleRepulse',K.BURST,target_self=True,damage=15.0,damage_per_level=2.0,radius=300.0,hit_child=bramblemove,impact_vfx=burst.get_editor_property('impact_vfx'),**rootvals('荆棘震退',24.0,7.0,'震退近敌，落点留下缠绕藤蔓'))

for name,config in roots.items():
    config.set_editor_property('cooldown_tag',tag('Cooldown.Extension.'+name));save(config)
    ga_name=('GA_Prototype_' if name in ['Attach','Stack','Displace'] else 'GA_')+name
    asset,cdo=bp('/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes/GA_Prototype_Projectile',ga_name,u.ExtensionAbility)
    cdo.set_editor_property('prototype',config)
    tags=u.GameplayTagContainer();tags.import_text('(GameplayTags=((TagName="Abilities.Extension.'+name+'")))')
    cdo.set_editor_property('ability_tags',tags);cdo.set_editor_property('startup_input_tag',tag(''))
    cdo.set_editor_property('net_execution_policy',u.GameplayAbilityNetExecutionPolicy.SERVER_ONLY if config.get_editor_property('target_self') else u.GameplayAbilityNetExecutionPolicy.LOCAL_PREDICTED)
    save(asset);classes[name]=cls(asset)

catalog=duplicate('/Game/PureMass/DA_MassAbilityInfo','DA_AbilityInfo')
rows=list(catalog.get_editor_property('ability_information'));template=rows[0]
for name,klass in classes.items():
    identity='Abilities.Extension.'+name
    if any(r.get_editor_property('ability_tag').export_text()==tag(identity).export_text() for r in rows):continue
    row=u.MyAbilityInfo();row.import_text(template.export_text())
    row.import_text('(AbilityTag=(TagName="'+identity+'"),InputTag=(TagName=""),StatusTag=(TagName=""),CooldownTag=(TagName="Cooldown.Extension.'+name+'"),AbilityType=(TagName="Abilities.Type.Offensive"),LevelRequirement=1,Ability="/Script/Engine.BlueprintGeneratedClass\''+klass.get_path_name()+'\'",Icon="/Script/Engine.Texture2D\'/Game/Assets/UI/Spells/ArcaneBlast.ArcaneBlast\'")')
    rows.append(row)
catalog.set_editor_property('ability_information',rows);save(catalog)
hud,hcdo=bp('/Game/PureMass/BP_MassHUD','BP_ExtensionHUD',u.ExtensionHUD)
for key in ['overlay_widget_controller_class','spell_menu_widget_controller_class','attribute_menu_widget_controller_class']:
    parent=hcdo.get_editor_property(key);name='BP_Extension_'+key;path=BASE+'/'+name
    if ASSET.does_asset_exist(path): child=u.load_asset(path)
    else:
        factory=u.BlueprintFactory();factory.set_editor_property('parent_class',parent)
        child=TOOLS.create_asset(name,BASE,u.Blueprint,factory)
    u.BlueprintEditorLibrary.compile_blueprint(child)
    u.get_default_object(cls(child)).set_editor_property('ability_info',catalog);save(child)
    hcdo.set_editor_property(key,cls(child))
save(hud)
pawn,pcdo=bp('/Game/PureMass/BP_MassPlayer','BP_ExtensionPlayer',u.ExtensionPlayerCharacter)
save(pawn)
mode,mcdo=bp('/Game/PureMass/BP_PureMassGameMode','BP_ExtensionGameMode',u.ExtensionGameMode)
mcdo.set_editor_property('ability_info',catalog);mcdo.set_editor_property('hud_class',cls(hud));mcdo.set_editor_property('default_pawn_class',cls(pawn));save(mode)
levels=u.get_editor_subsystem(u.LevelEditorSubsystem); editor=u.get_editor_subsystem(u.UnrealEditorSubsystem)
original=editor.get_editor_world().get_path_name().split('.')[0]
map_path=BASE+'/L_PrototypeLab'
if not ASSET.does_asset_exist(map_path): assert levels.new_level_from_template(map_path,'/Game/levels/L_DungeonLabyrinth')
else: assert levels.load_level(map_path)
world=editor.get_editor_world();world.get_world_settings().set_editor_property('default_game_mode',cls(mode))
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
for a in list(u.GameplayStatics.get_all_actors_of_class(world,u.MapEntrance)): actors.destroy_actor(a)
regions=list(u.GameplayStatics.get_all_actors_of_class(world,u.PureMassSpawnRegion))
assert sum(a.get_editor_property('count') for a in regions)==200
assert levels.save_current_level()
regions.clear();a=None;world=None
# Leave the dedicated new level open for the next validation stage.
for path,digest in originals.items(): assert hashlib.sha256((ROOT/path).read_bytes()).hexdigest()==digest,path
(OUT/'assets.json').write_text(json.dumps(dict(passed=True,classes={k:v.get_path_name() for k,v in classes.items()},nodes={k:v.get_path_name() for k,v in nodes.items()},created=created,catalog_count=len(rows),map=map_path,previous_map=original,original_hashes=originals),indent=2,ensure_ascii=False),encoding='utf-8')
u.log('BATCH5_ASSETS_AUTHORED')
