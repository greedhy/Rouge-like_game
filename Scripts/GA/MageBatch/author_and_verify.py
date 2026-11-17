"""Create and independently verify the Mage batch's prototypes, Schema 2 graphs and learning catalog."""
import json
import math
import os
import shutil
from pathlib import Path
from uuid import uuid4

import unreal as u


BASE = '/Game/UnifiedAbilities/MageBatch'
CATALOGS = (
    '/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo',
    '/Game/PureMass/DA_MassAbilityInfo',
    '/Game/PrototypeExtensions/Batch5/DA_AbilityInfo',
)
DAMAGE_GE = '/Game/BluePrints/AbilitySystem/Player/Effect/GE_Damage'
A = u.EditorAssetLibrary
TOOLS = u.AssetToolsHelpers.get_asset_tools()
ROOT = Path(u.Paths.project_dir()).resolve()
OUT = ROOT / 'Saved/GAAuthoring/MageBatch' / uuid4().hex
MODE = os.environ.get('MAGE_ASSET_MODE', 'verify')
assert MODE in ('author', 'author_assets', 'verify', 'verify_assets', 'register', 'update_passives')
AUTHOR = MODE in ('author', 'author_assets')
EDIT_CATALOGS = MODE in ('author', 'register')
SPECS = json.loads((ROOT / 'Scripts/GA/MageBatch/skills.json').read_text(encoding='utf-8-sig'))


def struct(cls, value):
    result = cls()
    result.import_text(value)
    return result


def tags(*names):
    return struct(u.GameplayTagContainer, '(GameplayTags=(' + ','.join(f'(TagName="{n}")' for n in names) + '))')


def tag(name):
    result = struct(u.GameplayTag, f'(TagName="{name}")')
    assert name in result.export_text(), name
    return result


def load(path):
    asset = u.load_asset(path)
    assert asset, path
    return asset


def klass(path):
    cls = A.load_blueprint_class(path)
    assert cls, path
    return cls


def save(asset):
    if isinstance(asset, u.Blueprint):
        u.BlueprintEditorLibrary.compile_blueprint(asset)
    assert A.save_loaded_asset(asset, False), asset.get_path_name()


def get_or_create(name, cls, factory):
    path = f'{BASE}/{name}'
    if A.does_asset_exist(path):
        return load(path)
    assert AUTHOR, path
    asset = TOOLS.create_asset(name, BASE, cls, factory)
    assert asset, path
    return asset


def data_asset(name, cls):
    factory = u.DataAssetFactory()
    factory.set_editor_property('data_asset_class', cls)
    asset = get_or_create(name, u.DataAsset, factory)
    assert isinstance(asset, cls), name
    return asset


def blueprint(name, parent):
    factory = u.BlueprintFactory()
    factory.set_editor_property('parent_class', parent)
    asset = get_or_create(name, u.Blueprint, factory)
    cls = klass(f'{BASE}/{name}')
    assert u.MathLibrary.class_is_child_of(cls, parent.static_class()), name
    return asset, u.get_default_object(cls)


def set_or_check(obj, key, want):
    if AUTHOR:
        obj.set_editor_property(key, want)
        return
    got = obj.get_editor_property(key)
    if isinstance(want, float):
        same = math.isclose(got, want, abs_tol=1e-5)
    elif isinstance(want, str):
        same = str(got) == want
    elif hasattr(want, 'export_text'):
        same = got.export_text() == want.export_text()
    else:
        same = got == want
    assert same, (obj.get_path_name(), key, str(got), str(want))


def modifier(attribute, delta, channel):
    return struct(u.GameplayModifierInfo,
        f'(Attribute=(AttributeName="{attribute}",Attribute=/Script/test_25d.MyAttributeSet:{attribute},'
        'AttributeOwner="/Script/CoreUObject.Class\'/Script/test_25d.MyAttributeSet\'"),'
        'ModifierOp=AddBase,ModifierMagnitude=(MagnitudeCalculationType=ScalableFloat,'
        f'ScalableFloatMagnitude=(Value={delta})),EvaluationChannelSettings=(Channel=Channel{channel}))')


def effect(name, duration, modifiers, cooldown_tag=None, seconds=0):
    bp, cdo = blueprint(name, u.GameplayEffect)
    set_or_check(cdo, 'duration_policy', duration)
    # Secondary attributes use a Channel 0 Override. Flat passive bonuses run after it.
    channel = 1 if duration == u.GameplayEffectDurationType.INFINITE else 0
    expected = [modifier(attribute, delta, channel) for attribute, delta in modifiers.items()]
    edit = AUTHOR or (MODE == 'update_passives' and channel == 1)
    if edit:
        cdo.set_editor_property('modifiers', expected)
    else:
        assert [m.export_text() for m in cdo.get_editor_property('modifiers')] == [m.export_text() for m in expected]
    if cooldown_tag:
        magnitude = struct(u.GameplayEffectModifierMagnitude,
            f'(MagnitudeCalculationType=ScalableFloat,ScalableFloatMagnitude=(Value={seconds}))')
        if AUTHOR:
            cdo.set_editor_property('duration_magnitude', magnitude)
            components = list(cdo.get_editor_property('ge_components'))
            component = next((x for x in components if x.get_name() == 'CooldownTags'), None)
            if component is None:
                cls = u.load_class(None, '/Script/GameplayAbilities.TargetTagsGameplayEffectComponent')
                component = u.new_object(cls, outer=cdo, name='CooldownTags')
                cdo.set_editor_property('ge_components', [component])
            component.set_editor_property('InheritableGrantedTagsContainer', struct(u.InheritedTagContainer,
                f'(CombinedTags=(GameplayTags=((TagName="{cooldown_tag}"))),'
                f'Added=(GameplayTags=((TagName="{cooldown_tag}"))),Removed=(GameplayTags=()))'))
        else:
            got = cdo.get_editor_property('duration_magnitude').get_editor_property('scalable_float_magnitude')
            assert math.isclose(got.get_editor_property('value'), seconds)
            components = cdo.get_editor_property('ge_components')
            assert any(cooldown_tag in component.get_editor_property('InheritableGrantedTagsContainer').export_text()
                       for component in components)
    if edit:
        save(bp)
    return klass(f'{BASE}/{name}')


def configure_prototype(obj, params, links):
    kind = params['kind']
    set_or_check(obj, 'kind', getattr(u.AbilityPrototypeKind, kind))
    set_or_check(obj, 'damage_type', tag('Damage.' + params.get('element', 'Magic')))
    set_or_check(obj, 'mana_cost', 0.0)
    set_or_check(obj, 'cooldown', 0.0)
    set_or_check(obj, 'damage', float(params.get('damage', 0)))
    set_or_check(obj, 'damage_per_level', 0.0)
    for key, value in params.items():
        if key in ('id', 'kind', 'damage', 'element'):
            continue
        if key.endswith('_vfx'):
            value = load(value)
        elif key != 'count':
            value = float(value)
        set_or_check(obj, key, value)
    for field, value in links.items():
        set_or_check(obj, field, value)
    assert obj.is_valid_configuration(), obj.get_path_name()


def configure_graph(graph, spec, persistent_effect=None):
    set_or_check(graph, 'schema_version', 2)
    set_or_check(graph, 'compatibility_profile', 'UnifiedV2')
    if persistent_effect:
        definitions = [('Passive', 'Status.OwnedEffect', u.AbilityOwnedEffectNodeConfig)]
        by_id = {}
        prototype_specs = {}
    else:
        definitions = [(n['id'], 'Legacy.Mixed.' + n['kind'].capitalize(), u.AbilityLegacyNodeConfig)
                       for n in spec['nodes']]
        prototype_specs = {n['id']: n for n in spec['nodes']}
        snapshots = {
            key: data_asset(f'DA_{spec["id"]}_{key}', u.Batch6FieldPrototype)
            for key, _, _ in definitions
        }
        by_id = {key: {} for key in snapshots}
        for first, port, second in spec['edges']:
            by_id[first][{'OnExpire':'on_expire', 'OnPeriod':'on_period'}[port]] = snapshots[second]
        for key, prototype in reversed(list(snapshots.items())):
            configure_prototype(prototype, prototype_specs[key], by_id[key])
            if AUTHOR:
                save(prototype)
    stored = list(graph.get_editor_property('nodes'))
    if not AUTHOR:
        assert len(stored) == len(definitions)
    nodes = []
    ids = {}
    for index, (name, type_id, cls) in enumerate(definitions, 1):
        guid = struct(u.Guid, f'(A=1829117,B=1,C=1,D={index})')
        node = u.AbilityGraphNode() if AUTHOR else stored[index-1]
        if AUTHOR:
            config_name = name + 'Config'
            config = u.find_object(graph, config_name) or u.new_object(cls, outer=graph, name=config_name)
            node.set_editor_property('node_id', guid)
            node.set_editor_property('type_id', type_id)
            node.set_editor_property('config', config)
        else:
            assert str(node.get_editor_property('type_id')) == type_id
            assert node.get_editor_property('node_id').export_text() == guid.export_text()
            config = node.get_editor_property('config')
        assert isinstance(config, cls)
        if persistent_effect:
            set_or_check(config, 'effect_class', persistent_effect)
            set_or_check(config, 'until_graph_ends', True)
        else:
            if AUTHOR:
                params = (u.find_object(config, 'Parameters') or
                          u.new_object(u.Batch6FieldPrototype, outer=config, name='Parameters'))
            else:
                params = config.get_editor_property('parameters')
            configure_prototype(params, prototype_specs[name], by_id[name])
            set_or_check(config, 'parameters', params)
            set_or_check(config, 'presentation_parameters', snapshots[name])
            set_or_check(config, 'profile', 'LegacyMixed.v1')
        ids[name] = guid
        nodes.append(node)
    edges = []
    for first, port, second in spec.get('edges', []):
        edge = u.AbilityGraphEdge()
        edge.set_editor_property('from', ids[first])
        edge.set_editor_property('to', ids[second])
        edge.set_editor_property('output_port', port)
        edges.append(edge)
    set_or_check(graph, 'entry_node_id', ids[definitions[0][0]])
    if AUTHOR:
        graph.set_editor_property('nodes', nodes)
        graph.set_editor_property('edges', edges)
    else:
        assert [e.export_text() for e in graph.get_editor_property('edges')] == [e.export_text() for e in edges]
    assert graph.validate_for_authoring() == [], graph.get_path_name()
    if AUTHOR:
        save(graph)
    return [entry[1] for entry in definitions]


def create_skill(spec):
    name = spec['id']
    passive = 'modifiers' in spec
    identity = f'Abilities.Mage.{name}'
    graph = data_asset(f'DA_{name}_Graph', u.AbilityGraphDefinition)
    persistent = effect(f'GE_{name}', u.GameplayEffectDurationType.INFINITE, spec['modifiers']) if passive else None
    types = configure_graph(graph, spec, persistent)
    parent = u.MagePassiveGraphAbility if passive else u.MageGraphAbility
    ga_bp, ga = blueprint(f'GA_{name}', parent)
    set_or_check(ga, 'spell_name', spec['name'])
    set_or_check(ga, 'spell_summary', spec['summary'])
    set_or_check(ga, 'ability_tags', tags(identity))
    set_or_check(ga, 'graph', graph)
    if not passive:
        cost = effect(f'GE_{name}_Cost', u.GameplayEffectDurationType.INSTANT, {'Mana':-spec['mana']})
        cooldown = effect(f'GE_{name}_Cooldown', u.GameplayEffectDurationType.HAS_DURATION, {},
                          f'Cooldown.Mage.{name}', spec['cooldown'])
        set_or_check(ga, 'target_mode', getattr(u.UnifiedAbilityTargetMode, spec['mode']))
        set_or_check(ga, 'execution_lifetime', u.AbilityExecutionLifetime.INDEPENDENT_CAST_EFFECT)
        set_or_check(ga, 'activation_blocked_tags', tags('Debuff.Stun', 'Effects.HitReact'))
        set_or_check(ga, 'cost_gameplay_effect_class', cost)
        set_or_check(ga, 'cooldown_gameplay_effect_class', cooldown)
        set_or_check(ga, 'damage_effect_class', klass(DAMAGE_GE))
    if AUTHOR:
        save(ga_bp)
    assert ga.get_editor_property('startup_input_tag').export_text() == u.GameplayTag().export_text()
    return {'ga': klass(f'{BASE}/GA_{name}').get_path_name(), 'graph': graph.get_path_name(), 'nodes': types}


def verify_catalog(catalog, records):
    rows = list(catalog.get_editor_property('ability_information'))
    original = [row.export_text() for row in rows]
    changed = False
    for spec in SPECS:
        name = spec['id']
        identity = f'Abilities.Mage.{name}'
        expected_ability = records[name]['ga']
        matching = [row for row in rows if identity in row.get_editor_property('ability_tag').export_text()]
        assert len(matching) <= 1, identity
        if matching:
            assert matching[0].get_editor_property('ability').get_path_name() == expected_ability
            continue
        assert EDIT_CATALOGS, identity
        ability_type = 'Passive' if 'modifiers' in spec else 'Offensive'
        cooldown = '' if 'modifiers' in spec else f'Cooldown.Mage.{name}'
        icon = load(f'/Game/Skill_Icons/Textures/128x128/T_Skill_Icons__{spec["icon"]:02d}').get_path_name()
        row = struct(u.MyAbilityInfo,
            f'(AbilityTag=(TagName="{identity}"),CooldownTag=(TagName="{cooldown}"),'
            f'AbilityType=(TagName="Abilities.Type.{ability_type}"),LevelRequirement=1,'
            f'Ability="/Script/Engine.BlueprintGeneratedClass\'{expected_ability}\'",'
            f'Icon="/Script/Engine.Texture2D\'{icon}\'")')
        assert row.get_editor_property('ability').get_path_name() == expected_ability
        rows.append(row)
        changed = True
    if changed:
        catalog.set_editor_property('ability_information', rows)
        save(catalog)
    saved_rows = list(catalog.get_editor_property('ability_information'))
    assert [row.export_text() for row in saved_rows[:len(original)]] == original
    for spec in SPECS:
        identity = f'Abilities.Mage.{spec["id"]}'
        row = next(row for row in saved_rows if identity in row.get_editor_property('ability_tag').export_text())
        assert row.get_editor_property('level_requirement') == 1
        assert row.get_editor_property('icon')
        assert ('Passive' if 'modifiers' in spec else 'Offensive') in row.get_editor_property('ability_type').export_text()
    return len(saved_rows)


OUT.mkdir(parents=True)
catalogs = [load(path) for path in CATALOGS] if MODE not in ('author_assets', 'verify_assets') else []
for spec in SPECS:
    name = spec['id']
    tag(f'Abilities.Mage.{name}')
    if 'mana' in spec:
        tag(f'Cooldown.Mage.{name}')
    load(f'/Game/Skill_Icons/Textures/128x128/T_Skill_Icons__{spec["icon"]:02d}')
    for node in spec.get('nodes', []):
        for key, value in node.items():
            if key.endswith('_vfx'):
                load(value)
if EDIT_CATALOGS:
    for name in CATALOGS:
        relative = Path(name.replace('/Game/', 'Content/', 1) + '.uasset')
        dest = OUT / 'catalog_backup' / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / relative, dest)
records = {spec['id']: create_skill(spec) for spec in SPECS}
catalog_counts = {}
errors = {}
for name, catalog in zip(CATALOGS, catalogs):
    try:
        catalog_counts[name] = verify_catalog(catalog, records)
    except Exception as error:
        errors[name] = repr(error)
report = {'passed':not errors, 'mode':MODE, 'skills':records, 'catalogs':catalog_counts, 'errors':errors}
receipt = OUT / 'receipt.json'
receipt.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
if errors:
    raise RuntimeError(f'Catalog verification or save failed: {errors}. See {receipt}')
u.log('MAGE_BATCH_OK ' + str(receipt))
