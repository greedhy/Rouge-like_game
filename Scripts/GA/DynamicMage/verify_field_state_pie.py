"""Actual local Field input + accepted damage-result status edit, Standalone/2-player Listen.

Loads only the Fire test's fixture/cleanup library; never activates its scenario.
No TargetData, Notify, damage or private ability callback is injected. Exact
close-ack native ABI and the current real default exporter must be built first.
"""
import time
from pathlib import Path
import unreal as u

q = {"_DYNAMIC_MAGE_LIBRARY_ONLY": True, "__name__": "dynamic_mage_field_qa"}
exec((Path(u.Paths.project_dir()) / "Scripts/GA/DynamicMage/verify_real_casting_pie.py").read_text(encoding="utf-8"), q)
s = q["state"]
identity = globals().get("DYNAMIC_MAGE_FIELD_IDENTITY", "Abilities.Prototype.Field")
follow = identity == "Abilities.Batch6.StarFlameHalo"
s.update(scenario="FollowFieldDamageAndAppendedState" if follow else "FieldDamageAndAppendedState", scope="Actual local T/cursor/TargetData; remote RPC; real Actor GE and replicated field/status.",
         limitations=["Not OS keyboard injection", "Command bridge does not certify UMG interactions", "Visual quality remains user acceptance"])


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
            if not q["setup_fixture"]((100, 200, 1200) if follow else (300, 450, 1200)):
                return
            q["request_begin"](identity)
        elif s["phase"] == "begin":
            if not q["preview_started"](local, server):
                return
            q["check"]("same_authority_base_acknowledged", ls.get_editor_property("base_hash") == ss.get_editor_property("base_hash"))
            scope, scope_kind = q["first_node"](local, {"Scope.Prototype"})
            selected = str(ls.get_editor_property("selected_id"))
            row = next(r for r in q["dev"](local).get_definitions() if str(r.get_editor_property("definition_id")) == selected)
            original_scope = row.get_editor_property("base").get_editor_property("default_graph").get_editor_property("nodes")[scope]
            s["original_base_radius"] = float(original_scope.get_editor_property("config").get_editor_property("parameters").get_editor_property("radius"))
            s["requested_radius"] = 450.0
            q["check"]("requested_radius_differs_from_original_asset", abs(s["original_base_radius"] - s["requested_radius"]) > .01)
            s["revision_before"] = int(ls.get_editor_property("revision"))
            q["command"](local, f"Edit Set.Radius {scope} {s['requested_radius']}")
            q["phase"]("radius")
        elif s["phase"] == "radius":
            if str(ls.get_editor_property("error")):
                raise AssertionError("Actual radius edit rejected: " + str(ls.get_editor_property("error")))
            if int(ls.get_editor_property("revision")) <= s["revision_before"] or int(ls.get_editor_property("revision")) != int(ss.get_editor_property("revision")):
                return
            q["check"]("actual_changed_radius_edit_accepted", True)
            index, kind = q["first_node"](local, {"Effect.PrototypeDamageBatch"})
            s["payload_node"] = {"index": index, "kind": kind}
            s["revision_before"] = int(ls.get_editor_property("revision"))
            s["append_state_sequence"] = int(ls.get_editor_property("state_sequence"))
            q["command"](local, f"Edit Append.Status.Petrify {index} 1.5")
            q["phase"]("edit")
        elif s["phase"] == "edit":
            error = str(ls.get_editor_property("error"))
            if int(ls.get_editor_property("state_sequence")) > s["append_state_sequence"] and error:
                s["actual_append_rejection"] = error
                raise AssertionError("Actual field payload append rejected: " + error)
            if int(ls.get_editor_property("revision")) <= s["revision_before"] or int(ls.get_editor_property("revision")) != int(ss.get_editor_property("revision")):
                return
            q["check"]("field_status_edit_accepted", not str(ls.get_editor_property("error")))
            replicas = q["ready_replicas"]()
            if not replicas:
                return
            q["aim_at"](replicas[0])
            s["revision"] = int(ss.get_editor_property("revision"))
            s["observation_sequence"] = max([int(r.get_editor_property("sequence")) for r in q["records"](server)] + [0])
            s["before_health"] = q["healths"](q["owned"])
            s["before_mana"] = q["attribute"](q["component"](server, u.MyAbilitySystemComponent), "mana")
            s["server_status_seen"] = [False, False, False]
            s["client_status_seen"] = [False, False, False]
            s["geometry"] = {"server": [], "client": []}
            q["phase"]("aim")
        elif s["phase"] == "aim":
            replicas = q["ready_replicas"]()
            if not replicas:
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
            replicas = q["native_targets"](local.get_world(), local.get_controlled_pawn())
            for key, actors in (("server_status_seen", q["owned"]), ("client_status_seen", replicas)):
                if len(actors) == 3:
                    for index, actor in enumerate(actors):
                        s[key][index] |= q["petrified"](actor)
            for key, controller in (("server", server), ("client", local)):
                for actor in q["effect_hosts"](controller):
                    if isinstance(actor, (u.PrototypeRuntime, u.PureMassRuntime)):
                        # The public readback returns -1 until the frozen
                        # snapshot is ready; it exposes no private frame.
                        radius = float(actor.get_presentation_radius())
                        if radius >= 0 and len(s["geometry"][key]) < 100:
                            s["geometry"][key].append({"actor": actor.get_path_name(), "radius_cm": radius,
                                "position": str(actor.get_actor_location()), "direction": str(actor.get_actor_forward_vector()),
                                "niagara_components": len(actor.get_components_by_class(u.NiagaraComponent))})
            hp = q["healths"](q["owned"])
            s["samples"].append({"time": age, "server_health": hp,
                                  "client_health": q["healths"](replicas) if len(replicas) == 3 else []})
            if age < 8:
                return
            q["check"]("original_field_really_damages_both_inside_lives", all(hp[i] < s["before_health"][i] for i in (0, 1)))
            q["check"]("outside_life_never_damaged", hp[2] == s["before_health"][2])
            q["check"]("real_status_seen_on_both_inside_authority_lives", all(s["server_status_seen"][i] for i in (0, 1)) and not s["server_status_seen"][2])
            q["check"]("real_status_replicates_to_both_inside_client_lives", all(s["client_status_seen"][i] for i in (0, 1)) and not s["client_status_seen"][2])
            q["check"]("health_converges_on_both_worlds", len(replicas) == 3 and all(abs(a-b) < .01 for a,b in zip(hp, q["healths"](replicas))))
            q["check"]("both_worlds_received_frozen_field_geometry", bool(s["geometry"]["server"]) and bool(s["geometry"]["client"]))
            q["check"]("both_worlds_received_changed_radius_instead_of_default_asset", all(abs(p["radius_cm"] - s["requested_radius"]) < .01 for side in ("server", "client") for p in s["geometry"][side]))
            q["check"]("field_radius_snapshot_is_shared_not_transient_DA_replication", {p["radius_cm"] for p in s["geometry"]["server"]} == {p["radius_cm"] for p in s["geometry"]["client"]})
            rows = q["new_records"](server, s["observation_sequence"])
            q["check"]("actual_original_frontend_cost_observed_once", sum(r.get_editor_property("kind") == u.AbilityRuntimeObservationKind.COST and r.get_editor_property("cost_succeeded") for r in rows) == 1)
            q["check"]("real_field_hits_keep_accepted_revision", any(r.get_editor_property("damage_attempted") and int(r.get_editor_property("revision")) == s["revision"] for r in rows))
            if follow:
                q["check"]("original_follow_toggle_still_exists_after_authored_finite_duration", bool(q["effect_hosts"](server)))
                q["check"]("real_follow_upkeep_consumed_additional_mana", q["attribute"](q["component"](server, u.MyAbilitySystemComponent), "mana") < s["before_mana"] - 15)
            if other:
                q["check"]("host_other_player_recipe_isolated", q["preview"](other).export_text() == s["other_preview_before"])
            q["finish"]()
    except Exception as error:
        q["finish"](repr(error))


q["handle"] = u.register_slate_post_tick_callback(tick)
q["write"]()
u.log("DYNAMIC_MAGE_FIELD_STARTED " + str(q["run"] / "report.json"))
