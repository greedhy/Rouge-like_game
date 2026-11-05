"""Run only after new native reflection is built and editor restarted. No auto execution."""
import sys,json
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
import importlib
if "author_common" in sys.modules:importlib.reload(sys.modules["author_common"])
from author_common import *
manifest=json.loads((Path(__file__).parent/'weapon-manifest.json').read_text(encoding='utf-8'))
assert hasattr(u,'StanderWeaponActionConfig') and hasattr(u,'StanderWeaponActionAbility')
rows=[];catalog_rows=[]
# Resolve production references instead of fabricating VFX object paths.
stander_cfg=u.load_asset('/Game/BluePrints/Characters/Stander/DA_Stander');assert stander_cfg
hit_effect=stander_cfg.get_editor_property('hit_effect');assert isinstance(hit_effect,u.NiagaraSystem),'Existing Stander HitEffect missing'
projectile_source=u.load_asset('/Game/CompositionBatch6/DA_SolarFireball');assert projectile_source
projectile_effect=projectile_source.get_editor_property('loop_vfx');assert isinstance(projectile_effect,u.NiagaraSystem),'Existing projectile VFX missing'
(RUN/'effect-references.json').write_text(json.dumps({'hit':hit_effect.get_path_name(),'projectile':projectile_effect.get_path_name()},indent=2))
fixed_ge,fixed_cdo=bp(BASE+'/GE_WeaponTraining',u.EquipmentGameplayEffect)
modifier=u.GameplayModifierInfo();modifier.import_text('(Attribute=(AttributeName="Armor",Attribute=/Script/test_25d.MyAttributeSet:Armor,AttributeOwner="/Script/CoreUObject.Class\'/Script/test_25d.MyAttributeSet\'"),ModifierOp=AddBase,ModifierMagnitude=(MagnitudeCalculationType=ScalableFloat,ScalableFloatMagnitude=(Value=1.0)),EvaluationChannelSettings=(Channel=Channel1))')
fixed_cdo.set_editor_property('modifiers',[modifier]);save(fixed_ge)
for family_index,family in enumerate(manifest['families']):
 (RUN/'progress.json').write_text(json.dumps({'state':'authoring','family_index':family_index,'family':family['family'],'saved':len(SAVED)},indent=2))
 name=family['family'];folder=BASE+'/'+name;animations={}
 sources={a['name']:a['source'] for a in family['actions']}
 sources.update(family['locomotion'])
 for helper in family['helper_clips']:sources[helper['source'].rsplit('.',1)[-1].removeprefix('Stander_'+name+'_')]=helper['source']
 for key,source in sources.items():animations[key]=clip(source,folder+'/Animations/AS_'+key)
 idle=animations['Idle'];bs=existing(folder+'/Animations/BS_Move')
 if not bs:
  fac=u.BlendSpaceFactory1D();fac.set_editor_property('target_skeleton',idle.get_editor_property('skeleton'));bs=TOOLS.create_asset('BS_Move',folder+'/Animations',u.BlendSpace1D,fac)
 axes=bs.get_editor_property('blend_parameters');axis=axes[0];axis.set_editor_property('max',450.);axes[0]=axis;bs.set_editor_property('blend_parameters',axes)
 samples=[]
 for key,speed in [('Idle',0.),('Run',450.)]:
  sample=u.BlendSample();sample.set_editor_property('animation',animations[key]);sample.set_editor_property('sample_value',u.Vector(speed,0,0));samples.append(sample)
 bs.set_editor_property('sample_data',samples);save(bs)
 editors=u.get_editor_subsystem(u.AssetEditorSubsystem);assert editors.open_editor_for_assets([bs]);save(bs);editors.close_all_editors_for_asset(bs)
 abp=duplicate('/Game/BluePrints/Characters/Stander/Animations/ABP_Stander',folder+'/Animations/ABP_Move')
 for cls,prop,asset in [(u.AnimGraphNode_SequencePlayer,'sequence',idle),(u.AnimGraphNode_BlendSpacePlayer,'blend_space',bs)]:
  for node in u.ObjectIterator(cls):
   if abp.get_path_name() in node.get_path_name():v=node.get_editor_property('node');v.set_editor_property(prop,asset);node.set_editor_property('node',v)
 save(abp);entries=[]
 defaults=['Attack1','Block','Skill7_Around' if any(a['name']=='Skill7_Around' for a in family['actions']) else 'Skill7','Skill6_Move' if any(a['name']=='Skill6_Move' for a in family['actions']) else 'Skill6','Skill1_Float' if any(a['name']=='Skill1_Float' for a in family['actions']) else 'Skill1','Skill4_Push' if any(a['name']=='Skill4_Push' for a in family['actions']) else 'Skill4'];keys=['LMB','RMB','1','2','3','4']
 for action in family['actions']:
  (RUN/'progress.json').write_text(json.dumps({'state':'authoring','family_index':family_index,'family':name,'action':action['name'],'saved':len(SAVED)},indent=2))
  key=action['name'];stage_names=[key];event_offset=0.;offsets=[0.];airborne='airborne' in action['flags']
  if action['kind']=='Guard':
   stage_names=[n for n in ['Block_Start' if 'Block_Start' in animations else 'Block_In','Block_Loop','Block_End' if 'Block_End' in animations else 'Block_Out'] if n in animations and (n=='Block_Loop' or animations[n].get_play_length()<=.5)]
  elif airborne:
   # Keep the authored attack, then its air follow-through and landing helper.
   air_variant=key.replace('JumpAttack','Attack')+'_Air'
   stage_names=([n for n in ['Jump'] if n in animations]+[key]+[n for n in [air_variant,key+'_Ground','JumpFall'] if n in animations])
  if len(stage_names)>1:
   m,offsets=montage_stages([animations[n] for n in stage_names],folder+'/Animations/AM_'+key)
   event_offset=offsets[stage_names.index(key)] if key in stage_names else offsets[stage_names.index('Block_Loop')]
  else:m=montage(animations[key],folder+'/Animations/AM_'+key)
  cfg=data(folder+'/DA_'+key,u.StanderWeaponActionConfig)
  identity='Abilities.StanderWeapons.'+name+'.'+key;cooldown='Cooldown.StanderWeapons.'+name+'.'+key
  label=(name+' 招式'+key.split('_')[0].removeprefix('Skill')) if key.startswith('Skill') else name+' '+key
  for k,v in dict(display_name=label,weapon_family=name,action_tag=tag(identity),cooldown_tag=tag(cooldown),montage=m,play_rate=1.,mana_cost=action['mana'],cooldown=action['cooldown']).items():cfg.set_editor_property(k,v)
  events=[]
  def event(kind,time,**props):
   e=u.StanderWeaponActionEvent();e.set_editor_property('kind',getattr(u.StanderWeaponActionKind,kind.upper()));e.set_editor_property('time',time)
   for k,v in props.items():e.set_editor_property(k,v)
   events.append(e)
  if action['kind']=='Guard':event('Guard',event_offset,duration=animations['Block_Loop'].get_play_length())
  elif action['kind']=='Evade':event('Move',.05,move_distance=250.,duration=min(.3,action['duration']*.5))
  else:
   if airborne:
    landing_name=next((n for n in [key+'_Ground','JumpFall'] if n in stage_names),None)
    landing_time=offsets[stage_names.index(landing_name)] if landing_name else m.get_play_length()-.05
    jump_start=.02
    assert landing_time>jump_start
    event('Move',jump_start,move_distance=150.,move_height=100.,duration=landing_time-jump_start)
   if 'displacement' in action['flags'] and not airborne:event('Move',max(.02,action['events'][0]-.18),move_distance=350.,duration=.15)
   for contact in action['events']:
    radial_projectile=action['kind']=='Projectile' and 'radial' in action['flags']
    for yaw in (range(0,360,45) if radial_projectile else [0]):
     event(action['kind'],event_offset+contact,damage=action['damage']/4 if radial_projectile else action['damage'],radius=action['projectile_radius'] if action['kind']=='Projectile' else action['radius'],angle=action['angle'],death_impulse=u.Vector(*action['death_impulse']),projectile_speed=1600.,projectile_lifetime=1.2,launch_yaw=float(yaw),socket_name='ik_hand_l' if name in ['Bow','Staff','Spear','Rapier'] else 'ik_hand_r',effect=projectile_effect if action['kind']=='Projectile' else hit_effect)
  events.sort(key=lambda e:e.get_editor_property('time'));cfg.set_editor_property('events',events);save(cfg)
  ga,cdo=bp(folder+'/GA_'+key,u.StanderWeaponActionAbility);cdo.set_editor_property('action_config',cfg);cdo.initialize_action_identity();cdo.set_editor_property('damage_type',tag('Damage.Magic' if name in ['Magic','Staff'] else 'Damage.Physics'));cdo.set_editor_property('damage_effect_class',u.load_class(None,'/Game/BluePrints/AbilitySystem/Player/Effect/GE_Damage.GE_Damage_C'));save(ga)
  entry=u.WeaponAbilityEntry();entry.set_editor_property('ability',ga.generated_class());entry.set_editor_property('level',1)
  if key in defaults:entry.set_editor_property('input_tag',tag('InputTag.'+keys[defaults.index(key)]))
  entries.append(entry)
  info=u.MyAbilityInfo();info.import_text('(AbilityTag=(TagName="'+identity+'"),CooldownTag=(TagName="'+cooldown+'"),AbilityType=(TagName="Abilities.Type.Offensive"),LevelRequirement=1,Ability="'+ga.generated_class().get_path_name()+'",Icon="'+u.load_asset(action['icon']).get_path_name()+'")');catalog_rows.append(info)
 weapon=data(folder+'/DA_WeaponSet',u.WeaponSetConfig)
 for k,v in dict(family=name,abilities=entries,anim_class=abp.generated_class()).items():weapon.set_editor_property(k,v)
 for slot,index in [('main',0),('offhand',1)]:
  attachment=family['attachments'][index] if len(family['attachments'])>index else None
  weapon.set_editor_property(slot+'_mesh',u.load_asset(attachment['mesh']) if attachment else None)
  if attachment:weapon.set_editor_property(slot+'_socket',attachment['socket']);weapon.set_editor_property(slot+'_transform',transform(attachment['relative_transform']))
 save(weapon)
 rows.append(dict(Name=family['item_id'],ItemDefinitionId='BackpackItem:'+family['item_id'],DisplayName='Stander '+name,Icon=u.load_asset(family['icon']).get_path_name(),PickupActorClass='/Script/test_25d.EquipmentSoulPickup',PickupMode='Interact',bStackable=False,MaxStack=1,bEquippable=True,bRandomizedEquipment=False,AllowedEquipmentSlots=dict(GameplayTags=[dict(TagName='Equipment.Slot.Weapon')]),EquipEffect=fixed_ge.generated_class().get_path_name(),EquipEffectLevel=1,bUsable=False,UseEffect='None',WeaponSet=weapon.get_path_name()))
catalog=u.load_asset('/Game/BluePrints/Characters/Stander/DA_SwordAbilityInfo');assert catalog
shutil.copy2(ROOT/'Content/BluePrints/Characters/Stander/DA_SwordAbilityInfo.uasset',RUN/'DA_SwordAbilityInfo-before.uasset')
old=list(catalog.get_editor_property('ability_information'));identities={row.get_editor_property('ability_tag').export_text() for row in catalog_rows}
merged=[row for row in old if row.get_editor_property('ability_tag').export_text() not in identities]+catalog_rows
catalog.set_editor_property('ability_information',merged);assert u.EditorAssetLibrary.save_loaded_asset(catalog,False);merge_equipment(rows)
(RUN/'result.json').write_text(json.dumps({'saved':SAVED,'families':len(manifest['families']),'skills':len(catalog_rows),'complete':True},indent=2));print('Stander weapon assets authored')
