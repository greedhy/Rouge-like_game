"""Generate proposed authoring data from real inventory; no Unreal assets are touched."""
import json,re,math
from pathlib import Path
root=Path(__file__).resolve().parents[3];r=json.loads((root/'Saved/GAAuthoring/StanderWeapons/inventory.json').read_text());result={'version':1,'source':'live inventory 2026-11-04','visual_acceptance_pending':True,'families':[]}
icons=['T_DeepCut','T_StrokeOfAnger','T_FuryAttack','T_PiercingAttack','T_StealthAttack','T_CursedFury','T_RapidSpear_Attack','T_EnergyRecharge','T_Berserk','T_CursedCut','T_StormAttack']
for gi,g in enumerate(r['groups'][1:12]):
 family=g['name'][3:];prefix='Stander_'+family+'_';clips={c['name'][len(prefix):]:c for c in g['clips']};attachments=[x for x in r['demo'] if ('Stander_'+family+'_Idle') in x['parent'] and '/Models/Weapon' in x['mesh']];attachments.sort(key=lambda x:0 if 'hand_r' in x['socket'] else 1)
 f={'family':family,'source_group':g['name'],'item_id':'StanderWeapon'+family.replace('_',''),'icon':'/Game/KiroshoSkIconsPk0/Textures/'+icons[gi],'attachments':attachments,'locomotion':{k:clips[k]['path'] for k in ['Idle','Run','RunEnd','Jump','JumpFall','Jump_Original','Reload'] if k in clips},'actions':[],'helper_clips':[]}
 for name,c in clips.items():
  if name.endswith(('_Air','_Ground')) or name.startswith('Block_') or name in f['locomotion'] or name=='Attack':
   f['helper_clips'].append({'source':c['path'],'role':'alternate airborne/landing phase' if name.endswith(('_Air','_Ground')) else 'guard phase' if name.startswith('Block_') else 'locomotion/reload/reference'});continue
  attack=c['attack_candidate'];kind='Guard' if name=='Block' else 'Evade' if name in ['Dodge','DodgeRoll'] else ('Projectile' if family in ['Bow','Pistol','Magic','Staff'] else 'Melee') if attack else None
  if not kind:continue
  samples=c['samples'];events=[];evidence=[]
  if attack:
   bones=['hand_l'] if family in ['Bow','Spear','Staff','Rapier'] else ['hand_r','hand_l'] if family in ['DualBlades','Fighter'] else ['hand_r']
   # Identify strongest outbound joint motion before the long recovery half; retain measured frame index.
   for bone in bones:
    scores=[]
    for i in range(1,len(samples)):
     a,b=samples[i-1],samples[i]
     if not .06<=b['time']<=c['duration']*.65:continue
     dt=b['time']-a['time'];d=[(b[bone]['translation'][j]-b['root']['translation'][j])-(a[bone]['translation'][j]-a['root']['translation'][j]) for j in range(3)]
     scores.append((math.sqrt(sum(v*v for v in d))/dt,i))
    if scores:
     speed,i=max(scores);time=samples[i]['time'];evidence.append({'bone':bone,'sample_index':i,'time':time,'relative_speed':speed})
     if not events or all(abs(time-t)>.13 for t in events):events.append(time)
   events=sorted(events)
  flags=[]
  if 'JumpAttack' in name or name.endswith('_Air') or 'Skill8' in name:flags.append('airborne')
  if name=='DodgeAttack' or 'Skill6' in name:flags.append('displacement')
  if 'Skill7' in name:flags.append('radial')
  # Named source effects are requested designs, not evidence that an animation itself applies status.
  status='slow' if '_Slow' in name else 'stun' if '_Stun' in name else 'pull' if '_Pull' in name else None
  damage=(35 if family=='Hammer' else 24 if family in ['DualBlades','Fighter'] else 28) if name.startswith('Attack') else 45
  if len(events)>1:damage/=len(events)
  a={'id':family+'_'+name,'name':name,'source':c['path'],'duration':c['duration'],'kind':kind,'flags':flags,'events':events,'event_evidence':evidence,'events_status':'authored provisional phase from sampled curves; visual validation pending','helper_stages':{k:clips[k]['path'] for k in [name+'_Air',name+'_Ground'] if k in clips},'damage':damage if attack else 0,'radius':240 if family in ['Spear','Staff','DoubleBlades'] else 180,'angle':360 if 'radial' in flags else 110,'range':1400 if kind=='Projectile' else 0,'mana':0 if name.startswith('Attack') or kind=='Evade' else 15,'cooldown':0 if name.startswith('Attack') else 3 if kind in ['Guard','Evade'] else 5,'death_impulse':[500,0,1400] if '_Float' in name else [1400,0,250] if '_Push' in name else [1000,0,350],'proposed_status':status,'guard_stages':{k:clips[k]['path'] for k in clips if k.startswith('Block_')} if kind=='Guard' else {}}
  a['projectile_radius']={'Bow':12.,'Pistol':8.,'Magic':24.,'Staff':24.}.get(family,12.)
  f['actions'].append(a)
 result['families'].append(f)
(root/'Scripts/GA/StanderWeapons/weapon-manifest.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');print(sum(len(f['actions']) for f in result['families']),'actions')
