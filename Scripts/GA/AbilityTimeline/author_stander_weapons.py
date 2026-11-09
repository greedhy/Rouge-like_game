"""Author one timeline per Standard weapon GA. Use -StanderTimelineReadback to verify only."""
import json
import math
import shutil
import uuid
from pathlib import Path

import unreal as u

BASE = "/Game/BluePrints/Characters/Stander/Weapons"
DEST = "/Game/AbilityTimelines/Stander"
ROOT = Path(u.Paths.project_dir())
RUN = ROOT / "Saved/GAAuthoring/StanderTimelines" / uuid.uuid4().hex
RUN.mkdir(parents=True)
COMMAND_LINE = u.SystemLibrary.get_command_line().lower()
READBACK = "-standertimelinereadback" in COMMAND_LINE
ASSETS_ONLY = "-standertimelineassetsonly" in COMMAND_LINE
LIB = u.EditorAssetLibrary
TOOLS = u.AssetToolsHelpers.get_asset_tools()


def phases_for(config, montage):
    """Use real montage segment changes and authored action contacts/windows as boundaries."""
    boundaries = {}
    for slot in montage.get_editor_property("slot_anim_tracks"):
        segments = slot.get_editor_property("anim_track").get_editor_property("anim_segments")
        for index, segment in enumerate(segments[1:], 2):
            boundaries[segment.get_editor_property("start_pos")] = f"Animation{index:02d}"
    for index, event in enumerate(config.get_editor_property("events"), 1):
        time = event.get_editor_property("time")
        kind = event.get_editor_property("kind")
        boundaries[time] = f"Event{index:02d}"
        if kind in (u.StanderWeaponActionKind.MOVE, u.StanderWeaponActionKind.GUARD):
            boundaries[time + event.get_editor_property("duration")] = f"WindowEnd{index:02d}"
    name = "Windup" if boundaries and min(boundaries) > 0.001 else "Start"
    return make_phases(montage, config.get_editor_property("play_rate"), boundaries.items(), name)


def make_phases(montage, rate, boundaries, name):
    length = montage.get_play_length()
    assert math.isfinite(rate) and rate > 0 and length > 0
    blend = montage.get_editor_property("blend_out").get_editor_property("blend_time")
    result = []
    start = 0.0
    # Merge boundaries in the automatic blend-out window into the final phase.
    for end, next_name in sorted(boundaries) + [(length, "")]:
        if next_name and ((end - start) / rate < 0.002 or (length - end) / rate <= blend + 0.001):
            continue
        phase = u.AbilityTimelinePhase()
        phase.set_editor_property("name", name)
        phase.set_editor_property("source_end", end)
        phase.set_editor_property("duration", (end - start) / rate)
        result.append(phase)
        start, name = end, next_name
    return result


def legacy_records():
    """The six original sword/shield GAs keep one asset each, with per-montage variants."""
    base = BASE.rsplit("/", 1)[0]
    combat = u.load_asset(f"{base}/DA_Stander")
    folder = f"{DEST}/Sword_Shield"
    records = []
    for action in ("Combo", "Guard", "Spin", "Dash", "Lift", "ShieldBash"):
        ga_path = f"{base}/GA_Stander_{action}"
        blueprint = u.load_asset(ga_path)
        cdo = u.get_default_object(blueprint.generated_class())
        current = cdo.get_editor_property("timeline")
        name = f"DA_Stander_{action}"
        path = f"{folder}/{name}"
        timeline = u.load_asset(path) if LIB.does_asset_exist(path) else None
        assert current is None or current == timeline, ga_path
        if READBACK:
            assert current and current == timeline, ga_path
        if action == "Combo":
            strikes = list(combat.get_editor_property("combo"))
        else:
            field = {"Guard": "counter", "ShieldBash": "shield_bash"}.get(action, action.lower())
            strikes = [combat.get_editor_property(field)]
        sources = []
        for strike in strikes:
            montage = strike.get_editor_property("montage")
            boundaries = [(strike.get_editor_property("contact"), "Contact")]
            if action == "Dash":
                boundaries.extend((combat.get_editor_property(key), label)
                                  for key, label in (("dash_start", "Dash"), ("dash_end", "Recovery")))
            sources.append((montage, strike.get_editor_property("play_rate"), boundaries))
        if action == "Guard":
            montage = combat.get_editor_property("guard_montage")
            sources.append((montage, montage.get_play_length() / combat.get_editor_property("guard_duration"), []))
        if timeline is None:
            factory = u.DataAssetFactory()
            factory.set_editor_property("data_asset_class", u.AbilityTimelineDefinition)
            timeline = TOOLS.create_asset(name, folder, u.AbilityTimelineDefinition, factory)
            variants = {}
            for montage, rate, boundaries in sources:
                variant = u.AbilityTimelineMontagePhases()
                variant.set_editor_property("phases",
                                            make_phases(montage, rate, boundaries, "Windup" if boundaries else "Guard"))
                variants[montage] = variant
            timeline.set_editor_property("montage_variants", variants)
            for montage, _, _ in sources:
                assert timeline.validate_for_montage(montage, True) is not None, f"{ga_path}: {montage.get_path_name()}"
            assert LIB.save_loaded_asset(timeline, False), path
        for montage, _, _ in sources:
            assert timeline.validate_for_montage(montage, True) is not None, f"{ga_path}: {montage.get_path_name()}"
        if not READBACK and not current:
            backup = RUN / "before" / (ga_path.removeprefix("/Game/") + ".uasset")
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / "Content" / (ga_path.removeprefix("/Game/") + ".uasset"), backup)
            cdo.set_editor_property("timeline", timeline)
            u.BlueprintEditorLibrary.compile_blueprint(blueprint)
            assert u.get_default_object(blueprint.generated_class()).get_editor_property("timeline") == timeline
            assert LIB.save_loaded_asset(blueprint, False), ga_path
        records.append({"ga": ga_path, "timeline": path, "montages": [m.get_path_name() for m, _, _ in sources]})
    return records


def main():
    assert not u.EditorLevelLibrary.get_game_world(), "End PIE before authoring"
    assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(), "Save edited assets first"
    assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), "Save edited maps first"
    records = []
    configs = set()
    plan = []
    for path in sorted(LIB.list_assets(BASE, recursive=True, include_folder=False)):
        package = path.split(".")[0]
        folder, ga_name = package.rsplit("/", 1)
        if not ga_name.startswith("GA_"):
            continue
        cdo = u.get_default_object(LIB.load_blueprint_class(package))
        assert isinstance(cdo, u.StanderWeaponActionAbility), package
        config = cdo.get_editor_property("action_config")
        montage = config.get_editor_property("montage")
        config_path = config.get_path_name().split(".")[0]
        assert config_path not in configs, f"Shared config requires explicit migration: {config_path}"
        configs.add(config_path)
        family = folder.rsplit("/", 1)[-1]
        action = ga_name[3:]
        dest_folder = f"{DEST}/{family}"
        name = "DA_" + action
        target = f"{dest_folder}/{name}"
        current = config.get_editor_property("timeline")
        if READBACK:
            if not ASSETS_ONLY:
                assert current and current.get_path_name().split(".")[0] == target, package
            timeline = u.load_asset(target)
            assert timeline, target
        else:
            timeline = u.load_asset(target) if LIB.does_asset_exist(target) else None
            if current and current != timeline:
                assert current.get_path_name().split(".")[0] == f"{DEST}/DA_{family}_{action}", package
        plan.append((package, config, config_path, montage, current, timeline, dest_folder, name, target))
    assert plan, "No Standard weapon abilities found"

    for package, config, config_path, montage, current, timeline, dest_folder, name, target in plan:
        if timeline is None:
            if current:
                timeline = LIB.duplicate_asset(current.get_path_name(), target)
            else:
                factory = u.DataAssetFactory()
                factory.set_editor_property("data_asset_class", u.AbilityTimelineDefinition)
                timeline = TOOLS.create_asset(name, dest_folder, u.AbilityTimelineDefinition, factory)
                timeline.set_editor_property("phases", phases_for(config, montage))
            assert isinstance(timeline, u.AbilityTimelineDefinition), target
            validation = timeline.validate_for_montage(montage)
            assert validation is not None, f"{package}: timeline validation failed"
            assert LIB.save_loaded_asset(timeline, False), target
        validation = timeline.validate_for_montage(montage)
        assert validation is not None, f"{package}: timeline validation failed"
        if not READBACK and not ASSETS_ONLY and current != timeline:
            backup = RUN / "before" / (config_path.removeprefix("/Game/") + ".uasset")
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / "Content" / (config_path.removeprefix("/Game/") + ".uasset"), backup)
            config.set_editor_property("timeline", timeline)
            assert LIB.save_loaded_asset(config, False), config_path
        records.append({
            "ga": package, "config": config_path, "montage": montage.get_path_name(), "timeline": target,
            "phases": [{"name": str(p.get_editor_property("name")), "source_end": p.get_editor_property("source_end"),
                        "seconds": p.get_editor_property("duration")} for p in timeline.get_editor_property("phases")],
        })
    if not ASSETS_ONLY:
        records.extend(legacy_records())
    player = u.get_default_object(LIB.load_blueprint_class("/Game/BluePrints/Characters/Stander/BP_StanderPlayer"))
    startup = [a.get_path_name() for a in player.get_editor_property("startup_ability") if a]
    (RUN / "report.json").write_text(json.dumps({"readback": READBACK, "startup": startup, "abilities": records},
                                               ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"STANDER_TIMELINES_OK {len(records)} {RUN}")


main()
