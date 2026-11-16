"""Bounded asset contract shared by GuardParry authoring and independent readback."""
import datetime as dt
import hashlib
import json
from pathlib import Path
import uuid
import unreal as u


ROOT = Path(u.Paths.project_dir()).resolve()
BASE = "/Game/BluePrints/Characters/Stander"
FAMILIES = ("Bow", "DoubleBlades", "DualBlades", "Fighter", "Hammer", "Magic",
            "Pistol", "Rapier", "Spear", "Staff", "Sword_Shield")
FALLBACK = BASE + "/DA_Stander"
PAWN = BASE + "/BP_StanderPlayer"
FALLBACK_GA = BASE + "/GA_Stander_Guard"
INPUT = "/Game/BluePrints/Input/DA_MyInputConfig"
CONTEXT = "/Game/BluePrints/Input/IMC_PlayerContext"
ACTIONS = tuple(BASE + "/Weapons/" + family + "/DA_Block" for family in FAMILIES)
WHITELIST = frozenset((FALLBACK, *ACTIONS))
VALUES = {"enabled": True, "window": .15, "melee_stun_duration": 1.,
          "reward_world_slow": True, "reward_lock": 3.}
SLOW_VALUES = {"scale": .2, "blend_in": .05, "hold": 1.2, "blend_out": .15, "compensate_source": True}
ACTION_FIELDS = ("display_name", "weapon_family", "action_tag", "cooldown_tag", "montage",
                 "play_rate", "timeline", "mana_cost", "cooldown")
EVENT_FIELDS = ("time", "kind", "damage", "radius", "angle", "death_impulse", "projectile_speed",
                "projectile_lifetime", "launch_yaw", "move_distance", "move_height", "duration",
                "socket_name", "effect")
FALLBACK_FIELDS = ("combo", "spin", "guard_montage", "combo_reset", "guard_duration", "guard_cooldown",
                   "spin_cooldown", "spin_mana", "hit_effect", "combo_death_impulses", "counter_death_impulse",
                   "spin_death_impulse", "dash_death_impulse", "lift_death_impulse", "shield_bash_death_impulse",
                   "lift", "shield_bash", "lift_mana", "lift_cooldown", "shield_bash_mana", "shield_bash_cooldown",
                   "counter", "dash", "dash_distance", "dash_start", "dash_end", "dash_cooldown", "dash_mana",
                   "swing_effect")
ABILITY_FIELDS = ("startup_input_tag", "ability_tags", "action_config",
                  "cost_gameplay_effect_class", "cooldown_gameplay_effect_class")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def prop(obj, name):
    require(obj is not None, "Null object reading " + name)
    return obj.get_editor_property(name)


def path(obj):
    return obj.get_path_name() if obj else None


def package(name):
    return name.split(".")[0]


def object_path(name):
    return name + "." + name.rsplit("/", 1)[1]


def describe(value):
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    if hasattr(value, "get_path_name"):
        return value.get_path_name()
    if hasattr(value, "export_text"):
        return value.export_text()
    try:
        return [describe(item) for item in value]
    except TypeError:
        return str(value)


def fields(obj, names):
    return {name: describe(prop(obj, name)) for name in names}


def parry_fields(settings):
    result = fields(settings, VALUES)
    result["reward"] = fields(prop(settings, "reward"), SLOW_VALUES)
    return result


def load(name):
    asset = u.load_asset(name)
    require(asset, "Missing asset: " + name)
    return asset


def cdo(name):
    cls = u.EditorAssetLibrary.load_blueprint_class(name)
    require(cls, "Missing Blueprint class: " + name)
    return u.get_default_object(cls)


def disk_path(name):
    require(name.startswith("/Game/"), "Unexpected package: " + name)
    return ROOT / "Content" / (package(name)[len("/Game/"):] + ".uasset")


def sha256(file):
    require(file.is_file(), "Missing file: " + str(file))
    return hashlib.sha256(file.read_bytes()).hexdigest()


def hashes(names):
    return {name: sha256(disk_path(name)) for name in sorted(names)}


def dirty_packages():
    return sorted(obj.get_name() for obj in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
                  list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))


def new_report(kind):
    name = kind + "-" + dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S") + "-" + uuid.uuid4().hex[:8]
    result = ROOT / "Saved/GAAuthoring/GuardParry" / name
    result.mkdir(parents=True, exist_ok=False)
    return result


def write_json(file, result):
    file.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")


def read_inspection(name):
    file = Path(name)
    require(file.is_file() and file.name == "inspection.json", "Set GUARD_PARRY_INSPECTION to inspection.json")
    result = json.loads(file.read_text(encoding="utf-8"))
    require(result["success"] and not result["errors"], "Inspection was not successful")
    require(not result["dirty_before"] and not result["dirty_after"], "Inspection had dirty packages")
    expected = {object_path(BASE + "/Weapons/" + family + "/DA_WeaponSet") for family in FAMILIES}
    require(set(result["weapon_sets"]) == expected, "Inspected weapon set list differs from reviewed scope")
    require(result["pawn"]["sword_config"] == object_path(FALLBACK), "Fallback SwordConfig changed")
    return result


def snapshot():
    result = {"actions": {}, "weapon_sets": {}, "abilities": {}, "timelines": {}, "parry": {}}
    protected = {PAWN, FALLBACK_GA, INPUT, CONTEXT}
    for family, action_name in zip(FAMILIES, ACTIONS):
        set_name = BASE + "/Weapons/" + family + "/DA_WeaponSet"
        weapon_set = load(set_name)
        result["weapon_sets"][object_path(set_name)] = fields(weapon_set, ("family", "abilities"))
        rmb = [entry for entry in prop(weapon_set, "abilities")
               if 'TagName="InputTag.RMB"' in prop(entry, "input_tag").export_text()]
        require(len(rmb) == 1, "Expected one RMB mapping: " + set_name)
        ability_class = prop(rmb[0], "ability")
        require(ability_class, "RMB has no ability: " + set_name)
        defaults = u.get_default_object(ability_class)
        action = prop(defaults, "action_config")
        require(path(action) == object_path(action_name), "RMB ActionConfig changed: " + set_name)
        require(path(action.get_class()) == "/Script/test_25d.StanderWeaponActionConfig", "Unexpected action class")
        result["abilities"][set_name] = {"ability_class": path(ability_class), "ability": fields(defaults, ABILITY_FIELDS)}
        events = list(prop(action, "events"))
        require(len(events) == 1 and prop(events[0], "kind") == u.StanderWeaponActionKind.GUARD,
                "Reviewed RMB Guard event topology changed: " + action_name)
        result["actions"][action_name] = fields(action, ACTION_FIELDS)
        result["actions"][action_name]["events"] = [fields(event, EVENT_FIELDS) for event in events]
        result["parry"][action_name] = [parry_fields(prop(event, "parry")) for event in events]
        for event in events:
            require(float(prop(event, "duration")) >= VALUES["window"], "Parry exceeds Guard duration")
        timeline = prop(action, "timeline")
        require(timeline, "Reviewed action lost Timeline: " + action_name)
        result["timelines"][path(timeline)] = fields(timeline, ("events", "phases"))
        protected.update((set_name, package(path(ability_class)), package(path(timeline)), package(path(prop(action, "montage")))))
    pawn = cdo(PAWN)
    result["pawn"] = fields(pawn, ("startup_ability", "startup_passive_ability", "sword_config"))
    require(result["pawn"]["sword_config"] == object_path(FALLBACK), "Pawn fallback config changed")
    fallback = load(FALLBACK)
    require(path(fallback.get_class()) == "/Script/test_25d.StanderCombatConfig", "Unexpected fallback class")
    result["fallback_config"] = fields(fallback, FALLBACK_FIELDS)
    result["parry"][FALLBACK] = parry_fields(prop(fallback, "guard_parry"))
    fallback_ga = cdo(FALLBACK_GA)
    result["fallback_ability"] = fields(fallback_ga, ("startup_input_tag", "ability_tags", "timeline",
                                                      "cost_gameplay_effect_class", "cooldown_gameplay_effect_class"))
    fallback_timeline = prop(fallback_ga, "timeline")
    require(fallback_timeline, "Fallback lost Timeline")
    result["timelines"][path(fallback_timeline)] = fields(fallback_timeline, ("events", "phases"))
    protected.update((package(path(fallback_timeline)), package(path(prop(fallback, "guard_montage")))))
    input_config = load(INPUT)
    result["input_rows"] = describe(prop(input_config, "ability_input_actions"))
    mappings = list(prop(prop(load(CONTEXT), "default_key_mappings"), "mappings"))
    result["mappings"] = describe(mappings)
    result["rmb_input_rows"] = [describe(row) for row in prop(input_config, "ability_input_actions")
                                if 'TagName="InputTag.RMB"' in prop(row, "input_tag").export_text()]
    result["right_mouse_mappings"] = [describe(row) for row in mappings if prop(row, "key").export_text() == "RightMouseButton"]
    result["protected_hashes"] = hashes(protected)
    return result


def validate_inspection(inspected, actual):
    for set_path, row in inspected["weapon_sets"].items():
        set_name = package(set_path)
        require(actual["weapon_sets"][set_path] == {key: row[key] for key in ("family", "abilities")},
                "Weapon routing differs from inspection: " + set_path)
        require(actual["protected_hashes"][set_name] == row["sha256"], "WeaponSet disk changed since inspection")
        require(len(row["rmb"]) == 1, "Inspection had ambiguous RMB")
        observed = row["rmb"][0]
        require(actual["abilities"][set_name] == {key: observed[key] for key in ("ability_class", "ability")},
                "RMB ability differs from inspection: " + set_name)
        action_name = package(observed["ability"]["action_config"])
        old_action = {key: observed["action"][key] for key in ACTION_FIELDS}
        # Import old event text into a detached value to compare original fields after native struct expansion.
        old_events = []
        for text in observed["action"]["events"]:
            event = u.StanderWeaponActionEvent()
            event.import_text(text)
            old_events.append(fields(event, EVENT_FIELDS))
        old_action["events"] = old_events
        require(actual["actions"][action_name] == old_action, "Original action parameters changed: " + action_name)
        timeline_path = observed["action"]["timeline"]
        require(actual["timelines"][timeline_path] == observed["timeline"], "Action Timeline differs from inspection")
    for key in ("pawn", "fallback_ability", "rmb_input_rows", "right_mouse_mappings"):
        require(actual[key] == inspected[key], "Inspection contract changed: " + key)
    for key, value in inspected["fallback_config"].items():
        require(actual["fallback_config"][key] == value, "Fallback parameter changed: " + key)


def matches_fields(values, expected_values):
    return all(values[key] == expected if isinstance(expected, bool) else abs(values[key] - expected) < 1.e-6
               for key, expected in expected_values.items())


def matches(values):
    return matches_fields(values, VALUES) and matches_fields(values["reward"], SLOW_VALUES)


def settings():
    value = u.GuardParrySettings()
    for name, expected in VALUES.items():
        value.set_editor_property(name, expected)
    reward = prop(value, "reward")
    for name, expected in SLOW_VALUES.items():
        reward.set_editor_property(name, expected)
    value.set_editor_property("reward", reward)
    require(matches(parry_fields(value)), "Native Parry settings failed roundtrip; full Editor build required")
    return value


def validate_authored(result):
    for name, values in result["parry"].items():
        for row in values if isinstance(values, list) else [values]:
            require(matches(row), "Parry settings differ: " + name)


def validate_preserved(before, after):
    require({key: value for key, value in before.items() if key != "parry"} ==
            {key: value for key, value in after.items() if key != "parry"},
            "Original configuration, routing, montage, Timeline or protected package changed")
