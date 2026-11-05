"""Owned Stander showcase assets; source content remains intact."""
import unreal as u,json,uuid,shutil
from pathlib import Path
assert not u.EditorLevelLibrary.get_game_world(),'Stop PIE before asset writes'
root=Path(u.Paths.project_dir()).resolve();folder=root/'Saved/GAAuthoring/StanderMass';folder.mkdir(parents=True,exist_ok=True);backup=folder/('author-'+uuid.uuid4().hex);backup.mkdir(parents=True)
R='/Game/BluePrints/Characters/Stander';owner='StanderShowcaseV1';tools=u.AssetToolsHelpers.get_asset_tools()
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(),'Unsaved map'
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

# Contact values must come from the current source-pose inspection, not clip names.
selection=json.loads((folder/'animation-selection.json').read_text())
cfg=existing('DA_Stander');assert cfg
new_classes=[]
for name,cls,key,damage,radius,angle,mana,cooldown in [('Lift',u.StanderLiftAbility,'3',45.,180.,100.,18.,6.),('ShieldBash',u.StanderShieldBashAbility,'4',35.,150.,90.,10.,4.)]:
 choice=selection[name];source=choice['source'];contact=float(choice['contact'])
 clip=duplicate(source,'Animations/AS_Stander_'+name);assert clip
 assert 0<=contact<clip.get_play_length()
 for k,v in dict(enable_root_motion=False,force_root_lock=True,root_motion_root_lock=u.RootMotionRootLock.ZERO).items():clip.set_editor_property(k,v)
 for e in u.AnimationLibrary.get_animation_notify_events(clip):u.AnimationLibrary.remove_animation_notify_events_by_name(clip,e.get_editor_property('notify_name'))
 save(clip);montage=existing('Animations/AM_Stander_'+name)
 if not montage:
  f=u.AnimMontageFactory();f.set_editor_property('target_skeleton',clip.get_editor_property('skeleton'));f.set_editor_property('source_animation',clip)
  montage=tools.create_asset('AM_Stander_'+name,R+'/Animations',u.AnimMontage,f)
 for field in ['blend_in','blend_out']:
  blend=montage.get_editor_property(field);blend.set_editor_property('blend_time',.06);montage.set_editor_property(field,blend)
 save(montage);strike=u.SwordStrike()
 for k,v in dict(montage=montage,damage=damage,radius=radius,angle=angle,contact=contact,play_rate=1.).items():strike.set_editor_property(k,v)
 field='shield_bash' if name=='ShieldBash' else 'lift';cfg.set_editor_property(field,strike);cfg.set_editor_property(field+'_mana',mana);cfg.set_editor_property(field+'_cooldown',cooldown)
 a,cdo=bp('GA_Stander_'+name,cls.static_class())
 for k,v in dict(startup_input_tag=tag('InputTag.'+key),damage_type=tag('Damage.Physics'),damage_effect_class=u.load_class(None,'/Game/BluePrints/AbilitySystem/Player/Effect/GE_Damage.GE_Damage_C')).items():cdo.set_editor_property(k,v)
 compile_save(a);new_classes.append(a.generated_class())
# Only extension fields are changed; existing strike timing and damage remain intact.
for k,v in dict(combo_death_impulses=[u.Vector(900,0,250),u.Vector(1000,0,300),u.Vector(1200,0,450)],counter_death_impulse=u.Vector(1100,0,300),spin_death_impulse=u.Vector(1000,0,350),dash_death_impulse=u.Vector(1400,0,250),lift_death_impulse=u.Vector(450,0,1600),shield_bash_death_impulse=u.Vector(1600,0,200)).items():cfg.set_editor_property(k,v)
save(cfg)
player=existing('BP_StanderPlayer');pcdo=u.get_default_object(player.generated_class());startup=list(pcdo.get_editor_property('startup_ability'))
for cls in new_classes:
 if cls not in startup:startup.append(cls)
pcdo.set_editor_property('startup_ability',startup);compile_save(player)
catalog=existing('DA_SwordAbilityInfo');rows=list(catalog.get_editor_property('ability_information'));template=rows[0]
icons=['/Game/KiroshoSkIconsPk0/Textures/T_DeepCut','/Game/Skill_Icons/Textures/512x512/T_Skill_Icons__23','/Game/KiroshoSkIconsPk0/Textures/T_StormAttack','/Game/KiroshoSkIconsPk0/Textures/T_PiercingAttack','/Game/KiroshoSkIconsPk0/Textures/T_FuryAttack','/Game/KiroshoSkIconsPk0/Textures/T_StrokeOfAnger']
for name,icon in zip(['Combo','Guard','Spin','Dash','Lift','ShieldBash'],icons):
 identity='Abilities.Stander.'+name
 indices=[i for i,r in enumerate(rows) if identity in r.get_editor_property('ability_tag').export_text()];assert len(indices)<=1
 row=rows[indices[0]] if indices else u.MyAbilityInfo()
 ga=u.EditorAssetLibrary.load_blueprint_class(R+'/GA_Stander_'+name);texture=u.load_asset(icon);assert ga and texture
 fields=['AbilityTag=(TagName="'+identity+'")','AbilityType=(TagName="Abilities.Type.Offensive")','LevelRequirement=1','Ability="'+ga.get_path_name()+'"','Icon="'+texture.get_path_name()+'"']
 if name!='Combo':fields.append('CooldownTag=(TagName="Cooldown.Stander.'+name+'")')
 background=template.get_editor_property('background_material')
 if background:fields.append('BackgroundMaterial="'+background.get_path_name()+'"')
 row.import_text('('+','.join(fields)+')')
 if indices:rows[indices[0]]=row
 else:rows.append(row)
catalog.set_editor_property('ability_information',rows);save(catalog)
mode=existing('BP_StanderGameMode');mcdo=u.get_default_object(mode.generated_class());original_hud=mcdo.get_editor_property('hud_class')
hud=duplicate(original_hud.get_path_name().split('.')[0],'BP_StanderHUD');hcdo=u.get_default_object(hud.generated_class())
for field,name in [('overlay_widget_controller_class','BP_StanderOverlayController'),('spell_menu_widget_controller_class','BP_StanderSpellController')]:
 source=hcdo.get_editor_property(field);assert source
 controller=duplicate(source.get_path_name().split('.')[0],name);u.get_default_object(controller.generated_class()).set_editor_property('ability_info',catalog);compile_save(controller);hcdo.set_editor_property(field,controller.generated_class())
compile_save(hud);mcdo.set_editor_property('hud_class',hud.generated_class());mcdo.set_editor_property('ability_info',catalog);compile_save(mode)
(folder/'author-result.json').write_text(json.dumps({'saved':saved,'backup':str(backup),'complete':True},indent=2));print('StanderMass asset authoring complete')
