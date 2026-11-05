import unreal as u,json
from pathlib import Path
cat=u.load_asset('/Game/BluePrints/Characters/Stander/DA_SwordAbilityInfo');rows=cat.get_editor_property('ability_information');cfg=u.load_asset('/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/DA_Attack1')
r={'tag':cfg.get_editor_property('action_tag').export_text(),'rows':[{'tag':x.get_editor_property('ability_tag').export_text(),'icon':str(x.get_editor_property('icon'))} for x in rows]}
(Path(u.Paths.project_dir())/'Saved/GAAuthoring/StanderWeapons/catalog-diagnostic.json').write_text(json.dumps(r,indent=2))
