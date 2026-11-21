"""Configure Grux's presentation and verify saved assets with GRUX_PRESENTATION_VERIFY_ONLY=1."""
import json
import os
import shutil
from pathlib import Path
from uuid import uuid4

import unreal


GRUX = "/Game/BluePrints/Characters/Grux/Expansion"
PRESENTATION = f"{GRUX}/Presentation/BPC_GruxCombatPresentation"
PARTICLES = "/Game/ParagonGrux/FX/Particles/Abilities"
SOUNDS = "/Game/ParagonGrux/Characters/Heroes/Grux/Sounds/SoundCues"

# Key, original Cascade asset, original SoundCue, attach to Grux's mesh, scale, Z offset.
EFFECTS = (
    ("AxeImpact", "Primary/FX/P_Grux_Melee_SucessfulImpact", "Grux_Effort_Ability_Primary", False, .75, 8.0),
    ("Charge", "Stampede/FX/P_Stampede_Intro", "Grux_Effort_Ability_Q", True, .75, 0.0),
    ("Uppercut", "HardKnocks/FX/P_Grux_Knockup", "Grux_Effort_Ability_E", False, .6, 5.0),
    ("Roar", "Ultimate/FX/Rework/P_GruxUlt_Roar", "Grux_Effort_Ability_Ultimate", True, .7, 95.0),
    ("RoarArmor", "Ultimate/FX/P_ExecuteBuff_Loop", None, True, .75, 100.0),
)

STAGES = {
    "Combo": (("tuning", "AxeImpact"), ("followup", "AxeImpact")),
    "Charge": (("tuning", "Charge"), ("followup", "Uppercut")),
    "Roar": (("tuning", "Roar"),),
}


def checked_asset(path, kind):
    asset = unreal.load_asset(path)
    assert isinstance(asset, kind), (path, kind, asset)
    return asset


def expected_presets():
    presets = {}
    for key, particle, sound, attach, scale, height in EFFECTS:
        preset = unreal.CombatPresentationPreset()
        preset.set_editor_property("particle", checked_asset(f"{PARTICLES}/{particle}", unreal.ParticleSystem))
        if sound:
            preset.set_editor_property("sound", checked_asset(f"{SOUNDS}/{sound}", unreal.SoundCue))
        preset.set_editor_property("attach_particle", attach)
        preset.set_editor_property("particle_scale", scale)
        preset.set_editor_property("particle_offset", unreal.Vector(0, 0, height))
        preset.set_editor_property("sound_volume", .85)
        presets[unreal.Name(key)] = preset
    return presets


def author(report_dir, boss, attacks, presets):
    for path in [PRESENTATION, f"{GRUX}/BP_GruxBoss", *(f"{GRUX}/DA_Grux_{key}" for key in STAGES)]:
        source = Path(unreal.Paths.project_content_dir()) / (path.removeprefix("/Game/") + ".uasset")
        if source.is_file():
            shutil.copy2(source, report_dir / "before" / source.name)

    editor = unreal.EditorAssetLibrary
    path, name = PRESENTATION.rsplit("/", 1)
    blueprint = editor.load_asset(PRESENTATION) if editor.does_asset_exist(PRESENTATION) else None
    if blueprint is None:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", unreal.CombatPresentationComponent.static_class())
        blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, path, unreal.Blueprint, factory)
        assert blueprint, PRESENTATION
    assert isinstance(blueprint, unreal.Blueprint), PRESENTATION
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    cls = unreal.EditorAssetLibrary.load_blueprint_class(PRESENTATION)
    defaults = unreal.get_default_object(cls)
    assert isinstance(defaults, unreal.CombatPresentationComponent), PRESENTATION
    defaults.set_editor_property("presets", presets)
    assert editor.save_loaded_asset(blueprint, False), PRESENTATION

    for name, phases in STAGES.items():
        config = attacks[name]
        for prop, key in phases:
            stage = config.get_editor_property(prop)
            stage.set_editor_property("impact_presentation_preset", unreal.Name(key))
            config.set_editor_property(prop, stage)
        assert editor.save_loaded_asset(config, False), config.get_path_name()

    unreal.get_default_object(boss).set_editor_property("combat_presentation_class", cls)
    assert editor.save_asset(f"{GRUX}/BP_GruxBoss", False), boss.get_path_name()


def verify(boss, attacks, expected):
    cls = unreal.EditorAssetLibrary.load_blueprint_class(PRESENTATION)
    assert cls and unreal.get_default_object(boss).get_editor_property("combat_presentation_class") == cls
    actual = unreal.get_default_object(cls).get_editor_property("presets")
    assert set(actual.keys()) == set(expected.keys())
    result = {"boss": boss.get_path_name(), "blueprint": cls.get_path_name(), "presets": {}, "stages": {}}
    for key, expected_preset in expected.items():
        actual_preset = actual[key]
        for prop in ("particle", "sound", "attach_particle", "particle_offset", "particle_scale", "sound_volume"):
            assert actual_preset.get_editor_property(prop) == expected_preset.get_editor_property(prop), (key, prop)
        sound = actual_preset.get_editor_property("sound")
        result["presets"][str(key)] = {
            "particle": actual_preset.get_editor_property("particle").get_path_name(),
            "sound": sound.get_path_name() if sound else None,
            "attached": actual_preset.get_editor_property("attach_particle"),
        }
    for name, phases in STAGES.items():
        for prop, key in phases:
            stage = attacks[name].get_editor_property(prop)
            assert str(stage.get_editor_property("impact_presentation_preset")) == key, (name, prop)
            result["stages"][f"{name}.{prop}"] = key
    profile = unreal.load_asset("/Game/RoguelikeGeneration/Gameplay/DA_DungeonGameplay")
    boss_class = profile.get_editor_property("boss_class")
    assert "BP_GruxBoss.BP_GruxBoss_C" in str(boss_class), boss_class
    return result


def main():
    verify_only = os.environ.get("GRUX_PRESENTATION_VERIFY_ONLY") == "1"
    report_root = Path(unreal.Paths.project_saved_dir()) / "GAAuthoring" / "GruxPresentation"
    report_dir = report_root / uuid4().hex
    (report_dir / "before").mkdir(parents=True)
    boss = unreal.EditorAssetLibrary.load_blueprint_class(f"{GRUX}/BP_GruxBoss")
    assert isinstance(unreal.get_default_object(boss), unreal.GruxEnemyCharacter), boss
    attacks = {name: checked_asset(f"{GRUX}/DA_Grux_{name}", unreal.GruxAttackConfig) for name in STAGES}
    presets = expected_presets()
    if not verify_only:
        author(report_dir, boss, attacks, presets)
    report = verify(boss, attacks, presets)
    report["mode"] = "verify" if verify_only else "author"
    (report_dir / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    unreal.log(f"Grux presentation {report['mode']} passed: {report_dir / 'report.json'}")


main()
