"""Actual native FireBolt with selected world radius/count, Standalone or Listen.

This is a separate scene from sequential propagation: all independent native
arrows hit the first original life. Original T/cursor/TargetData/Notify and costs
are used. The client must receive the requested value, not just match its own
class default. No client sphere property is mutated by this script.
"""
import time
from pathlib import Path
import unreal as u

q = {"_DYNAMIC_MAGE_LIBRARY_ONLY": True, "__name__": "dynamic_mage_native_radius_qa"}
exec((Path(u.Paths.project_dir()) / "Scripts/GA/DynamicMage/verify_real_casting_pie.py").read_text(encoding="utf-8"), q)
s = q["state"]
radius = float(globals().get("DYNAMIC_MAGE_NATIVE_RADIUS", 64.0))
count = int(globals().get("DYNAMIC_MAGE_NATIVE_COUNT", 3))
s.update(scenario="NativeSelectedRadiusAndPreparedCount", requested_radius_cm=radius, requested_count=count,
         scope="Actual native FireBolt T/cursor/Notify; independent Await receipts; selected server/client sphere geometry.",
         limitations=["Command bridge does not certify UMG", "No visual quality certification", "This scene has no Sequential replacement"])


def queue_edit(local):
    modifier, index, value = s["edits"].pop(0)
    s["revision_before"] = int(q["preview"](local).get_editor_property("revision"))
    q["command"](local, f"Edit {modifier} {index} {value}")
    q["phase"]("edit")


def tick(_delta):
    try:
        if s["phase"].startswith("cleanup"):
            q["cleanup_tick"]()
            return
        if time.monotonic() - s["started"] > 100:
            raise AssertionError("Timed out in " + s["phase"])
        local, server, other = q["participants"]()
        if not local or not server:
            return
        if q["pinned"]:
            q["check"]("pinned_participant_lives_unchanged", q["same_participant_lives"]())
        age = time.monotonic() - s.get("at", s["started"])
        ls, ss = q["preview"](local), q["preview"](server)
        if s["phase"] == "setup":
            if not q["setup_fixture"]((250, 450, 650)):
                return
            # A larger sphere needs a real clear vertical corridor as well.
            # These are only this run's owned disabled AI fixtures; their
            # original combat components and replication stay intact.
            for actor in q["owned"]:
                actor.set_actor_location(actor.get_actor_location() + u.Vector(0, 0, 100), False, True)
                actor.force_net_update()
            q["request_begin"]("Abilities.Fire.FireBolt")
        elif s["phase"] == "begin":
            if not q["preview_started"](local, server):
                return
            delivery = q["node_index"](local, "Delivery.ProjectileAwait")
            scatter = q["node_index"](local, "Distribution.Scatter")
            s["edits"] = [("Set.CollisionRadius", delivery, radius), ("Set.Count", scatter, count), ("Set.Spread", scatter, 0)]
            queue_edit(local)
        elif s["phase"] == "edit":
            if str(ls.get_editor_property("error")):
                raise AssertionError("Actual selected native parameter rejected: " + str(ls.get_editor_property("error")))
            if int(ls.get_editor_property("revision")) <= s["revision_before"] or int(ls.get_editor_property("revision")) != int(ss.get_editor_property("revision")):
                return
            if s["edits"]:
                queue_edit(local)
                return
            s["revision"] = int(ss.get_editor_property("revision"))
            s["sequence"] = max([int(r.get_editor_property("sequence")) for r in q["records"](server)] + [0])
            s["before_health"] = q["healths"](q["owned"])
            s["geometry"] = {"authority": [], "client": []}
            q["phase"]("aim")
        elif s["phase"] == "aim":
            replicas = q["ready_replicas"]()
            if not replicas or any(abs(a.get_actor_location().z-b.get_actor_location().z) > 1 for a,b in zip(q["owned"], replicas)):
                return
            q["aim_at"](replicas[0])
            if age >= .5:
                q["command"](local, "Press")
                q["phase"]("press")
        elif s["phase"] == "press":
            if age >= .12:
                q["command"](local, "Release")
                q["phase"]("observe")
        elif s["phase"] == "observe":
            for key, pc in (("authority", server), ("client", local)):
                for actor in u.GameplayStatics.get_all_actors_of_class(pc.get_world(), u.MyProjectile):
                    if actor.get_owner() == pc.get_controlled_pawn():
                        sphere = actor.get_component_by_class(u.SphereComponent)
                        if sphere:
                            s["geometry"][key].append({"radius_cm": sphere.get_scaled_sphere_radius(), "class": actor.get_class().get_path_name(), "position": str(actor.get_actor_location())})
            rows = q["new_records"](server, s["sequence"])
            hits = [r for r in rows if r.get_editor_property("confirmed_hit") and str(r.get_editor_property("node_type")) == "Delivery.ProjectileAwait" and str(r.get_editor_property("stage")) == "Hit"]
            s["samples"].append({"time": age, "physical_hits": len(hits), "health": q["healths"](q["owned"])})
            if len(hits) < count or age < 2.5:
                return
            q["check"]("actual_native_independent_physical_receipts_equal_prepared_count", len(hits) == count)
            q["check"]("independent_arrows_share_one_original_target_life", len({str(r.get_editor_property("target_label")) for r in hits}) == 1)
            q["check"]("independent_arrows_share_one_frozen_root", len({q["guid_hex"](r.get_editor_property("root_cast_id")) for r in hits}) == 1)
            hp = q["healths"](q["owned"])
            q["check"]("only_first_original_life_receives_native_payload", hp[0] < s["before_health"][0] and hp[1:] == s["before_health"][1:])
            costs = [r for r in rows if r.get_editor_property("kind") == u.AbilityRuntimeObservationKind.COST]
            q["check"]("one_real_frontend_cost_for_all_native_arrows", sum(r.get_editor_property("cost_succeeded") and r.get_editor_property("resource_after") < r.get_editor_property("resource_before") for r in costs) == 1)
            q["check"]("authority_and_client_both_observed_real_projectiles", all(s["geometry"].values()))
            q["check"]("authority_received_requested_world_radius", all(abs(g["radius_cm"]-radius) < .01 for g in s["geometry"]["authority"]))
            q["check"]("client_received_requested_world_radius_not_default", all(abs(g["radius_cm"]-radius) < .01 for g in s["geometry"]["client"]))
            replicas = q["native_targets"](local.get_world(), local.get_controlled_pawn())
            q["check"]("actual_native_target_health_converges", len(replicas) == 3 and all(abs(a-b) < .01 for a,b in zip(hp, q["healths"](replicas))))
            if other:
                q["check"]("other_player_recipe_isolated", q["preview"](other).export_text() == s["other_preview_before"])
            q["finish"]()
    except Exception as error:
        q["finish"](repr(error))


q["handle"] = u.register_slate_post_tick_callback(tick)
q["write"]()
u.log("DYNAMIC_MAGE_NATIVE_RADIUS_STARTED " + str(q["run"] / "report.json"))
