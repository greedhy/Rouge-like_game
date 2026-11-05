import unreal as u,json
from pathlib import Path
p=u.load_asset('/Game/RoguelikeGeneration/Gameplay/DA_DungeonGameplay')
t=p.monster_type
c=u.get_default_object(u.PureMassCrowd)
r={'profile':p.get_path_name(),'enabled':p.get_editor_property('use_mass_melee'),
   'count':p.mass_per_combat_room,'type':t.get_path_name(),'health':t.health,
   'windup':t.attack_windup_seconds,'roster':[str(x.id) for x in p.normal_roster],
   'net_frequency':c.get_editor_property('net_update_frequency'),
   'min_net_frequency':c.get_editor_property('min_net_update_frequency')}
r['passed']=r['enabled'] and r['count']==50 and r['health']==10 and 'GoblinSpear' not in r['roster'] and r['net_frequency']==60 and r['min_net_frequency']==60
Path(u.Paths.project_dir(),'Saved/GAAuthoring/DungeonHybrid/config-final.json').write_text(json.dumps(r,indent=2))
assert r['passed'],r
