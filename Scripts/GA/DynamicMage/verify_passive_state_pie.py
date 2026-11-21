"""Real ArcaneMeditation T input + registered Applied->area-state fragment.

Requires the trusted Append.Registered/Area.Petrify template and exact Close GUID
ack ABI. Missing templates remain a reported failure/pending capability; this
script never substitutes a direct status application or a baked ability graph.
"""
import time
from pathlib import Path
import unreal as u

q = {"_DYNAMIC_MAGE_LIBRARY_ONLY": True, "__name__": "dynamic_mage_passive_qa"}
exec((Path(u.Paths.project_dir()) / "Scripts/GA/DynamicMage/verify_real_casting_pie.py").read_text(encoding="utf-8"), q)
s = q["state"]
s.update(scenario="PassiveOwnedGEAndRegisteredAppliedState", scope="Actual passive local T input; trusted registered Applied fragment; owner-only close and both-world state.",
         limitations=["Requires registered Area.Petrify template; no fallback injection", "Command route does not certify UMG", "Visual quality remains user acceptance"])


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
        asc = q["component"](server, u.MyAbilitySystemComponent)
        client_asc = q["component"](local, u.MyAbilitySystemComponent)
        ls, ss = q["preview"](local), q["preview"](server)
        if s["phase"] == "setup":
            s["expected_restored_current"] = {"mana_regeneration": q["attribute"](asc, "mana_regeneration")}
            if not q["setup_fixture"]((300, 450, 1200)):
                return
            q["request_begin"]("Abilities.Mage.ArcaneMeditation")
        elif s["phase"] == "begin":
            if not q["preview_started"](local, server):
                return
            index, kind = q["first_node"](local, {"Status.OwnedEffect"})
            s["owned_effect_node"] = {"index": index, "kind": kind}
            s["revision_before"] = int(ls.get_editor_property("revision"))
            q["command"](local, f"Append Area.Petrify {index} Applied 2 600")
            q["phase"]("edit")
        elif s["phase"] == "edit":
            if str(ls.get_editor_property("error")):
                raise AssertionError("Required registered Applied payload is not available: " + str(ls.get_editor_property("error")))
            if int(ls.get_editor_property("revision")) <= s["revision_before"] or int(ls.get_editor_property("revision")) != int(ss.get_editor_property("revision")):
                return
            replicas = q["ready_replicas"]()
            if not replicas:
                return
            s["before_health"] = q["healths"](q["owned"])
            s["before_regen"] = q["attribute"](asc, "mana_regeneration")
            s["server_status_seen"] = [False, False, False]
            s["client_status_seen"] = [False, False, False]
            s["revision"] = int(ss.get_editor_property("revision"))
            s["observation_sequence"] = max([int(r.get_editor_property("sequence")) for r in q["records"](server)] + [0])
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
            regen = q["attribute"](asc, "mana_regeneration")
            client_regen = q["attribute"](client_asc, "mana_regeneration")
            s["samples"].append({"time": age, "authority_regen": regen, "client_regen": client_regen,
                                  "authority_health": q["healths"](q["owned"]),
                                  "client_health": q["healths"](replicas) if len(replicas) == 3 else []})
            if age < 15:
                return
            q["check"]("original_passive_modifier_remains_after_15_seconds", abs(regen - s["before_regen"] - 2) < .01)
            q["check"]("original_passive_modifier_replicates_to_owner", abs(client_regen - regen) < .01)
            q["check"]("registered_applied_state_affects_authority_inside_lives", all(s["server_status_seen"][i] for i in (0, 1)) and not s["server_status_seen"][2])
            q["check"]("registered_applied_state_replicates_to_owner_inside_lives", all(s["client_status_seen"][i] for i in (0, 1)) and not s["client_status_seen"][2])
            q["check"]("state_fragment_has_no_hidden_damage", q["healths"](q["owned"]) == s["before_health"])
            rows = q["new_records"](server, s["observation_sequence"])
            q["check"]("accepted_runtime_revision_really_executed", any(int(r.get_editor_property("revision")) == s["revision"] for r in rows))
            if other:
                q["check"]("other_player_recipe_remains_isolated", q["preview"](other).export_text() == s["other_preview_before"])
            q["finish"]()
    except Exception as error:
        q["finish"](repr(error))


q["handle"] = u.register_slate_post_tick_callback(tick)
q["write"]()
u.log("DYNAMIC_MAGE_PASSIVE_STARTED " + str(q["run"] / "report.json"))
