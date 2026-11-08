"""Measure the resolved LMB abilities in PIE -> montage -> movement unlock; only transient tuning changes."""
import json, time, traceback
from pathlib import Path
import unreal as u
root = Path(u.Paths.convert_relative_path_to_full(u.Paths.project_dir()))
out = root / 'Saved/GAAuthoring/AbilityTimeline/recovery-investigation'
world = u.EditorLevelLibrary.get_game_world()
assert world and world.get_path_name().startswith('/Temp/'), 'Only run in the owned temporary fixture'
pawn = u.GameplayStatics.get_player_pawn(world, 0)
assert isinstance(pawn, u.StanderPlayer)
state = pawn.get_editor_property('player_state')
asc = state.get_component_by_class(u.MyAbilitySystemComponent)
equipment = state.get_equipment_component()
movement = pawn.get_component_by_class(u.CharacterMovementComponent)
mesh = pawn.get_component_by_class(u.SkeletalMeshComponent)
original_inventory = state.export_backpack_snapshot()
original_configs = []
performance = u.get_default_object(u.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
original_throttle = performance.get_editor_property('bThrottleCPUWhenNotForeground')
performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
report = {'status': 'running', 'cases': [], 'passed': False}
current = None
case_index = 0
started = time.monotonic()
last_finished = -10.
def tag(name):
    t = u.GameplayTag(); t.import_text('(TagName="' + name + '")'); return t
def flush():
    (out / 'pie-recovery.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
def install():
    snapshot = u.BackpackSnapshot()
    snapshot.set_editor_property('capacity', 1)
    slot = u.BackpackSnapshotSlot()
    slot.import_text('(ItemDefinitionId="BackpackItem:StanderWeaponSwordShield",ItemInstanceId=' + u.GuidLibrary.new_guid().export_text() + ',Quantity=1)')
    snapshot.set_editor_property('slots', [slot]); snapshot.set_editor_property('equipment', [])
    assert state.import_backpack_snapshot(snapshot)
    equipment.request_equip(0, tag('Equipment.Slot.Weapon'))
    assert equipment.get_weapon_set()
def tune(duration):
    cls = u.load_class(None, '/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/GA_Attack1.GA_Attack1_C')
    instances = [x for x in u.ObjectIterator(cls) if x.get_world() == world and not x.get_name().startswith('Default__')]
    assert len(instances) == 1, [x.get_path_name() for x in instances]
    ability = instances[0]
    source = ability.get_editor_property('action_config')
    original_configs.append((source, source.get_editor_property('timeline')))
    timeline = u.new_object(u.AbilityTimelineDefinition)
    timeline.set_editor_property('phases', source.get_editor_property('timeline').get_editor_property('phases'))
    phases = timeline.get_editor_property('phases')
    found = False
    for index, phase in enumerate(phases):
        if str(phase.get_editor_property('name')).lower() == 'recovery':
            phase.set_editor_property('duration', duration)
            phases[index] = phase
            found = True
    assert found, [x.export_text() for x in phases]
    timeline.set_editor_property('phases', phases)
    # ActionConfig is EditDefaultsOnly. Temporarily replace only its timeline reference in memory;
    # restore the exact original object after the probe and never save the asset.
    source.set_editor_property('timeline', timeline)
    return sum(x.get_editor_property('duration') for x in phases)
def finish():
    u.unregister_slate_post_tick_callback(handle)
    try:
        for cfg, timeline in reversed(original_configs):
            cfg.set_editor_property('timeline', timeline)
        state.import_backpack_snapshot(original_inventory)
    finally:
        performance.set_editor_property('bThrottleCPUWhenNotForeground', original_throttle)
        flush()
def tick(delta):
    global current, case_index, last_finished
    try:
        now = u.GameplayStatics.get_time_seconds(world)
        if time.monotonic() - started > 90: raise RuntimeError('Probe timeout')
        if current is None:
            if now - last_finished < 1.3: return
            expected = None
            if case_index == 1: install(); expected = tune(.8333333)
            elif case_index == 2: expected = tune(.1)
            current = {'name': ['spawn_default','equipped_recovery_0.8333','equipped_recovery_0.1'][case_index],
                       'requested_at': now, 'expected': expected, 'maximum_rate': 0.}
            ability_path = ('/Game/BluePrints/Characters/Stander/GA_Stander_Combo.GA_Stander_Combo_C'
                if case_index == 0 else '/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield/GA_Attack1.GA_Attack1_C')
            current['activation_path'] = ability_path
            current['activation'] = 'ASC.TryActivateAbilityByClass (resolved LMB class)'
            assert asc.try_activate_ability_by_class(u.load_class(None, ability_path)), 'Could not activate resolved LMB ability'
        anim = mesh.get_anim_instance()
        montage = anim.get_current_active_montage()
        if 'lock_start' not in current:
            if movement.get_editor_property('movement_mode') == u.MovementMode.MOVE_NONE:
                current['lock_start'] = now
                current['montage'] = montage.get_path_name() if montage else None
            else:
                if now - current['requested_at'] > 3:
                    current['movement_mode'] = str(movement.get_editor_property('movement_mode'))
                    current['location'] = str(pawn.get_actor_location())
                    current['abilities'] = [x.get_path_name() for x in u.ObjectIterator(u.StanderAbility) if x.get_world() == world]
                    raise RuntimeError('LMB failed to start: ' + str(current))
        else:
            if montage: current['maximum_rate'] = max(current['maximum_rate'], anim.montage_get_play_rate(montage))
            if movement.get_editor_property('movement_mode') != u.MovementMode.MOVE_NONE:
                current['locked_seconds'] = now - current['lock_start']
                report['cases'].append(current); case_index += 1; current = None; last_finished = now; flush()
                if case_index == 3:
                    a,b,c = report['cases']
                    report['passed'] = ('AM_Stander_Combo1' in a['montage'] and 'AM_Attack1' in b['montage'] and
                        abs(b['locked_seconds'] - b['expected']) < .1 and abs(c['locked_seconds'] - c['expected']) < .1 and
                        c['locked_seconds'] < b['locked_seconds'] - .5 and c['maximum_rate'] > 5.)
                    report['status'] = 'complete'; finish()
    except Exception:
        report.update(status='error', error=traceback.format_exc()); finish()
handle = u.register_slate_post_tick_callback(tick)
flush()
