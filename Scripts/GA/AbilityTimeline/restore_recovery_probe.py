"""Restore the prior editor map without saving or discarding dirty content assets."""
import json
from pathlib import Path
import unreal as u
assert not u.EditorLevelLibrary.get_game_world()
root = Path(u.Paths.convert_relative_path_to_full(u.Paths.project_dir()))
out = root / 'Saved/GAAuthoring/AbilityTimeline/recovery-investigation'
saved = json.loads((out / 'restore.json').read_text())
probe_config_path = '/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/DA_Attack1'
if probe_config_path not in saved['dirty']:
    # This package was clean at entry. Reload only our restored in-memory reference edit;
    # never reload the user's dirty montage or any originally dirty configuration.
    cfg = u.load_asset(probe_config_path)
    assert cfg.get_editor_property('timeline').get_path_name().split('.')[0] == '/Game/AbilityTimelines/Stander/DA_Sword_Shield_Attack1'
    changed, error = u.EditorLoadingAndSavingUtils.reload_packages([cfg.get_outermost()], u.ReloadPackagesInteractionMode.ASSUME_POSITIVE)
    assert changed, str(error)
u.get_default_object(u.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')).set_editor_property(
    'bThrottleCPUWhenNotForeground', saved.get('throttle', True))
assert u.EditorLoadingAndSavingUtils.load_map(saved['map'])
u.EditorLevelLibrary.set_level_viewport_camera_info(u.Vector(*saved['location']),
    u.Rotator(pitch=saved['rotation'][0], yaw=saved['rotation'][1], roll=saved['rotation'][2]))
report = {'map': u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name(),
          'dirty': [x.get_path_name() for x in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}
(out / 'restored.json').write_text(json.dumps(report, ensure_ascii=False, indent=2))
assert set(saved['dirty']).issubset(set(report['dirty'])), 'An existing dirty asset was unexpectedly cleared'
