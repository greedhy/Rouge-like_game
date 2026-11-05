"""Offline-authored PIE lifecycle probe; execute only after setup_runtime.py and PIE.
No assets are saved. Stop manually with STANDER_WEAPON_LIFECYCLE.stop().
Each case owns its Slate callback; all exits restore the equipment snapshot.
"""
import json
import time
import traceback
import uuid
from pathlib import Path
import unreal as u


class LifecycleProbe:
    def __init__(self):
        self.root = Path(u.Paths.project_dir())
        self.world = u.EditorLevelLibrary.get_game_world()
        assert self.world and self.world.get_path_name().startswith('/Temp/'), 'Temporary PIE fixture required'
        self.player = u.GameplayStatics.get_player_pawn(self.world, 0)
        assert isinstance(self.player, u.StanderPlayer), 'Stander player required'
        self.state = self.player.get_editor_property('player_state')
        self.asc = self.state.get_component_by_class(u.MyAbilitySystemComponent)
        self.equipment = self.state.get_equipment_component()
        self.original = self.state.export_backpack_snapshot()
        self.position = self.player.get_actor_location()
        self.rotation = self.player.get_actor_rotation()
        self.targets = {name: next(a for a in u.GameplayStatics.get_all_actors_of_class(self.world, u.EnemyCharacter)
                                  if a.actor_has_tag(name)) for name in ('Target', 'Friend', 'Outside')}
        manifest = json.loads((self.root / 'Scripts/GA/StanderWeapons/weapon-manifest.json').read_text(encoding='utf-8'))
        self.families = {f['family']: f for f in manifest['families']}
        self.mass = next(s for s in u.ObjectIterator(u.PureMassSubsystem) if s.get_world() == self.world)
        self.mass.enable()
        self.handle = None
        self.entity_ids = set()
        self.projectiles = []
        self.saved = []
        self.closed = False
        self.case = None
        self.index = 0
        self.deadline = time.monotonic() + 100
        self.output = self.root / 'Saved/GAAuthoring/StanderWeapons/runtime-lifecycle.json'
        self.output.parent.mkdir(parents=True, exist_ok=True)
        self.report = {'run_id': uuid.uuid4().hex, 'status': 'running', 'expected': 3, 'cases': [], 'cleanup_errors': []}
        for actor in [self.player] + list(self.targets.values()):
            attributes = self.attributes(actor)
            for name in ('health', 'max_health', 'mana', 'max_mana', 'armor', 'block_chance', 'critical_hit_chance', 'physics_resistance', 'magic_resistance'):
                self.saved.append((attributes, name, attributes.get_editor_property(name).export_text()))
        self.target_positions = {name: actor.get_actor_location() for name, actor in self.targets.items()}

    def attributes(self, actor):
        component = self.asc if actor == self.player else actor.get_component_by_class(u.MyAbilitySystemComponent)
        return component.get_attribute_set(u.MyAttributeSet)

    def value(self, actor, name):
        return self.attributes(actor).get_editor_property(name).get_editor_property('current_value')

    def assign(self, actor, name, value):
        attributes = self.attributes(actor)
        field = attributes.get_editor_property(name)
        field.import_text('(BaseValue=%s,CurrentValue=%s)' % (value, value))
        attributes.set_editor_property(name, field)

    @staticmethod
    def tag(name):
        result = u.GameplayTag()
        result.import_text('(TagName="' + name + '")')
        return result

    def flush(self):
        self.output.write_text(json.dumps(self.report, ensure_ascii=False, indent=2), encoding='utf-8')

    def install(self, family):
        snapshot = u.BackpackSnapshot()
        snapshot.set_editor_property('capacity', 1)
        slot = u.BackpackSnapshotSlot()
        slot.import_text('(ItemDefinitionId="BackpackItem:%s",ItemInstanceId=%s,Quantity=1)' %
                         (self.families[family]['item_id'], u.GuidLibrary.new_guid().export_text()))
        snapshot.set_editor_property('slots', [slot])
        snapshot.set_editor_property('equipment', [])
        assert self.state.import_backpack_snapshot(snapshot), 'Snapshot import failed: ' + family
        self.equipment.request_equip(0, self.tag('Equipment.Slot.Weapon'))
        weapon = self.equipment.get_weapon_set()
        assert weapon and str(weapon.get_editor_property('family')) == family, 'Equip failed: ' + family

    def anim(self):
        return self.player.get_component_by_class(u.SkeletalMeshComponent).get_anim_instance()

    def prepare(self, family, action):
        self.install(family)
        self.asc.remove_active_gameplay_effect_by_source_effect(u.SwordTimedEffect, self.asc, -1)
        self.player.set_actor_location(u.Vector(0, 0, 98), False, True)
        self.player.get_component_by_class(u.CharacterMovementComponent).set_movement_mode(u.MovementMode.MOVE_WALKING)
        for actor in [self.player] + list(self.targets.values()):
            for name, amount in [('max_health', 100000), ('health', 100000), ('max_mana', 10000), ('mana', 10000),
                                 ('armor', 0), ('block_chance', 0), ('critical_hit_chance', 0), ('physics_resistance', 0), ('magic_resistance', 0)]:
                self.assign(actor, name, amount)
        for index, actor in enumerate(self.targets.values()):
            actor.set_actor_location(u.Vector(1200, 900 + index * 100, 98), False, True)
        path = '/Game/BluePrints/Characters/Stander/Weapons/%s/GA_%s.GA_%s_C' % (family, action, action)
        ability = u.load_class(None, path)
        assert ability, 'Missing ability ' + path
        config = u.get_default_object(ability).get_editor_property('action_config')
        assert config, 'Missing action config'
        return ability, config

    def projectiles_now(self):
        return [a for a in u.GameplayStatics.get_all_actors_of_class(self.world, u.StanderWeaponProjectile)
                if a.get_owner() == self.player]

    def clean_case(self):
        # Only this probe's projectiles/entities are removed.
        for projectile in self.projectiles:
            try:
                if u.SystemLibrary.is_valid(projectile):
                    projectile.destroy_actor()
            except Exception as exc:
                self.report['cleanup_errors'].append('projectile: ' + str(exc))
        self.projectiles.clear()
        for entity in list(self.entity_ids):
            try:
                self.mass.remove_monster(entity, u.PureMassRemovalReason.RECYCLED)
            except Exception as exc:
                self.report['cleanup_errors'].append('entity: ' + str(exc))
        self.entity_ids.clear()

    def start_case(self):
        if self.index == 3:
            self.stop(completed=True)
            return
        name = ['airborne_windup_cancel', 'bow_launched_then_hammer', 'mass_lethal_hips_velocity'][self.index]
        self.case = {'name': name, 'samples': []}
        self.safe_health = {}
        # Setup failures belong to this case and do not skip final restoration.
        try:
            if self.index == 0:
                ability, config = self.prepare('DoubleBlades', 'Skill8')
            elif self.index == 1:
                ability, config = self.prepare('Bow', 'Attack1')
            else:
                ability, config = self.prepare('Hammer', 'Attack1')
            events = config.get_editor_property('events')
            damage_events = [e for e in events if e.get_editor_property('kind') in
                             (u.StanderWeaponActionKind.MELEE, u.StanderWeaponActionKind.PROJECTILE)]
            assert damage_events, 'No damage event'
            self.contact = min(e.get_editor_property('time') for e in damage_events)
            self.first_event = damage_events[0]
            self.finish_time = config.get_editor_property('montage').get_play_length() / config.get_editor_property('play_rate') + 2.0
            self.case.update(start=u.GameplayStatics.get_time_seconds(self.world), contact=self.contact,
                             cancelled=False, launched=False, switched=False, peak_z=98.0)
            assert self.asc.try_activate_ability_by_class(ability), 'Activation rejected'
            forward = self.player.get_actor_forward_vector()
            self.forward = forward
            target = self.player.get_actor_location() + forward * (1100 if self.index == 1 else 100)
            target.z = 98
            if self.index < 2:
                self.targets['Target'].set_actor_location(target, False, True)
            else:
                template = u.load_asset('/Game/PureMass/DA_MassQAGoblin')
                assert template, 'Missing Mass fixture'
                self.monster_type = u.new_object(u.PureMassMonsterType)
                for key in ('mesh', 'animation_provider', 'mesh_offset', 'idle_animation', 'run_animation', 'attack_animation', 'radius', 'half_height'):
                    self.monster_type.set_editor_property(key, template.get_editor_property(key))
                for key, amount in dict(health=1., speed=0., attack_damage=0., armor=0., block_chance=0., attack_period=10.).items():
                    self.monster_type.set_editor_property(key, amount)
                entity = self.mass.spawn_monster(self.monster_type, target, 'None')
                assert entity > 0, 'Mass spawn failed'
                self.entity_ids.add(entity)
                self.case['entity'] = entity
                self.case['velocity_nonzero'] = False
                self.case['impulse_direction_seen'] = False
            self.case['hp_before'] = self.value(self.targets['Target'], 'health')
            self.safe_health = {name: self.value(self.targets[name], 'health') for name in ('Friend', 'Outside')}
            callback = [self.tick_cancel, self.tick_switch, self.tick_mass][self.index]
            self.handle = u.register_slate_post_tick_callback(lambda dt: self.guard_callback(callback, dt))
        except Exception as exc:
            self.finish_case(False, error=str(exc), traceback=traceback.format_exc())

    def guard_callback(self, callback, dt):
        try:
            if u.EditorLevelLibrary.get_game_world() != self.world:
                self.stop(reason='PIE stopped or world changed')
                return
            if time.monotonic() > self.deadline:
                raise RuntimeError('Lifecycle probe timeout')
            callback(dt)
        except Exception as exc:
            try:
                self.finish_case(False, error=str(exc), traceback=traceback.format_exc())
            except Exception as cleanup_exc:
                self.report['cleanup_errors'].append('case finalization: ' + str(cleanup_exc))
                self.stop(reason='Callback exception: ' + str(exc))

    def elapsed(self):
        return u.GameplayStatics.get_time_seconds(self.world) - self.case['start']

    def tick_cancel(self, dt):
        elapsed = self.elapsed()
        animation = self.anim()
        montage = animation.get_current_active_montage()
        position = animation.montage_get_position(montage) if montage else -1.0
        height = self.player.get_actor_location().z
        self.case['peak_z'] = max(self.case['peak_z'], height)
        if not self.case['cancelled'] and montage and (height > 105 or position >= self.contact - .04):
            self.case.update(cancelled=True, cancel_position=position, cancelled_before_contact=position < self.contact)
            animation.montage_stop(0.0)
        self.case['samples'].append({'t': elapsed, 'z': height, 'montage_position': position})
        if elapsed > self.finish_time:
            grounded = self.player.get_component_by_class(u.CharacterMovementComponent).get_editor_property('movement_mode') == u.MovementMode.MOVE_WALKING
            self.finish_case(self.case['cancelled'] and self.case.get('cancelled_before_contact', False)
                             and self.case['peak_z'] > 105 and grounded and height < 110
                             and self.value(self.targets['Target'], 'health') == self.case['hp_before'], grounded=grounded)

    def tick_switch(self, dt):
        elapsed = self.elapsed()
        if not self.case['launched']:
            flying = self.projectiles_now()
            if flying:
                self.projectiles.extend(flying)
                self.case.update(launched=True, projectile_count=len(flying), switch_time=elapsed)
                self.install('Hammer')
                self.case['switched'] = True
        self.case['samples'].append({'t': elapsed, 'projectiles': len(self.projectiles_now()), 'hp': self.value(self.targets['Target'], 'health')})
        if elapsed > max(self.finish_time, self.first_event.get_editor_property('projectile_lifetime') + 1.0):
            destroyed = all(not u.SystemLibrary.is_valid(a) for a in self.projectiles)
            self.finish_case(self.case['launched'] and self.case['switched'] and destroyed
                             and self.value(self.targets['Target'], 'health') == self.case['hp_before'], old_projectiles_destroyed=destroyed)

    def tick_mass(self, dt):
        elapsed = self.elapsed()
        view = next((v for v in self.mass.get_snapshots() if v.id == self.case['entity']), None)
        velocities = []
        for mesh in u.ObjectIterator(u.SkeletalMeshComponent):
            if mesh.get_world() == self.world and isinstance(mesh.get_owner(), u.PureMassCrowd) and mesh.is_simulating_physics():
                if not mesh.does_socket_exist('Hips'):
                    continue
                velocity = mesh.get_physics_linear_velocity('Hips')
                velocities.append([velocity.x, velocity.y, velocity.z])
                self.case['velocity_nonzero'] |= velocity.length() > 1.0
                # Gravity alone must not be reported as evidence of a death impulse.
                self.case['impulse_direction_seen'] |= velocity.x * self.forward.x + velocity.y * self.forward.y > 10 or velocity.z > 20
        self.case['samples'].append({'t': elapsed, 'entity_alive': view is not None, 'hips_velocities': velocities})
        if elapsed > self.finish_time:
            self.finish_case(view is None and self.case['velocity_nonzero'] and self.case['impulse_direction_seen'], killed=view is None)

    def finish_case(self, passed, **details):
        if self.handle is not None:
            u.unregister_slate_post_tick_callback(self.handle)
            self.handle = None
        if self.case is not None:
            try:
                safe = all(self.value(self.targets[name], 'health') == hp for name, hp in getattr(self, 'safe_health', {}).items())
            except Exception as exc:
                safe = False
                details['safety_check_error'] = str(exc)
            self.case.update(details, passed=bool(passed and safe), friend_outside_unchanged=safe)
            self.report['cases'].append(self.case)
            self.case = None
        self.clean_case()
        self.flush()
        self.index += 1
        self.start_case()

    def stop(self, reason='Stopped manually', completed=False):
        if self.closed:
            return
        self.closed = True
        if self.handle is not None:
            u.unregister_slate_post_tick_callback(self.handle)
            self.handle = None
        try:
            self.anim().montage_stop(0.0)
        except Exception as exc:
            self.report['cleanup_errors'].append('cancel: ' + str(exc))
        self.clean_case()
        try:
            assert self.state.import_backpack_snapshot(self.original), 'Original snapshot restore rejected'
            self.report['snapshot_restored'] = True
        except Exception as exc:
            self.report['snapshot_restored'] = False
            self.report['cleanup_errors'].append('snapshot: ' + str(exc))
        for attributes, name, text in self.saved:
            try:
                field = attributes.get_editor_property(name)
                field.import_text(text)
                attributes.set_editor_property(name, field)
            except Exception as exc:
                self.report['cleanup_errors'].append('attribute: ' + str(exc))
        try:
            self.player.set_actor_location(self.position, False, True)
            self.player.set_actor_rotation(self.rotation, False)
            self.player.get_component_by_class(u.CharacterMovementComponent).set_movement_mode(u.MovementMode.MOVE_WALKING)
            for name, position in self.target_positions.items():
                self.targets[name].set_actor_location(position, False, True)
        except Exception as exc:
            self.report['cleanup_errors'].append('transform: ' + str(exc))
        self.report.update(status='complete' if completed else 'stopped', reason=None if completed else reason,
                           passed=completed and len(self.report['cases']) == 3 and all(c['passed'] for c in self.report['cases'])
                           and self.report.get('snapshot_restored', False) and not self.report['cleanup_errors'])
        self.flush()


if 'STANDER_WEAPON_LIFECYCLE' in globals():
    STANDER_WEAPON_LIFECYCLE.stop(reason='Replaced by new lifecycle probe')
STANDER_WEAPON_LIFECYCLE = LifecycleProbe()
STANDER_WEAPON_LIFECYCLE.flush()
STANDER_WEAPON_LIFECYCLE.start_case()
