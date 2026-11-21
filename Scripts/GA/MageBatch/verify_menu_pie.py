"""Two-player PIE check of the actual spell list and menu learning/equip RPCs."""
import json
import os
import time
from pathlib import Path
from uuid import uuid4

import unreal as u


root = Path(u.Paths.project_dir())
specs = json.loads((root / os.environ.get('GA_BATCH_SPECS', 'Scripts/GA/MageBatch/skills.json')).read_text(encoding='utf-8-sig'))
excluded_specs = []
profession_filter = os.environ.get('GA_MENU_PROFESSION')
if profession_filter:
    excluded_specs = [s for s in specs if s.get('profession', 'MAGE') != profession_filter]
    specs = [s for s in specs if s.get('profession', 'MAGE') == profession_filter]
namespace = os.environ.get('GA_BATCH_NAMESPACE', 'Mage')
receipt = root / os.environ.get('GA_MENU_RECEIPT', f'Saved/GAAuthoring/MageBatch/menu-{uuid4().hex}.json')
receipt.parent.mkdir(parents=True, exist_ok=True)
state = {'phase':'wait', 'started':time.monotonic(), 'views':[], 'learned':[], 'equipped':[], 'passed':False}
menus = []


def tag(name):
    result = u.GameplayTag()
    result.import_text(f'(TagName="{name}")')
    return result


def selected(controller, spec):
    controller.spell_globe_selected(tag(f'Abilities.{namespace}.{spec["id"]}'))


def owns_widget(parent, widget):
    return widget.get_path_name().startswith(parent.get_path_name() + '.')


def controller_asc(pc):
    player_state = pc.get_editor_property('player_state')
    return player_state.get_component_by_class(u.MyAbilitySystemComponent)


def attributes(asc):
    attributes = asc.get_attribute_set(u.MyAttributeSet)
    return {
        name: attributes.get_editor_property(name).get_editor_property('current_value')
        for name in ('mana_regeneration', 'fire_resistance', 'thunder_resistance')
    }


def finish(error=None):
    u.unregister_slate_post_tick_callback(handle)
    for menu in menus:
        menu.remove_from_parent()
    state.update(passed=error is None, error=error)
    receipt.write_text(json.dumps(state, ensure_ascii=False, indent=2), encoding='utf-8')
    u.log('MAGE_MENU_RECEIPT ' + str(receipt))
    if os.environ.get('GA_MENU_QUIT') == '1':
        u.get_editor_subsystem(u.LevelEditorSubsystem).editor_request_end_play()
        u.SystemLibrary.quit_editor()


def setup_views(controllers):
    cls = u.load_class(None, '/Game/BluePrints/UI/SpellMenu/WBP_SpellMune.WBP_SpellMune_C')
    factory = u.get_default_object(u.load_class(None, '/Script/UMG.WidgetBlueprintLibrary'))
    for pc in controllers:
        menu = factory.call_method('Create', (pc.get_world(), cls, pc))
        assert menu
        menu.set_widget_controller(u.MyAbilitySystemLibrary.get_spell_menu_widget_controller(pc.get_world()))
        menu.add_to_viewport()
        menus.append(menu)


def verify_entries(menu):
    trees = [t for t in u.ObjectIterator(u.PrototypeSpellTree) if owns_widget(menu, t)]
    assert len(trees) == 1, ('expected one skill tree', len(trees))
    tree = trees[0]
    tree.call_method('ShowPrototypes')
    menu.force_layout_prepass()
    entries = [e for e in u.ObjectIterator(u.PrototypeSpellEntry) if owns_widget(tree, e) and e.get_parent()]
    labels = [t for t in u.ObjectIterator(u.TextBlock) if owns_widget(tree, t)]
    state.setdefault('tree_diagnostics', []).append({
        'pawn': menu.get_owning_player().get_controlled_pawn().get_class().get_path_name(),
        'controller': str(tree.get_editor_property('widget_controller')),
        'labels': [str(label.get_text()) for label in labels],
        'menu_controller': str(menu.get_editor_property('widget_controller')),
    })
    for spec in excluded_specs:
        assert all(str(label.get_text()) != spec['name'] for label in labels), ('wrong profession visible', spec['id'])
    descriptions = [t for t in u.ObjectIterator(u.RichTextBlock) if owns_widget(menu, t)]
    icons = [i for i in u.ObjectIterator(u.Image) if owns_widget(tree, i)]
    for spec in specs:
        matching = [e for e in entries if any(
            owns_widget(e, t) and str(t.get_text()) == spec['name'] for t in labels)]
        assert len(matching) == 1, ('missing or duplicate', spec['id'], len(matching))
        assert any(owns_widget(matching[0], icon) for icon in icons), spec['id']
        matching[0].call_method('Select')
        menu.force_layout_prepass()
        assert any(spec['name'] in str(t.get_text()) for t in descriptions), ('description', spec['id'])
    state['views'].append(menu.get_owning_player().get_path_name())


def tick(_delta):
    try:
        now = time.monotonic()
        assert now - state['started'] < 65, ('timed out', state['phase'], state.get('index'))
        pcs = [p for p in u.ObjectIterator(u.MyPlayerController)
               if 'UEDPIE_' in p.get_path_name() and p.get_controlled_pawn()]
        local = [p for p in pcs if p.is_local_controller()]
        remote = next((p for p in local if p.get_local_role() == u.NetRole.ROLE_AUTONOMOUS_PROXY), None)
        server = next((p for p in pcs if not p.is_local_controller() and
                       p.get_local_role() == u.NetRole.ROLE_AUTHORITY), None)
        if len(local) != 2 or remote is None or server is None:
            return
        assert u.GameplayStatics.get_current_level_name(server.get_world(), True) == 'L_Batch6Test'
        if state['phase'] == 'wait':
            if now - state['started'] < 3:
                return
            pawn = server.get_controlled_pawn()
            points = pawn.call_method('GetSpellPoints')
            if points < len(specs):
                pawn.call_method('AddToSpellPoints', (len(specs) - points,))
            setup_views(local)
            state.update(phase='entries', ready=now+1.5)
            return
        if now < state['ready']:
            return
        if state['phase'] == 'entries':
            for menu in menus:
                verify_entries(menu)
            state.update(phase='spend', index=0, ready=now+0.5,
                         baseline=attributes(controller_asc(server)))
            return
        client_asc = controller_asc(remote)
        server_asc = controller_asc(server)
        control = next(menu.get_editor_property('widget_controller') for menu in menus
                       if menu.get_owning_player() == remote)
        current = specs[state['index']]
        identity = tag(f'Abilities.{namespace}.{current["id"]}')
        passive = 'modifiers' in current
        slot = 'InputTag.Passive.1' if passive else 'InputTag.4'
        if state['phase'] == 'spend':
            state['before'] = server.get_controlled_pawn().call_method('GetSpellPoints')
            selected(control, current)
            # Execute the existing menu action on the owning PIE world's timer.
            u.get_default_object(u.SystemLibrary).call_method(
                'K2_SetTimer', (control, 'SpendPointButtonPressed', 0.1, False, False, 0.0, 0.0))
            state.update(phase='equip', ready=now+0.8)
        elif state['phase'] == 'equip':
            after = server.get_controlled_pawn().call_method('GetSpellPoints')
            assert after == state['before'] - 1, ('spell points', current['id'], state['before'], after)
            state['learned'].append(current['id'])
            selected(control, current)
            control.equip_button_pressed()
            ability_type = tag('Abilities.Type.Passive' if passive else 'Abilities.Type.Offensive')
            assert u.GameplayMenuInputProbe.queue_spell_slot(control, tag(slot), ability_type), current['id']
            state.update(phase='check', ready=now+0.8)
        elif state['phase'] == 'check':
            assert server_asc.get_slot_from_ability_tag(identity).export_text() == tag(slot).export_text(), current['id']
            assert client_asc.get_slot_from_ability_tag(identity).export_text() == tag(slot).export_text(), current['id']
            baseline = state['baseline']
            current_attrs = attributes(server_asc)
            if current['id'] == 'ArcaneMeditation':
                assert abs(current_attrs['mana_regeneration'] - baseline['mana_regeneration'] - 2) < 0.01
                state['meditation'] = current_attrs
            if current['id'] == 'ElementalWard':
                assert abs(current_attrs['mana_regeneration'] - baseline['mana_regeneration']) < 0.01
                for name in ('fire_resistance', 'thunder_resistance'):
                    assert abs(current_attrs[name] - baseline[name] - 12) < 0.01
                state['ward_replaces_meditation'] = current_attrs
            state['equipped'].append(current['id'])
            state['index'] += 1
            if state['index'] == len(specs):
                finish()
            else:
                state.update(phase='spend', ready=now+0.2)
    except Exception as exc:
        finish(repr(exc))


handle = u.register_slate_post_tick_callback(tick)
receipt.write_text(json.dumps(state, ensure_ascii=False, indent=2), encoding='utf-8')
