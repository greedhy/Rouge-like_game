"""Reproduce the real L_RoguelikeRun direct-PIE entry without selecting a profession.

Only drives the original attribute/tab/selector/Begin/Close controls via the
native probe. No fixture sets a profession, attributes, targets, or saved data.
"""
import json
import time
from pathlib import Path
import unreal as u

root = Path(u.Paths.project_saved_dir()).resolve() / 'GAAuthoring/WorkshopMageAccess/20261121'
result = {'phase': 'startup', 'checks': {}, 'finished': False, 'passed': False}
started = time.monotonic()
pc = None
panel = None


def state():
    return pc.get_editor_property('player_state').get_component_by_class(u.AbilityRuntimeRecipeDeveloperComponent).get_developer_state()


def specs():
    asc = pc.get_editor_property('player_state').get_component_by_class(u.MyAbilitySystemComponent)
    return sorted(h.export_text() for h in asc.get_all_abilities())


def command(action):
    u.SystemLibrary.execute_console_command(pc.get_world(), 'RuntimeRecipe.Probe ' + action, pc)


def phase(name):
    result.update(phase=name, at=time.monotonic())


def write():
    (root / 'normal-entry-report.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')


def finish(error=None):
    result.update(finished=True, passed=not error, error=error)
    write()
    u.unregister_slate_post_tick_callback(handle)


def tick(_):
    global pc, panel
    try:
        if time.monotonic() - started > 30:
            raise AssertionError('Timed out in ' + result['phase'])
        name = result['phase']
        if name == 'startup':
            players = [p for p in u.ObjectIterator(u.MyPlayerController)
                       if u.SystemLibrary.is_valid(p) and 'UEDPIE_' in p.get_path_name()
                       and p.is_local_controller() and p.get_controlled_pawn() and p.get_editor_property('player_state')]
            if len(players) != 1:
                return
            pc = players[0]
            if not isinstance(pc.get_hud(), u.MyHUD):
                return
            assert 'L_RoguelikeRun' in pc.get_world().get_path_name(), 'Use actual gameplay map'
            assert pc.get_editor_property('player_state').get_editor_property('profession') == u.PlayerProfession.UNSELECTED
            result['checks']['real_gameplay_map_starts_without_selected_profession'] = True
            result['world'] = pc.get_world().get_path_name()
            result['pawn'] = pc.get_controlled_pawn().get_class().get_path_name()
            result['specs_before'] = specs()
            command('UMG.Character.Attributes')
            command('UMG.Character.Page 3')
            phase('open')
        elif name == 'open':
            menu = pc.get_hud().get_character_menu()
            panel = menu.get_runtime_recipe_panel() if menu else None
            if not panel or menu.get_active_page() != 3 or not panel.is_visible():
                return
            developer = pc.get_editor_property('player_state').get_component_by_class(u.AbilityRuntimeRecipeDeveloperComponent)
            row = next(r for r in developer.get_definitions()
                       if r.get_editor_property('base').get_editor_property('ability_class').get_path_name()
                       == '/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/GA_FireBolt.GA_FireBolt_C')
            result['definition_id'] = str(row.get_editor_property('definition_id'))
            command('UMG.Select RecipeDefinition ' + result['definition_id'])
            phase('selected')
        elif name == 'selected' and time.monotonic() - result['at'] > .15:
            command('UMG.Click RecipeBegin')
            phase('begin')
        elif name == 'begin':
            value = state()
            graph = value.get_editor_property('accepted_graph')
            if not value.get_editor_property('preview_active') or not graph.get_editor_property('ready'):
                return
            assert not value.get_editor_property('error')
            assert len(specs()) == len(result['specs_before']) + 1
            result['checks']['normal_attribute_fourth_page_begin_publishes_actual_firebolt_graph'] = True
            result['nodes'] = len(graph.get_editor_property('nodes'))
            result['edges'] = len(graph.get_editor_property('edges'))
            assert result['nodes'] and result['edges']
            command('UMG.Click RecipeGraphFit')
            phase('fit')
        elif name == 'fit' and time.monotonic() - result['at'] > .2:
            directory = root / 'screenshots'
            directory.mkdir(exist_ok=True)
            filename = directory / '00-real-game-map-direct-pie.png'
            result['screenshot'] = str(filename)
            u.SystemLibrary.execute_console_command(pc.get_world(), 'shot showui filename="' + str(filename) + '" -nosuffix', pc)
            phase('capture')
        elif name == 'capture' and time.monotonic() - result['at'] > .3:
            assert Path(result['screenshot']).exists()
            command('UMG.Character.Close')
            phase('closed')
        elif name == 'closed':
            if state().get_editor_property('preview_active') or specs() != result['specs_before']:
                return
            assert pc.get_editor_property('player_state').get_editor_property('profession') == u.PlayerProfession.UNSELECTED
            assert not pc.get_hud().get_character_menu().is_visible()
            result['checks']['temporary_spec_and_parent_menu_close_cleanly'] = True
            result['checks']['no_profession_or_formal_spec_changes'] = True
            result['checks']['real_viewport_input_focus_restored'] = panel.has_owner_viewport_focus()
            assert result['checks']['real_viewport_input_focus_restored']
            finish()
            return
        write()
    except Exception as error:
        finish(repr(error))


handle = u.register_slate_post_tick_callback(tick)
write()
