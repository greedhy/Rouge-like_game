import unreal as u,json
from pathlib import Path
r='/Game/BluePrints/Characters/SwordShield/'
cfg=u.load_asset(r+'DA_SwordShield');player=u.get_default_object(u.load_class(None,r+'BP_SwordShieldPlayer.BP_SwordShieldPlayer_C'))
rows=u.load_asset(r+'DA_SwordAbilityInfo').get_editor_property('ability_information')
report={'config':cfg.get_path_name(),'contact':[s.contact for s in cfg.combo],'sword_rotation':str(player.sword_visual.get_relative_transform().rotation),'abilities':[x.get_path_name() for x in player.get_editor_property('startup_ability')],'catalog':[x.export_text() for x in rows]}
report['passed']=len(rows)==3 and len(report['abilities'])==3 and all('Abilities.SwordShield.' in x for x in report['catalog']) and len({x.icon.get_path_name() for x in rows})==3
(Path(u.Paths.project_dir())/'Saved/GAAuthoring/SwordShield/final-assets.json').write_text(json.dumps(report,indent=2))
assert report['passed']
