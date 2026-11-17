"""Apply or independently verify the default sword/shield playtest and live Mass health."""

import json
import os
import shutil
from pathlib import Path

import unreal as ue


ROOT = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_dir()))
REPORT = ROOT / "Saved/GAAuthoring/StanderCombat/2026-11-17"
APPLY = os.environ.get("STANDER_COMBAT_APPLY") == "1"
WEAPON = "/Game/BluePrints/Characters/Stander/Weapons/Sword_Shield"
TIMELINES = "/Game/AbilityTimelines/Stander/Sword_Shield"
ACTIONS = ("Attack1", "Attack2", "Attack3", "Skill7_Around", "Skill6_Move", "Skill1_Float", "Skill4_Push")
MASS = (
    "/Game/PureMass/DA_MassGoblin",
    "/Game/RoguelikeGeneration/Gameplay/DA_DungeonGoblin",
    "/Game/RoguelikeGeneration/Gameplay/DA_DungeonMassMelee",
)
LIVE_IMPULSES = {"Skill1_Float": (90.0, 0.0, 350.0), "Skill4_Push": (450.0, 0.0, 85.0)}
LIGHT_WINDUPS = {"Attack2": .17, "Attack3": .16}
DESTINATIONS = ("Attack1", "Attack2", "Attack3", "Block", *ACTIONS[3:])
TAGS = tuple("Abilities.StanderWeapons.Sword_Shield." + identifier for identifier in DESTINATIONS) + (
    "Abilities.Common.PerfectDodge",
)


def asset(path):
    value = ue.load_asset(path)
    assert value, path
    return value


def prop(value, name):
    return value.get_editor_property(name)


def tag_container(names):
    value = ue.GameplayTagContainer()
    tags = ",".join(f'(TagName="{name}")' for name in names)
    value.import_text(f"(GameplayTags=({tags}))")
    assert all(name in value.export_text() for name in names), value.export_text()
    return value


def phase(name, source_end, duration):
    value = ue.AbilityTimelinePhase()
    for key, setting in dict(name=name, source_end=source_end, duration=duration).items():
        value.set_editor_property(key, setting)
    return value


def save(value):
    if not APPLY:
        return
    package = value.get_path_name().split(".")[0].removeprefix("/Game/")
    relative = Path(package + ".uasset")
    backup = REPORT / "before" / relative
    if not backup.exists():
        backup.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / "Content" / relative, backup)
    assert ue.EditorAssetLibrary.save_loaded_asset(value, only_if_is_dirty=False), value.get_path_name()


def authored_phases(identifier, phases, montage, events):
    if identifier == "Attack1":
        return phases  # Existing 0.05 + 0.10 contact follow-through.
    contact = max(
        prop(e, "time") + (prop(e, "duration") if e.kind == ue.StanderWeaponActionKind.MOVE else 0.0)
        for e in events
    )
    length = montage.get_play_length()
    if str(prop(phases[-1], "name")) == "Recovery":
        return phases
    assert contact < length - .005, (identifier, contact, length)
    tail = length - contact
    result = []
    start = 0.0
    for old in phases:
        end = prop(old, "source_end")
        if end <= contact + .0001:
            result.append(old)
        elif start < contact:
            duration = prop(old, "duration") * (contact - start) / (end - start)
            result.append(phase(str(prop(old, "name")), contact, duration))
        start = end
    if identifier == "Attack3":
        # Preserve the existing effective rate and total post-contact time.
        release = .026
        previous = prop(phases[-1], "duration")
        boundary = contact + tail * release / previous
        recovery = previous - release
    else:
        release = .05
        boundary = contact + min(.09, tail * .27)
        recovery = max(.12, (length - boundary) / 9.8)
    result.extend((phase("Release", boundary, release), phase("Recovery", length, recovery)))
    return result


def set_or_require(value, name, desired):
    if APPLY:
        value.set_editor_property(name, desired)
    else:
        assert prop(value, name) == desired, (value, name, prop(value, name), desired)


def main():
    REPORT.mkdir(parents=True, exist_ok=True)
    report = {"mass": {}, "weapon": {}, "slots": {}, "mode": "apply" if APPLY else "verify"}
    weapon_set = asset(WEAPON + "/DA_WeaponSet")
    for entry in prop(weapon_set, "abilities"):
        slot = prop(entry, "input_tag").export_text()
        if 'TagName=""' not in slot:
            report["slots"][slot] = prop(entry, "ability").get_path_name()
    for key, action in zip(("LMB", "RMB", "1", "2", "3", "4"), ("Attack1", "Block", *ACTIONS[3:])):
        assert report["slots"][f'(TagName="InputTag.{key}")'].endswith(f"/GA_{action}.GA_{action}_C")
    for path in MASS:
        value = asset(path)
        set_or_require(value, "health", 1.0)
        report["mass"][path] = prop(value, "health")
        save(value)
    for identifier in ACTIONS:
        config = asset(f"{WEAPON}/DA_{identifier}")
        timeline = asset(f"{TIMELINES}/DA_{identifier}")
        assert timeline == prop(config, "timeline"), identifier
        montage = prop(config, "montage")
        events = prop(config, "events")
        phases = list(prop(timeline, "phases"))
        if APPLY:
            phases = list(authored_phases(identifier, phases, montage, events))
            if identifier in LIGHT_WINDUPS:
                assert str(prop(phases[0], "name")) == "Windup", identifier
                phases[0].set_editor_property("duration", LIGHT_WINDUPS[identifier])
            phases[-1].set_editor_property("cancel_into", tag_container(TAGS))
            timeline.set_editor_property("phases", phases)
        assert str(prop(phases[-1], "name")) == "Recovery", identifier
        assert all(tag in prop(phases[-1], "cancel_into").export_text() for tag in TAGS), identifier
        error = timeline.validate_for_montage(montage)
        assert not error, (identifier, error, [p.export_text() for p in phases])
        if identifier in LIVE_IMPULSES:
            matches = [e for e in events if e.kind == ue.StanderWeaponActionKind.MELEE]
            assert matches, identifier
            for index, event in enumerate(events):
                if event.kind == ue.StanderWeaponActionKind.MELEE:
                    set_or_require(event, "live_hit_impulse", ue.Vector(*LIVE_IMPULSES[identifier]))
                    events[index] = event
            if APPLY:
                config.set_editor_property("events", events)
            save(config)
        save(timeline)
        report["weapon"][identifier] = {
            "montage": montage.get_path_name(),
            "seconds": sum(prop(p, "duration") for p in phases),
            "phases": [p.export_text() for p in phases],
            "events": [e.export_text() for e in events],
        }
    (REPORT / (report["mode"] + ".json")).write_text(json.dumps(report, indent=2), encoding="utf-8")


main()
