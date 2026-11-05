"""Owned Stander showcase assets; source content remains intact."""
import unreal as u,json,uuid,shutil
from pathlib import Path
assert not u.EditorLevelLibrary.get_game_world(),'Stop PIE before asset writes'
root=Path(u.Paths.project_dir()).resolve();folder=root/'Saved/GAAuthoring/Stander';backup=folder/('author-'+uuid.uuid4().hex);backup.mkdir(parents=True)
R='/Game/BluePrints/Characters/Stander';owner='StanderShowcaseV1';tools=u.AssetToolsHelpers.get_asset_tools()
dirty={p.get_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()};saved=[]
def existing(name):
 path=R+'/'+name;assert path not in dirty,'Unsaved target '+path
 if not u.EditorAssetLibrary.does_asset_exist(path):return None
 a=u.load_asset(path);assert u.EditorAssetLibrary.get_metadata_tag(a,'AuthoringOwner')==owner,'Unowned target '+path
 f=root/'Content'/(path.removeprefix('/Game/')+'.uasset')
 if f.exists():shutil.copy2(f,backup/(name.replace('/','_')+'.uasset'))
 return a
def save(a):
 u.EditorAssetLibrary.set_metadata_tag(a,'AuthoringOwner',owner)
 assert u.EditorAssetLibrary.save_loaded_asset(a,False);saved.append(a.get_path_name());return a
def duplicate(source,name):return existing(name) or u.EditorAssetLibrary.duplicate_asset(source,R+'/'+name)
def data(name,cls):
 a=existing(name)
 if not a:
  f=u.DataAssetFactory();f.set_editor_property('data_asset_class',cls);a=tools.create_asset(name,R,cls,f)
 return a
def bp(name,cls):
 a=existing(name)
 if not a:
  f=u.BlueprintFactory();f.set_editor_property('parent_class',cls);a=tools.create_asset(name,R,u.Blueprint,f)
 u.BlueprintEditorLibrary.compile_blueprint(a);return a,u.get_default_object(a.generated_class())
def compile_save(a):
 u.BlueprintEditorLibrary.compile_blueprint(a);assert str(a.get_editor_property('status'))!='BlueprintStatus.BS_ERROR';save(a)
def tag(name):
 t=u.GameplayTag();t.import_text('(TagName="'+name+'")');return t
source='/Game/PlatformAnimation/Animation/01_Sword_Shield/Stander_Sword_Shield_'
clips={};montages={}
mapping={'Idle':'Idle','Move':'Run','Combo1':'Attack1','Combo2':'Attack2','Combo3':'Attack3','Spin':'Skill7_Around','Guard':'Block_Loop','CounterStrike':'Attack1','Dash':'Skill6_Move'}
for name,path in mapping.items():
 a=duplicate(source+path,'Animations/AS_Stander_'+name);a.set_editor_property('enable_root_motion',False);a.set_editor_property('force_root_lock',True);a.set_editor_property('root_motion_root_lock',u.RootMotionRootLock.ZERO)
 for e in u.AnimationLibrary.get_animation_notify_events(a):u.AnimationLibrary.remove_animation_notify_events_by_name(a,e.get_editor_property('notify_name'))
 clips[name]=save(a)
 if name in ['Idle','Move']:continue
 m=existing('Animations/AM_Stander_'+name)
 if not m:
  f=u.AnimMontageFactory();f.set_editor_property('target_skeleton',a.get_editor_property('skeleton'));f.set_editor_property('source_animation',a)
  m=tools.create_asset('AM_Stander_'+name,R+'/Animations',u.AnimMontage,f)
 for field in ['blend_in','blend_out']:
  blend=m.get_editor_property(field);blend.set_editor_property('blend_time',.06);m.set_editor_property(field,blend)
 if name=='Guard':m.set_editor_property('blend_out_trigger_time',0.)
 montages[name]=save(m)
bs=existing('Animations/BS_Stander_Move')
if not bs:
 f=u.BlendSpaceFactory1D();f.set_editor_property('target_skeleton',clips['Idle'].get_editor_property('skeleton'));bs=tools.create_asset('BS_Stander_Move',R+'/Animations',u.BlendSpace1D,f)
axes=bs.get_editor_property('blend_parameters');axis=axes[0];axis.set_editor_property('max',450.);axes[0]=axis;bs.set_editor_property('blend_parameters',axes)
samples=[]
for name,speed in [('Idle',0),('Move',450)]:
 s=u.BlendSample();s.set_editor_property('animation',clips[name]);s.set_editor_property('sample_value',u.Vector(speed,0,0));samples.append(s)
bs.set_editor_property('sample_data',samples);save(bs)
editors=u.get_editor_subsystem(u.AssetEditorSubsystem)
assert editors.open_editor_for_assets([bs]);save(bs);editors.close_all_editors_for_asset(bs)
abp=duplicate('/Game/BluePrints/Characters/StoneGolem/Animations/ABP_SG_Elite','Animations/ABP_Stander')
abp.set_editor_property('target_skeleton',clips['Idle'].get_editor_property('skeleton'))
for cls,key,asset in [(u.AnimGraphNode_SequencePlayer,'sequence',clips['Idle']),(u.AnimGraphNode_BlendSpacePlayer,'blend_space',bs)]:
 for node in u.ObjectIterator(cls):
  if abp.get_path_name() in node.get_path_name():
   val=node.get_editor_property('node');val.set_editor_property(key,asset);node.set_editor_property('node',val)
compile_save(abp)
cfg=data('DA_Stander',u.StanderCombatConfig)
combo=[]
for i,contact in enumerate([.1833333,.2,.25]):
 s=u.SwordStrike()
 for k,v in dict(montage=montages['Combo'+str(i+1)],damage=[25.,30.,40.][i],radius=180.,angle=[100.,110.,120.][i],contact=contact,play_rate=1.).items():s.set_editor_property(k,v)
 combo.append(s)
spin=u.SwordStrike()
for k,v in dict(montage=montages['Spin'],damage=55.,radius=220.,angle=360.,contact=.3333333,play_rate=1.).items():spin.set_editor_property(k,v)
for k,v in dict(combo=combo,spin=spin,guard_montage=montages['Guard'],combo_reset=.9,guard_duration=.8,guard_cooldown=3.,spin_cooldown=5.,spin_mana=15.,hit_effect=u.load_asset('/Game/Assets/Effects/Combat/NS_CombatImpact')).items():cfg.set_editor_property(k,v)
for name,contact,damage,angle in [('Counter',.1833333,35.,110.),('Dash',.5166667,45.,100.)]:
 strike=u.SwordStrike()
 for k,v in dict(montage=montages['CounterStrike' if name=='Counter' else name],damage=damage,radius=180.,angle=angle,contact=contact,play_rate=1.).items():strike.set_editor_property(k,v)
 cfg.set_editor_property(name.lower(),strike)
cfg.set_editor_property('dash_start',.3333333);cfg.set_editor_property('dash_end',.5)

save(cfg)
abilities=[]
for name,cls,key in [('Combo',u.StanderComboAbility,'LMB'),('Guard',u.StanderGuardAbility,'RMB'),('Spin',u.StanderSpinAbility,'1'),('Dash',u.StanderDashAbility,'2')]:
 a,cdo=bp('GA_Stander_'+name,cls.static_class())
 cdo.set_editor_property('startup_input_tag',tag('InputTag.'+key));cdo.set_editor_property('damage_type',tag('Damage.Physics'))
 cdo.set_editor_property('damage_effect_class',u.load_class(None,'/Game/BluePrints/AbilitySystem/Player/Effect/GE_Damage.GE_Damage_C'))
 compile_save(a);abilities.append(a.generated_class())
player=duplicate('/Game/RoguelikeGeneration/Gameplay/BP_DungeonPlayer','BP_StanderPlayer')
u.BlueprintEditorLibrary.reparent_blueprint(player,u.StanderPlayer.static_class());u.BlueprintEditorLibrary.compile_blueprint(player)
cdo=u.get_default_object(player.generated_class());mesh=cdo.get_component_by_class(u.SkeletalMeshComponent)
mesh.set_editor_property('skeletal_mesh_asset',u.load_asset('/Game/PlatformAnimation/Models/Stander'));mesh.set_editor_property('anim_class',abp.generated_class())
mesh.set_editor_property('relative_location',u.Vector(0,0,-96));mesh.set_editor_property('relative_rotation',u.Rotator(yaw=-90));mesh.set_editor_property('relative_scale3d',u.Vector(1,1,1))
cdo.get_editor_property('weapon').set_editor_property('skeletal_mesh_asset',None)
cdo.get_component_by_class(u.CapsuleComponent).set_editor_property('capsule_half_height',96.)
cdo.get_editor_property('stander_sword').set_editor_property('skeletal_mesh_asset',u.load_asset('/Game/PlatformAnimation/Models/Weapon.Weapon_Sword'))
cdo.get_editor_property('stander_sword').attach_to_component(mesh,'ik_hand_r',u.AttachmentRule.KEEP_RELATIVE,u.AttachmentRule.KEEP_RELATIVE,u.AttachmentRule.KEEP_RELATIVE,False)
cdo.get_editor_property('stander_shield').set_editor_property('skeletal_mesh_asset',u.load_asset('/Game/PlatformAnimation/Models/Weapon.Weapon_Shield'))
demo=u.load_asset('/Game/PlatformAnimation/Maps/Platform_Animations')
for actor_name,component_name in [('Weapon_Sword_14','stander_sword'),('Weapon_Shield_17','stander_shield')]:
 actor=next(a for a in u.GameplayStatics.get_all_actors_of_class(demo,u.Actor) if a.get_name()==actor_name)
 source_component=actor.get_component_by_class(u.SkeletalMeshComponent);tr=source_component.get_relative_transform();dest=cdo.get_editor_property(component_name)
 dest.set_editor_property('relative_location',tr.translation);dest.set_editor_property('relative_rotation',tr.rotation.rotator());dest.set_editor_property('relative_scale3d',tr.scale3d)
cdo.set_editor_property('sword_config',cfg);cdo.set_editor_property('startup_ability',abilities);cdo.set_editor_property('character_class',u.CharacterClass.WARRIOR)
cdo.get_component_by_class(u.CharacterMovementComponent).set_editor_property('max_walk_speed',450.)
compile_save(player)
catalog=data('DA_SwordAbilityInfo',u.AbilityInfo)
old=u.load_asset('/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo').get_editor_property('ability_information');template=old[0]
rows=[]
for i,name in enumerate(['Combo','Guard','Spin','Dash']):
 row=u.MyAbilityInfo()
 fields=['AbilityTag=(TagName="Abilities.Stander.'+name+'")','AbilityType=(TagName="Abilities.Type.Offensive")','LevelRequirement=1','Ability="'+abilities[i].get_path_name()+'"']
 if name!='Combo':fields.append('CooldownTag=(TagName="Cooldown.Stander.'+name+'")')
 for field,key in [('Icon','icon'),('BackgroundMaterial','background_material')]:
  value=template.get_editor_property(key)
  if key=='icon':value=u.load_asset(['/Game/KiroshoSkIconsPk0/Textures/T_DeepCut','/Game/Skill_Icons/Textures/512x512/T_Skill_Icons__23','/Game/KiroshoSkIconsPk0/Textures/T_StormAttack','/Game/KiroshoSkIconsPk0/Textures/T_PiercingAttack'][i])
  if value:fields.append(field+'="'+value.get_path_name()+'"')
 row.import_text('('+','.join(fields)+')')
 rows.append(row)
catalog.set_editor_property('ability_information',rows);save(catalog)
mode=duplicate('/Game/RoguelikeGeneration/Gameplay/BP_DungeonRunGameMode','BP_StanderGameMode');mcdo=u.get_default_object(mode.generated_class())
mcdo.set_editor_property('default_pawn_class',player.generated_class());mcdo.set_editor_property('ability_info',catalog);compile_save(mode)
(folder/'assets.json').write_text(json.dumps({'saved':saved,'backup':str(backup),'player':player.get_path_name(),'mode':mode.get_path_name(),'contact_candidates':[.1833333,.2,.25],'runtime_verified':False},indent=2))
print('Stander assets complete',len(saved))
