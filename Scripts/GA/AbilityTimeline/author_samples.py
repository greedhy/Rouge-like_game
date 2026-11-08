"""Create three opt-in timeline samples after a full native build. Preserve existing tuning."""
import json
import shutil
import uuid
from pathlib import Path
import unreal as u

root = Path(u.Paths.project_dir())
run = root / 'Saved/GAAuthoring/AbilityTimeline' / uuid.uuid4().hex
run.mkdir(parents=True)
assert not u.EditorLevelLibrary.get_game_world(), 'End PIE before authoring'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Save existing assets first'
base = '/Game/BluePrints/Characters/Stander/Weapons'
folder = '/Game/AbilityTimelines/Stander'
tools = u.AssetToolsHelpers.get_asset_tools()
report = []

for family, action in [('Sword_Shield', 'Attack1'), ('Sword_Shield', 'Block'), ('Magic', 'Attack1')]:
    cfg_path = f'{base}/{family}/DA_{action}'
    cfg = u.load_asset(cfg_path)
    assert isinstance(cfg, u.StanderWeaponActionConfig), cfg_path
    montage = cfg.get_editor_property('montage')
    length = montage.get_play_length()
    rate = cfg.get_editor_property('play_rate')
    events = cfg.get_editor_property('events')
    first = min(e.get_editor_property('time') for e in events)
    last = max(e.get_editor_property('time') + (e.get_editor_property('duration')
               if e.get_editor_property('kind') in [u.StanderWeaponActionKind.GUARD, u.StanderWeaponActionKind.MOVE]
               else 0.05) for e in events)
    # Guard may start at zero: omit an empty windup instead of manufacturing a new contact frame.
    ends = []
    if first > 0.001:
        ends.append(('Windup', first))
    last = min(last, length)
    if last > first + 0.001:
        ends.append(('Release', last))
    if not ends or ends[-1][1] < length - 0.001:
        ends.append(('Recovery', length))
    else:
        ends[-1] = (ends[-1][0], length)
    name = f'{family}_{action}'
    path = f'{folder}/DA_{name}'
    timeline = u.load_asset(path) if u.EditorAssetLibrary.does_asset_exist(path) else None
    old = cfg.get_editor_property('timeline')
    assert old is None or old.get_path_name().split('.')[0] == path, 'Different timeline already assigned'
    if timeline is None:
        factory = u.DataAssetFactory()
        factory.set_editor_property('data_asset_class', u.AbilityTimelineDefinition)
        timeline = tools.create_asset('DA_' + name, folder, u.AbilityTimelineDefinition, factory)
        assert timeline
        phases = []
        start = 0.0
        for phase_name, end in ends:
            phase = u.AbilityTimelinePhase()
            phase.set_editor_property('name', phase_name)
            phase.set_editor_property('source_end', end)
            phase.set_editor_property('duration', (end - start) / rate)
            phases.append(phase)
            start = end
        timeline.set_editor_property('phases', phases)
        assert u.EditorAssetLibrary.save_loaded_asset(timeline, False)
    assert isinstance(timeline, u.AbilityTimelineDefinition)
    disk = root / ('Content' + cfg_path.removeprefix('/Game') + '.uasset')
    shutil.copy2(disk, run / f'{name}-before.uasset')
    cfg.set_editor_property('timeline', timeline)
    assert u.EditorAssetLibrary.save_loaded_asset(cfg, False)
    assert cfg.get_editor_property('timeline') == timeline
    report.append({'config': cfg_path, 'timeline': path, 'montage': montage.get_path_name(),
                   'source_length': length, 'original_rate': rate})

(run / 'assets.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print('ABILITY_TIMELINE_AUTHORED ' + str(run))
