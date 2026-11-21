"""Apply profession metadata and add the new melee rows to the Stander catalog.

Run in the editor after building the new FMyAbilityInfo field.
Default mode only checks the saved data in an independent process.
"""
import json
import os
import re
from pathlib import Path
from uuid import uuid4
import unreal as u

apply = os.environ.get('GA_PROFESSION_APPLY') == '1'
paths = [
    '/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo',
    '/Game/PureMass/DA_MassAbilityInfo',
    '/Game/PrototypeExtensions/Batch5/DA_AbilityInfo',
    '/Game/BluePrints/Characters/Stander/DA_SwordAbilityInfo',
]
melee = {'Abilities.Art.GhostCleave', 'Abilities.Art.CrimsonEruption', 'Abilities.Art.GhostMoonArray'}
assets = [u.load_asset(p) for p in paths]
assert all(assets)
source = assets[0].get_editor_property('ability_information')
def identity(row):
    text = row.get_editor_property('ability_tag').export_text()
    found = re.search(r'TagName="([^"]*)"', text)
    return found.group(1) if found else ''

melee_rows = [r for r in source if identity(r) in melee]
assert len(melee_rows) == 3
result = {}
for path, asset in zip(paths, assets):
    rows = list(asset.get_editor_property('ability_information'))
    stander = path == paths[-1]
    if stander:
        existing = {identity(r) for r in rows}
        for row in melee_rows:
            if identity(row) not in existing:
                assert apply, 'Missing melee row in Stander catalog'
                rows.append(row)
    for row in rows:
        name = identity(row)
        required = u.PlayerProfession.STANDER if stander or name in melee else u.PlayerProfession.MAGE
        if apply:
            row.set_editor_property('required_profession', required)
        assert row.get_editor_property('required_profession') == required, name
    if apply:
        asset.set_editor_property('ability_information', rows)
        assert u.EditorAssetLibrary.save_loaded_asset(asset, False), path
    result[path] = len(rows)
out = Path(u.Paths.project_saved_dir()) / 'GAAuthoring/ProfessionSkills'
out.mkdir(parents=True, exist_ok=True)
(out / (uuid4().hex + '.json')).write_text(json.dumps({'passed': True, 'apply': apply, 'catalogs': result}, indent=2))
