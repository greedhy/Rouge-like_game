"""Read-only asset and integration inventory for the sixth player GA batch."""
import json
import uuid
from pathlib import Path
import unreal as u

root = Path(u.Paths.project_dir()).resolve()
run = root / 'Saved/GAAuthoring/CompositionBatch6' / uuid.uuid4().hex
run.mkdir(parents=True)
registry = u.AssetRegistryHelpers.get_asset_registry()
report = {'run_id': run.name, 'assets': {}, 'catalogs': {}, 'dirty_packages': []}
for directory in ['/Game/Art/CompositionEffects', '/Game/Skill_Icons/Textures',
                  '/Game/PrototypeExtensions', '/Game/BluePrints/AbilitySystem/Player/Ability/Prototypes']:
    report['assets'][directory] = [
        {'path': str(a.package_name), 'class': str(a.asset_class_path)}
        for a in registry.get_assets_by_path(directory, recursive=True)]
for a in registry.get_assets_by_class(u.TopLevelAssetPath('/Script/test_25d', 'AbilityInfo'), True):
    obj = a.get_asset()
    report['catalogs'][obj.get_path_name()] = [r.export_text() for r in obj.get_editor_property('ability_information')]
for package in u.EditorLoadingAndSavingUtils.get_dirty_content_packages():
    report['dirty_packages'].append(package.get_path_name())
report['success'] = True
(run / 'inventory.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
(run.parent / 'latest-inventory.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
u.log('Batch6 inventory: ' + str(run))
