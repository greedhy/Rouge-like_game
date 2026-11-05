"""Deterministic semantic icons; equipment keeps the family icon."""
from pathlib import Path
import json
root=Path(__file__).resolve().parents[3];path=root/'Scripts/GA/StanderWeapons/weapon-manifest.json';manifest=json.loads(path.read_text(encoding='utf-8'))
base='/Game/KiroshoSkIconsPk0/Textures/';guard='/Game/Skill_Icons/Textures/512x512/T_Skill_Icons__23';pool=['T_DeepCut','T_PiercingAttack','T_FuryAttack','T_StormAttack','T_CursedCut','T_RapidSpear_Attack','T_BloodyGrip','T_StealthAttack','T_VindictiveFury','T_UltimatePower','T_StrokeOfAnger','T_Berserk','T_Evade']
for family in manifest['families']:
 defaults=['Attack1','Block']+[next(a['name'] for a in family['actions'] if a['name'].startswith('Skill'+str(n))) for n in [7,6,1,4]]
 def preferred(a):
  name=a['name']
  if name=='Attack1':return family['icon']
  if a['kind']=='Guard':return guard
  if a['kind']=='Evade':return base+'T_Evade'
  if name.startswith('Skill'):
   i=int(name[5]);return base+{1:'T_FuryAttack',2:'T_CursedCut',3:'T_StrokeOfAnger',4:'T_BloodyGrip',5:'T_VindictiveFury',6:'T_PiercingAttack',7:'T_StormAttack',8:'T_UltimatePower'}[i]
  return base+('T_StealthAttack' if name=='DodgeAttack' else 'T_RapidSpear_Attack' if name.startswith('Jump') else 'T_CursedCut' if name.endswith('2') else 'T_StrokeOfAnger')
 used=set()
 for name in defaults:
  a=next(a for a in family['actions'] if a['name']==name);icon=preferred(a)
  if icon in used:icon=next(base+n for n in pool if base+n not in used)
  a['icon']=icon;used.add(icon)
 for a in family['actions']:
  if a['name'] not in defaults:a['icon']=preferred(a)
path.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
