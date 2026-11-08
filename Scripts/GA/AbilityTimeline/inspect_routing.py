"""Read-only: record actual startup/weapon action routing and saved timeline tuning."""
import json
from pathlib import Path
import unreal as u

root = Path(u.Paths.convert_relative_path_to_full(u.Paths.project_dir()))
out = root / 'Saved/GAAuthoring/AbilityTimeline/recovery-investigation'
out.mkdir(parents=True, exist_ok=True)
def path(obj):
    return obj.get_path_name() if obj else None
def strike(s):
    m = s.get_editor_property('montage')
    return {'montage': path(m), 'length': m.get_play_length(), 'contact': s.get_editor_property('contact'), 'play_rate': s.get_editor_property('play_rate')}

player = u.get_default_object(u.EditorAssetLibrary.load_blueprint_class('/Game/BluePrints/Characters/Stander/BP_StanderPlayer'))
report = {'startup': [], 'player_config': path(player.get_editor_property('sword_config'))}
for cls in player.get_editor_property('startup_ability'):
    cdo = u.get_default_object(cls)
    report['startup'].append({'class': path(cls), 'input': cdo.get_editor_property('startup_input_tag').export_text()})
report['combo'] = [strike(s) for s in player.get_editor_property('sword_config').get_editor_property('combo')]
cfg = u.load_asset('/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/DA_Attack1')
montage = cfg.get_editor_property('montage')
timeline = cfg.get_editor_property('timeline')
report['weapon'] = {'config': path(cfg), 'montage': path(montage), 'length': montage.get_play_length(),
                    'timeline': path(timeline), 'phases': [p.export_text() for p in timeline.get_editor_property('phases')],
                    'validation': str(timeline.validate_for_montage(montage))}
weapon = u.load_asset('/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/DA_WeaponSet')
report['weapon_abilities'] = [entry.export_text() for entry in weapon.get_editor_property('abilities')]
(out / 'routing.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print('TIMELINE_ROUTING_REPORT ' + str(out / 'routing.json'))
