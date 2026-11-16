"""Author or independently read back the two Batch6 lightning skills in Unreal Editor."""
import json
import os
import shutil
from pathlib import Path
from uuid import uuid4

import unreal as u


BASE = '/Game/CompositionBatch6'
OLD = 'FissionChain'
NEW = 'SequentialChain'
BEAM = '/Game/PureMass/Effects/NS_DungeonElectricBeam'
CATALOGS = (
    '/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo',
    '/Game/PureMass/DA_MassAbilityInfo',
    '/Game/PrototypeExtensions/Batch5/DA_AbilityInfo',
)
A = u.EditorAssetLibrary
TOOLS = u.AssetToolsHelpers.get_asset_tools()
ROOT = Path(u.Paths.project_dir()).resolve()
OUT = ROOT / 'Saved/GAAuthoring/LightningVariants' / uuid4().hex


def path(prefix, name):
    return f'{BASE}/{prefix}_{name}'


def tag(name):
    result = u.GameplayTag()
    result.import_text(f'(TagName="{name}")')
    assert name in result.export_text(), name
    return result


def load(name):
    result = u.load_asset(name)
    assert result, name
    return result


def save(obj):
    assert A.save_loaded_asset(obj, False), obj.get_path_name()


def rows_for(catalog, name):
    identity = f'Abilities.Batch6.{name}'
    return [
        row for row in catalog.get_editor_property('ability_information')
        if row.get_editor_property('ability_tag').export_text() == tag(identity).export_text()
    ]


def defaults(name):
    klass = A.load_blueprint_class(path('GA', name))
    assert klass, name
    cdo = u.get_default_object(klass)
    assert isinstance(cdo, u.Batch6ChainAbility), name
    return cdo


def duplicate(prefix, source):
    destination = path(prefix, NEW)
    if A.does_asset_exist(destination):
        return load(destination)
    duplicated = TOOLS.duplicate_asset(f'{prefix}_{NEW}', BASE, source)
    assert duplicated, destination
    return duplicated


def baseline(cdo, config):
    return {
        'mana': config.get_editor_property('mana_cost'),
        'cooldown': config.get_editor_property('cooldown'),
        'damage': config.get_editor_property('damage'),
        'damage_per_level': config.get_editor_property('damage_per_level'),
        'falloff': config.get_editor_property('damage_falloff'),
        'max_targets': cdo.get_editor_property('max_targets'),
        'jump_radius': cdo.get_editor_property('jump_radius'),
        'cast_range': cdo.get_editor_property('cast_range'),
    }


def author():
    old_cdo = defaults(OLD)
    old_config = load(path('DA', OLD))
    assert old_cdo.get_editor_property('prototype') == old_config
    old_bp = load(path('GA', OLD))
    beam = load(BEAM)
    catalogs = [load(name) for name in CATALOGS]
    original_rows = []
    for catalog in catalogs:
        original = rows_for(catalog, OLD)
        existing = rows_for(catalog, NEW)
        assert len(original) == 1 and len(existing) <= 1, catalog.get_path_name()
        if existing:
            assert existing[0].get_editor_property('ability').get_path_name() == path('GA', NEW) + '.GA_SequentialChain_C'
        original_rows.append(original[0])
    for name in (f'Abilities.Batch6.{NEW}', f'Cooldown.Batch6.{NEW}'):
        tag(name)
    before = baseline(old_cdo, old_config)
    for package in (path('DA', OLD), path('GA', OLD), path('DA', NEW), path('GA', NEW), *CATALOGS):
        relative = Path(package.replace('/Game/', 'Content/', 1) + '.uasset')
        if (ROOT / relative).is_file():
            backup = OUT / 'before' / relative
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / relative, backup)
    (OUT / 'baseline.json').write_text(json.dumps(before, indent=2), encoding='utf-8')

    config = duplicate('DA', old_config)
    assert isinstance(config, u.AbilityPrototype)
    config.set_editor_property('display_name', '闪电链')
    config.set_editor_property('cooldown_tag', tag(f'Cooldown.Batch6.{NEW}'))
    config.set_editor_property('damage_falloff', 1.0)
    assert config.is_valid_configuration()
    save(config)

    bp = duplicate('GA', old_bp)
    new_cdo = defaults(NEW)
    new_cdo.set_editor_property('prototype', config)
    new_cdo.set_editor_property('sequential_chain', True)
    new_cdo.set_editor_property('jump_delay', 0.1)
    new_cdo.set_editor_property('beam_system', beam)
    new_cdo.set_editor_property('max_targets', 6)
    new_cdo.set_editor_property('jump_radius', 400.0)
    new_cdo.set_editor_property('cast_range', 1200.0)
    new_cdo.set_editor_property('startup_input_tag', u.GameplayTag())
    tags = u.GameplayTagContainer()
    tags.import_text(f'(GameplayTags=((TagName="Abilities.Batch6.{NEW}")))')
    new_cdo.set_editor_property('ability_tags', tags)
    u.BlueprintEditorLibrary.compile_blueprint(bp)
    save(bp)

    old_config.set_editor_property('display_name', '叉状闪电')
    save(old_config)
    if old_cdo.get_editor_property('sequential_chain') or old_cdo.get_editor_property('beam_system') != beam:
        old_cdo.set_editor_property('sequential_chain', False)
        old_cdo.set_editor_property('beam_system', beam)
        u.BlueprintEditorLibrary.compile_blueprint(old_bp)
        save(old_bp)

    for catalog, original in zip(catalogs, original_rows):
        rows = list(catalog.get_editor_property('ability_information'))
        if rows_for(catalog, NEW):
            continue
        row = u.MyAbilityInfo()
        old_class = A.load_blueprint_class(path('GA', OLD)).get_path_name()
        new_class = A.load_blueprint_class(path('GA', NEW)).get_path_name()
        row.import_text(
            original.export_text()
            .replace(f'Abilities.Batch6.{OLD}', f'Abilities.Batch6.{NEW}')
            .replace(f'Cooldown.Batch6.{OLD}', f'Cooldown.Batch6.{NEW}')
            .replace(old_class, new_class)
        )
        assert row.get_editor_property('ability').get_path_name() == new_class
        assert row.get_editor_property('ability_tag').export_text() == tag(f'Abilities.Batch6.{NEW}').export_text()
        rows.append(row)
        catalog.set_editor_property('ability_information', rows)
        save(catalog)
    assert baseline(defaults(OLD), old_config) == before


def verify():
    old, new = defaults(OLD), defaults(NEW)
    old_config = old.get_editor_property('prototype')
    new_config = new.get_editor_property('prototype')
    assert str(old_config.get_editor_property('display_name')) == '叉状闪电'
    assert str(new_config.get_editor_property('display_name')) == '闪电链'
    assert not old.get_editor_property('sequential_chain') and new.get_editor_property('sequential_chain')
    assert abs(new.get_editor_property('jump_delay') - 0.1) < 1e-6
    assert new.get_editor_property('max_targets') == 6
    assert new.get_editor_property('jump_radius') == 400.0
    assert new.get_editor_property('beam_system') == old.get_editor_property('beam_system') == load(BEAM)
    assert new.get_editor_property('startup_input_tag').export_text() == u.GameplayTag().export_text()
    assert f'Abilities.Batch6.{NEW}' in new.get_editor_property('ability_tags').export_text()
    assert new_config.get_editor_property('cooldown_tag').export_text() == tag(f'Cooldown.Batch6.{NEW}').export_text()
    assert new_config.get_editor_property('damage_falloff') == 1.0
    previous, current = baseline(old, old_config), baseline(new, new_config)
    for key in ('mana', 'cooldown', 'damage', 'damage_per_level'):
        assert previous[key] == current[key], key
    for name in CATALOGS:
        catalog = load(name)
        for skill in (OLD, NEW):
            entries = rows_for(catalog, skill)
            assert len(entries) == 1 and entries[0].get_editor_property('ability') == A.load_blueprint_class(path('GA', skill)), name
    return {'passed': True, 'old': previous, 'new': current, 'catalogs': CATALOGS}


OUT.mkdir(parents=True)
mode = os.environ.get('LIGHTNING_ASSET_MODE', 'verify')
assert mode in ('author', 'verify')
if mode == 'author':
    author()
data = verify()
data['mode'] = mode
receipt = OUT / 'receipt.json'
receipt.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')
u.log('LIGHTNING_ASSETS_OK ' + str(receipt))
