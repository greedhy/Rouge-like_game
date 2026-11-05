"""Create missing novice equipment and specialist suffixes; preserve all existing rows."""
import json
from pathlib import Path
import unreal as u
BASE='/Game/Equipment'
ITEMS=[
 ('NoviceIronSword','铁卫长剑','33616_1','Weapon',[('Strength',2,4),('ArmorPene',1,3)]),
 ('NoviceBattleAxe','裂甲战斧','61669_1','Weapon',[('Strength',3,5),('CriticalHitDamage',2,4)]),
 ('NoviceArcaneStaff','学徒法杖','20152_1','Weapon',[('Intelligence',2,4),('MaxMana',10,20)]),
 ('NoviceIronMail','铁卫锁甲','64755_1','Chest',[('Armor',5,8),('MaxHealth',15,25)]),
 ('NoviceScholarRobe','学者法袍','60684_1','Chest',[('Intelligence',1,3),('MagicResistance',2,4)]),
 ('NoviceIronBoots','铁卫长靴','51613_1','Boots',[('Armor',2,4),('Resilience',1,2)]),
 ('NoviceGuardHelm','守卫头盔','54873_1','Helmet',[('BlockChance',1,3),('Armor',2,4)]),
 ('NoviceMageHat','学徒尖帽','24393_1','Helmet',[('Intelligence',1,3),('ManaRegeneration',0.2,0.4)]),
 ('NovicePrismStone','棱光魔法石','86021_1','MagicStone',[('MaxMana',10,20),('PureResistance',1,3)]),
 ('NoviceJadeRing','翡翠戒指','25186_1','Ring.Left',[('Vigor',1,3),('HealthRegeneration',0.2,0.4)])]
SUFFIXES=[('Strength','蛮力'),('Intelligence','睿智'),('Resilience','坚韧'),('Vigor','生机'),('Armor','铁壁'),('ArmorPene','破甲'),('BlockChance','格挡'),('CriticalHitChance','精准'),('CriticalHitDamage','毁伤'),('CriticalHitResistence','镇定'),('HealthRegeneration','复苏'),('ManaRegeneration','冥想'),('MaxHealth','长生'),('MaxMana','奥能'),('FireResistance','避火'),('ThunderResistance','绝雷'),('WaterResistance','御水'),('MagicResistance','禁魔'),('PhysicsResistance','坚壳'),('PureResistance','棱光')]
def stat(n,a,b):
 return dict(StatTag=dict(TagName='Equipment.Stat.'+n),MinValue=dict(Value=a),MaxValue=dict(Value=b),DecimalPlaces=1 if n.endswith('Regeneration') else 0)
def merge(path, additions):
 t=u.load_asset(path);assert t,path
 rows=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(t));existing={r['Name'] for r in rows}
 old=json.dumps(rows,sort_keys=True,ensure_ascii=False)
 for r in rows:
  identity=r.get('ItemDefinitionId')
  if isinstance(identity,dict):
   typ=identity.get('PrimaryAssetType',{});typ=typ.get('Name','BackpackItem') if isinstance(typ,dict) else typ
   assert typ=='BackpackItem';r['ItemDefinitionId']='BackpackItem:'+identity['PrimaryAssetName']
 additions=[r for r in additions if r['Name'] not in existing]
 if additions:
  assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(t,json.dumps(rows+additions,ensure_ascii=False))
  assert u.EditorAssetLibrary.save_loaded_asset(t,False)
 return len(additions)
profiles=[];definitions=[]
for name,label,icon,slot,stats in ITEMS:
 assert isinstance(u.load_asset('/Game/Assets/EquipmentIcon/'+icon),u.Texture2D)
 profiles.append(dict(Name=name,DefaultItemLevel=1,BaseStats=[stat(*s) for s in stats],RarityIds=['Common','Magic','Rare']))
 slots=['Equipment.Slot.'+slot]
 if slot=='Ring.Left':slots.append('Equipment.Slot.Ring.Right')
 definitions.append(dict(Name=name,ItemDefinitionId='BackpackItem:'+name,DisplayName=label,Icon=f'/Game/Assets/EquipmentIcon/{icon}.{icon}',PickupActorClass='/Script/test_25d.EquipmentSoulPickup',PickupMode='Interact',bStackable=False,MaxStack=1,bEquippable=True,bRandomizedEquipment=True,GenerationProfile=name,AllowedEquipmentSlots=dict(GameplayTags=[dict(TagName=s) for s in slots]),EquipEffect='/Game/Equipment/Effects/GE_Equipment_Random.GE_Equipment_Random_C',EquipEffectLevel=1,bUsable=False,UseEffect='None'))
affixes=[]
for attribute,label in SUFFIXES:
 preferred=[]
 for name,_,_,slot,stats in ITEMS:
  own={s[0] for s in stats}
  defensive=slot in ('Chest','Boots','Helmet')
  if attribute in own or slot in ('Ring.Left','MagicStone') or (attribute.endswith('Resistance') and defensive) or (attribute.startswith('Critical') and slot=='Weapon'):
   preferred.append(name)
 low,high=(15,30) if attribute in ('MaxHealth','MaxMana') else (0.3,0.6) if attribute.endswith('Regeneration') else (2,4)
 affixes.append(dict(Name='Specialist'+attribute,DisplayName='之'+label,bPrefix=False,ExclusiveGroup=attribute,Weight=3,Profiles=preferred,Tiers=[dict(Tier=t,MinItemLevel=lo,MaxItemLevel=hi,Stats=[stat(attribute,low*t,high*t)]) for t,lo,hi in [(1,1,9),(2,10,20)]]))
result={}
result['profiles_added']=merge(BASE+'/DT_EquipmentGenerationProfiles',profiles)
result['suffixes_added']=merge(BASE+'/DT_EquipmentAffixes',affixes)
result['items_added']=merge(BASE+'/DT_Equipment',definitions)
result['items']=ITEMS;result['suffixes']=SUFFIXES
out=Path(u.Paths.project_saved_dir())/'Equipment'/'novice_equipment_authored.json';out.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
u.log('NOVICE_EQUIPMENT_SUCCESS '+str(result))
