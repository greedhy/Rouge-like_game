"""Bake a rigid spear into a new goblin mesh, then migrate only matching Mass types."""
import unreal as u
import json, uuid, shutil, traceback
from pathlib import Path

root = Path(u.Paths.project_dir()).resolve()
out = root/'Saved/GAAuthoring/MassMergedGoblin'/uuid.uuid4().hex
out.mkdir(parents=True)
result = {}
folder = '/Game/PureMass/Rendering/Merged'
owner = 'MassMergedGoblinV1'

def save(asset):
    assert u.EditorAssetLibrary.save_loaded_asset(asset, False), asset.get_path_name()

def clone(source, name):
    path = folder+'/'+name
    assert not u.EditorAssetLibrary.does_asset_exist(path), 'Refuse to overwrite: '+path
    obj = u.EditorAssetLibrary.duplicate_asset(source, path)
    assert obj
    u.EditorAssetLibrary.set_metadata_tag(obj, 'AuthoringOwner', owner)
    return obj

try:
    assert not u.EditorLevelLibrary.get_game_world(), 'Stop PIE before authoring'
    dirty = list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    assert not dirty, [p.get_name() for p in dirty]
    baseline = u.load_asset('/Game/PureMass/DA_MassGoblin')
    body, weapon = baseline.mesh, baseline.weapon_mesh
    assert body and weapon
    mesh = clone(body.get_path_name(), 'SKM_MassGoblin_Armed')
    report = u.PureMassAssetAuthoring.bake_rigid_weapon(mesh, body, weapon, baseline.weapon_socket, baseline.weapon_offset)
    result['bake'] = report
    assert report.startswith('OK'), report
    save(mesh)
    bank = clone('/Game/PureMass/Rendering/AB_MassGoblin', 'AB_MassGoblin_Armed')
    bank.set_editor_property('asset', mesh)
    save(bank)
    provider = u.PureMassCrowd.create_animation_provider(bank)
    assert provider
    save(provider)
    result['mesh'] = mesh.get_path_name()
    result['bank'] = bank.get_path_name()
    result['provider'] = provider.get_path_name()
    result['skeleton'] = mesh.get_editor_property('skeleton').get_path_name()
    result['materials'] = [s.material_interface.get_path_name() for s in mesh.materials]
    result['physics_asset'] = str(mesh.get_editor_property('physics_asset'))
    assert mesh.get_editor_property('skeleton') == body.get_editor_property('skeleton')
    assert mesh.get_editor_property('physics_asset') == body.get_editor_property('physics_asset')
    registry = u.AssetRegistryHelpers.get_asset_registry()
    candidates = registry.get_assets_by_class(u.TopLevelAssetPath('/Script/test_25d', 'PureMassMonsterType'), True)
    types = [a.get_asset() for a in candidates]
    types = [t for t in types if t.mesh == body and t.weapon_mesh == weapon
             and t.weapon_socket == baseline.weapon_socket and t.weapon_offset == baseline.weapon_offset]
    assert len(types) >= 6, [t.get_path_name() for t in types]
    result['types'] = []
    # Back up every selected asset before changing any configuration.
    for t in types:
        source = root/'Content'/(t.get_path_name().split('.')[0].removeprefix('/Game/')+'.uasset')
        shutil.copy2(source, out/(t.get_name()+'.uasset'))
    for t in types:
        before = {'health':t.health, 'speed':t.speed, 'damage':t.attack_damage, 'period':t.attack_period}
        t.set_editor_property('mesh', mesh)
        t.set_editor_property('animation_provider', provider)
        t.set_editor_property('weapon_mesh', None)
        save(t)
        after = {'health':t.health, 'speed':t.speed, 'damage':t.attack_damage, 'period':t.attack_period}
        assert before == after
        result['types'].append({'path':t.get_path_name(), 'stats':after, 'separate_weapon':str(t.weapon_mesh)})
    result['passed'] = True
except Exception:
    result['passed'] = False
    result['error'] = traceback.format_exc()
finally:
    (out/'result.json').write_text(json.dumps(result, indent=2))
    (out.parent/'latest-authoring.txt').write_text(str(out))
    print(json.dumps(result, indent=2))
