"""Configure or verify the existing Fire Bolt and Arcane Shards release montages."""

import json
import os
import shutil
from pathlib import Path

import unreal as ue


ROOT = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_dir()))
REPORT = ROOT / "Saved/GAAuthoring/StanderCombat/2026-11-17"
MODE = os.environ.get("MAGE_RECOVERY_MODE", "verify")
ABILITY_ROOT = "/Game/BluePrints/AbilitySystem/Player/Ability"
MONTAGE_ROOT = "/Game/Assets/Characters/Aura/Animations/Abilities"
SPELLS = (
    ("Fire/FireBolt/GA_FireBolt", "AM_Cast_FireBolt", "Event.Montage.FireBolt"),
    ("Arcane/ArcaneChard/GA_ArcaneShards", "AM_Cast_ArcaneShards", "Event.Montage.AracaneShards"),
)


def inspect_notifies(animation):
    return [event.export_text() for event in ue.AnimationLibrary.get_animation_notify_events(animation)]


def configure():
    REPORT.mkdir(parents=True, exist_ok=True)
    data = {}
    pending = []
    for path, montage_name, release in SPELLS:
        bp = ue.load_asset(f"{ABILITY_ROOT}/{path}")
        montage = ue.load_asset(f"{MONTAGE_ROOT}/{montage_name}")
        assert bp and montage, (path, montage_name)
        spell = ue.get_default_object(bp.generated_class())
        assert isinstance(spell, ue.MyDamageGameplayAbility), path
        if "ArcaneShards" in path:
            assert spell.get_editor_property("shard_montage") == montage
        else:
            graph = ue.AbilityBlueprintAuditLibrary.describe_blueprint(bp)
            assert montage.get_path_name() in graph and release in graph, path
        tracks = montage.get_editor_property("slot_anim_tracks")
        clips = [
            segment.get_editor_property("anim_reference")
            for slot in tracks
            for segment in slot.get_editor_property("anim_track").get_editor_property("anim_segments")
        ]
        data[path] = {
            "montage": montage.get_path_name(),
            "length": montage.get_play_length(),
            "blend_out": montage.get_editor_property("blend_out").get_editor_property("blend_time"),
            "notifies": inspect_notifies(montage),
            "clip_notifies": {clip.get_path_name(): inspect_notifies(clip) for clip in clips},
        }
        if MODE == "inspect":
            continue
        release_tag = ue.GameplayTag()
        release_tag.import_text(f'(TagName="{release}")')
        assert release in release_tag.export_text()
        if MODE == "apply":
            spell.set_editor_property("recovery_montage", montage)
            spell.set_editor_property("recovery_release_event", release_tag)
            spell.set_editor_property("post_release_seconds", .12)
            pending.append(bp)
        assert spell.get_editor_property("recovery_montage") == montage
        assert spell.get_editor_property("recovery_release_event").export_text() == release_tag.export_text()
        assert abs(spell.get_editor_property("post_release_seconds") - .12) < .0001
        data[path].update(release=release, target_recovery_seconds=.12)
    for bp in pending:
        relative = Path(bp.get_path_name().split(".")[0].removeprefix("/Game/") + ".uasset")
        original = ROOT / "Content" / relative
        backup = REPORT / "before" / relative
        if not backup.exists():
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(original, backup)
        assert ue.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False), bp.get_path_name()
    (REPORT / f"mage-{MODE}.json").write_text(json.dumps(data, indent=2), encoding="utf-8")


configure()
