"""After the graph workshop scenario in Listen PIE, verify host removal cleans its preview.

Uses the real attribute/tab/Begin controls on the remote native tick. The only
UI action is removing the owning character menu from the viewport on the native tick, as
normal HUD teardown does. The empty QA map's Unselected profession is temporarily
set to Mage through the existing authority-only fixture, then restored.
"""
import json
import time
from pathlib import Path
import unreal as u

root = Path(u.Paths.project_saved_dir()).resolve() / 'GAAuthoring/WorkshopFourthPage/20261121'
result = {'phase': 'profession', 'checks': {}, 'finished': False, 'passed': False}
started = time.monotonic()
locals_ = [p for p in u.ObjectIterator(u.MyPlayerController)
           if u.SystemLibrary.is_valid(p) and 'UEDPIE_' in p.get_path_name()
           and p.is_local_controller() and p.get_local_role() == u.NetRole.ROLE_AUTONOMOUS_PROXY]
assert len(locals_) == 1, 'Requires exactly one remote client'
pc = locals_[0]
authority = next(p for p in u.ObjectIterator(u.MyPlayerController)
                 if u.SystemLibrary.is_valid(p) and 'UEDPIE_' in p.get_path_name()
                 and not p.is_local_controller() and p.get_local_role() == u.NetRole.ROLE_AUTHORITY)


def component(player, cls):
    return player.get_editor_property('player_state').get_component_by_class(cls)


def specs(player):
    return sorted(h.export_text() for h in component(player, u.MyAbilitySystemComponent).get_all_abilities())


def preview(player):
    return component(player, u.AbilityRuntimeRecipeDeveloperComponent).get_developer_state()


def command(action):
    u.SystemLibrary.execute_console_command(pc.get_world(), 'RuntimeRecipe.Probe ' + action, pc)


def write():
    (root / 'remote-host-destruction.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')


def finish(error=None):
    result.update(finished=True, passed=not error, error=error)
    write()
    u.unregister_slate_post_tick_callback(handle)


before = {p.get_path_name(): specs(p) for p in (pc, authority)}
assert not preview(pc).get_editor_property('preview_active'), 'Run after the full graph test cleanup'
original_profession = authority.get_editor_property('player_state').get_editor_property('profession')
assert original_profession in (u.PlayerProfession.UNSELECTED, u.PlayerProfession.MAGE)
def set_profession(name):
    u.SystemLibrary.execute_console_command(authority.get_world(), 'RuntimeRecipe.Probe Profession '
        + authority.get_controlled_pawn().get_path_name() + ' ' + name, authority)
set_profession('Mage')


def tick(_):
    try:
        if time.monotonic() - started > 20:
            raise AssertionError('Timed out in ' + result['phase'])
        menu = pc.get_hud().get_character_menu()
        if result['phase'] == 'profession':
            if any(p.get_editor_property('player_state').get_editor_property('profession') != u.PlayerProfession.MAGE
                   for p in (pc, authority)):
                return
            command('UMG.Character.Attributes')
            command('UMG.Character.Page 3')
            result['phase'] = 'open'
        elif result['phase'] == 'open':
            if not menu or not menu.is_visible() or menu.get_active_page() != 3:
                return
            command('UMG.Click RecipeBegin')
            result['phase'] = 'begin'
        elif result['phase'] == 'begin':
            if not all(preview(p).get_editor_property('preview_active') for p in (pc, authority)):
                return
            assert all(len(specs(p)) == len(before[p.get_path_name()]) + 1 for p in (pc, authority))
            result['checks']['real_remote_begin_has_one_temporary_spec'] = True
            result['session'] = preview(pc).get_editor_property('preview_session_id').export_text()
            command('UMG.Character.Remove')
            result['phase'] = 'destroyed'
        elif result['phase'] == 'destroyed':
            if any(preview(p).get_editor_property('preview_active') for p in (pc, authority)):
                return
            if any(specs(p) != before[p.get_path_name()] for p in (pc, authority)):
                return
            assert not menu.is_in_viewport()
            result['checks']['removing_parent_cleans_remote_session_and_exact_spec_set'] = True
            result['checks']['removed_host_did_not_reopen_itself'] = True
            set_profession('Mage' if original_profession == u.PlayerProfession.MAGE else 'Unselected')
            result['phase'] = 'restore'
        elif result['phase'] == 'restore':
            if any(p.get_editor_property('player_state').get_editor_property('profession') != original_profession
                   for p in (pc, authority)):
                return
            result['checks']['original_qa_profession_restored'] = True
            finish()
            return
        write()
    except Exception as error:
        finish(repr(error))


handle = u.register_slate_post_tick_callback(tick)
write()
