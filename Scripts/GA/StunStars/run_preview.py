"""Exercise real stun GE in the transient preview, capture early/late/recovered frames."""
import json
from pathlib import Path
import unreal as u

out = Path(u.Paths.project_dir()) / "Saved/GAAuthoring/StunStars/20261116"
world = u.EditorLevelLibrary.get_game_world()
assert world and world.get_path_name().startswith("/Temp/")
pc = u.GameplayStatics.get_player_controller(world, 0)
pawn = pc.get_controlled_pawn()
pawn.set_actor_enable_collision(False)
pawn.set_actor_location(u.Vector(600, 0, 320), False, True)
pc.set_control_rotation(u.Rotator(pitch=-17, yaw=180, roll=0))
u.SystemLibrary.execute_console_command(world, "viewmode unlit", pc)
enemies = []
for index, name in enumerate(["Goblin", "Ghoul", "StoneGolem", "Grux"]):
    actor = next(a for a in u.GameplayStatics.get_all_actors_of_class(world, u.EnemyCharacter) if a.actor_has_tag(name))
    capsule = actor.get_component_by_class(u.CapsuleComponent)
    actor.set_actor_location(u.Vector(0, index * 260 - 390, capsule.get_scaled_capsule_half_height()), False, True)
    actor.get_component_by_class(u.CharacterMovementComponent).set_movement_mode(u.MovementMode.MOVE_NONE)
    enemies.append(actor)
for actor in u.GameplayStatics.get_all_actors_of_class(world, u.StaticMeshActor):
    if actor.actor_has_tag("StunStarsPreview"):
        actor.set_actor_location(u.Vector(0, 0, -5), False, True)
started = u.GameplayStatics.get_time_seconds(world)
report = {"frames": [], "finished": False}
last_apply = -10
next_frame = 0


def sample(stage):
    rows = []
    for actor in enemies:
        stars = actor.get_editor_property("stun_debuff_component")
        anim = actor.get_component_by_class(u.SkeletalMeshComponent).get_anim_instance()
        rows.append({"actor": actor.get_actor_label(), "active": stars.is_active(),
                     "location": str(stars.get_world_location()), "scale": str(stars.get_world_scale()),
                     "anim_class": anim.get_class().get_path_name() if anim else None,
                     "stunned": actor.get_editor_property("bIsStunned")})
    report["frames"].append({"stage": stage, "world_time": u.GameplayStatics.get_time_seconds(world), "actors": rows})
    u.SystemLibrary.execute_console_command(world, 'HighResShot 1600x900 filename="' + str(out / (stage + ".png")) + '"', pc)


def tick(dt):
    global last_apply, next_frame
    try:
        age = u.GameplayStatics.get_time_seconds(world) - started
        if age < 6 and age - last_apply >= .6:
            for actor in enemies:
                asc = actor.get_component_by_class(u.MyAbilitySystemComponent)
                asc.apply_gameplay_effect_to_self(u.GuardParryStunEffect, 1., asc.make_effect_context())
            last_apply = age
        schedule = [(2., "stars-active"), (5., "stars-long-stun"), (8., "stars-recovered")]
        if next_frame < len(schedule) and age >= schedule[next_frame][0]:
            sample(schedule[next_frame][1])
            next_frame += 1
        if age >= 9:
            report["finished"] = True
            report["passed"] = all(a["active"] and a["stunned"] for f in report["frames"][:2] for a in f["actors"])
            report["passed"] &= all(not a["active"] and not a["stunned"] for a in report["frames"][2]["actors"])
            u.unregister_slate_post_tick_callback(handle)
        (out / "preview-runtime.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    except Exception as error:
        report["error"] = str(error)
        (out / "preview-runtime.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        u.unregister_slate_post_tick_callback(handle)
        raise


handle = u.register_slate_post_tick_callback(tick)
