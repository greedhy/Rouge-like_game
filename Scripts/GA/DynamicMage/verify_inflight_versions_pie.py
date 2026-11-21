"""Three actual casts on one preview lease: damage chain, status chain, default.

Edit the unique damage owner after the first real contact; reset while the next
status-only chain is physically in flight. Old roots must retain their accepted
version. Original cooldown and old Burn GEs expire naturally; no packet/Notify,
GE removal, direct damage or private GA callback is injected.
"""
import time
from pathlib import Path
import unreal as u

q = {"_DYNAMIC_MAGE_LIBRARY_ONLY": True, "__name__": "dynamic_mage_version_qa"}
exec((Path(u.Paths.project_dir()) / "Scripts/GA/DynamicMage/verify_real_casting_pie.py").read_text(encoding="utf-8"), q)
s = q["state"]
s.update(scenario="RealInFlightEditAndWithdrawal", scope="Same real player/preview lease; native T/cursor/TargetData/Notify; physical old and new snapshot roots.",
         limitations=["Command bridge does not certify UMG", "Original optional Burn is allowed and expires naturally", "Visual quality remains user acceptance"])
cache = {}


def has_owned_tag(asc, name):
    # Burn is a granted/owned tag, not an effect asset tag. Query the actual
    # ASC tag counts while allowing the original DOT GE to expire naturally.
    tags = u.GameplayTagContainer()
    tags.import_text('(GameplayTags=((TagName="' + name + '")))')
    return bool(asc.has_all_matching_gameplay_tags(tags))


def original_frontend_active(pc):
    # The public read-only gate requires the precise replicated owned grant,
    # its developer slot and original cooldown. No private ASC reflection or
    # invalid client-only handle is taken as permission to cast.
    return not q["dev"](pc).is_preview_frontend_idle()


def naturally_ready(local, server):
    for pc in {local, server}:
        if original_frontend_active(pc) or has_owned_tag(q["component"](pc, u.MyAbilitySystemComponent), "Cooldown.Fire.FireBolt"):
            return False
    return not any(has_owned_tag(a.get_component_by_class(u.MyAbilitySystemComponent), "Debuff.Burn") for a in q["owned"])


def collect(server):
    for record in q["records"](server):
        sequence = int(record.get_editor_property("sequence"))
        cache[sequence] = dict(sequence=sequence, kind=str(record.get_editor_property("kind")),
            stage=str(record.get_editor_property("stage")), node=str(record.get_editor_property("node_type")),
            confirmed=bool(record.get_editor_property("confirmed_hit")), paid=bool(record.get_editor_property("cost_succeeded")),
            root=q["guid_hex"](record.get_editor_property("root_cast_id")), revision=int(record.get_editor_property("revision")),
            frozen_hash=str(record.get_editor_property("frozen_hash")), server_seconds=float(record.get_editor_property("server_seconds")))
    if q["owned"]:
        sample = dict(phase=s["phase"], game_seconds=float(u.GameplayStatics.get_time_seconds(server.get_world())),
                      health=q["healths"](q["owned"]),
                      owned_burn=[has_owned_tag(a.get_component_by_class(u.MyAbilitySystemComponent), "Debuff.Burn") for a in q["owned"]])
        history = s.setdefault("target_state_changes", [])
        if not history or any(sample[key] != history[-1][key] for key in ("phase", "health", "owned_burn")):
            history.append(sample)
            del history[:-128]


def diagnostics(server, label):
    value = dict(label=label, game_seconds=float(u.GameplayStatics.get_time_seconds(server.get_world())), targets=[])
    for actor in q["owned"]:
        asc = actor.get_component_by_class(u.MyAbilitySystemComponent)
        target = dict(path=actor.get_path_name(), health=q["healths"]([actor])[0],
                      owned_burn=has_owned_tag(asc, "Debuff.Burn"), effects=[])
        try:
            target["effects"] = [dict(handle=handle.export_text(), description=u.AbilitySystemLibrary.get_active_gameplay_effect_debug_string(handle),
                                      remaining=float(u.AbilitySystemLibrary.get_active_gameplay_effect_remaining_duration(server.get_world(), handle)))
                                 for handle in asc.get_active_effects(u.GameplayEffectQuery())]
        except Exception as error:
            # Optional GE detail must not replace the actual health/tag/input
            # assertions with a Python diagnostic export failure.
            target["effect_diagnostic_error"] = repr(error)
        value["targets"].append(target)
    s.setdefault("target_diagnostics", []).append(value)


def hits(server, after, kind):
    return [r for r in q["new_records"](server, after)
            if r.get_editor_property("confirmed_hit") and str(r.get_editor_property("stage")) == "Hit"
            and str(r.get_editor_property("node_type")) == kind]


def begin_cast(local, server, label):
    replicas = q["native_targets"](local.get_world(), local.get_controlled_pawn())
    q["check"]("three_pinned_target_replicas_for_" + label, len(replicas) == 3)
    if not s.get("corridor_verified"):
        q["check"]("actual_map_corridor_ready_before_version_cast", q["projectile_corridor_ready"]())
    q["aim_at"](replicas[0])
    s[label + "_sequence"] = max([int(r.get_editor_property("sequence")) for r in q["records"](server)] + [0])
    s[label + "_before_health"] = q["healths"](q["owned"])
    s[label + "_revision"] = int(q["preview"](server).get_editor_property("revision"))
    diagnostics(server, label + "_before_original_input")
    q["command"](local, "Press")
    s["cast_label"] = label
    q["phase"]("press")


def tick(_delta):
    local = server = other = None
    try:
        if s["phase"].startswith("cleanup"):
            q["cleanup_tick"]()
            return
        if time.monotonic() - s["started"] > 120:
            raise AssertionError("Timed out in " + s["phase"])
        local, server, other = q["participants"]()
        if not local or not server:
            return
        if q["pinned"]:
            q["check"]("pinned_participant_lives_unchanged", q["same_participant_lives"]())
        age = time.monotonic() - s.get("at", s["started"])
        ls, ss = q["preview"](local), q["preview"](server)
        collect(server)
        if s["phase"] == "setup":
            if not q["setup_fixture"]((250, 450, 650)):
                return
            q["request_begin"]("Abilities.Fire.FireBolt")
        elif s["phase"] == "begin":
            if not q["preview_started"](local, server):
                return
            s["await_node"] = q["node_index"](local, "Delivery.ProjectileAwait")
            s["sink_node"] = q["node_index"](local, "Effect.InputDamage")
            s["before_edit_revision"] = int(ls.get_editor_property("revision"))
            q["command"](local, f"Edit Propagation.Sequential {s['await_node']} 1 3 0.3 400 0.7")
            q["phase"]("chain_ack")
        elif s["phase"] == "chain_ack":
            if int(ls.get_editor_property("revision")) <= s["before_edit_revision"] or int(ls.get_editor_property("revision")) != int(ss.get_editor_property("revision")):
                return
            q["check"]("initial_chain_was_accepted", not str(ls.get_editor_property("error")))
            if not q["ready_replicas"]() or not naturally_ready(local, server):
                return
            begin_cast(local, server, "old_damage")
        elif s["phase"] == "press":
            if age >= .12:
                q["command"](local, "Release")
                q["phase"]("observe_" + s["cast_label"])
        elif s["phase"] == "observe_old_damage":
            rows = hits(server, s["old_damage_sequence"], "Propagation.Sequential")
            if rows and not s.get("status_edit_requested"):
                s["status_edit_requested"] = True
                s["status_edit_at_contacts"] = len(rows)
                q["check"]("payload_edit_was_requested_before_all_old_contacts", len(rows) < 3)
                q["command"](local, f"Edit Replace.Status.Petrify {s['sink_node']} 1.25")
            if len(rows) < 3:
                return
            q["check"]("old_real_chain_retained_its_three_hits", len(rows) == 3)
            q["check"]("all_old_contacts_keep_old_frozen_revision", all(int(r.get_editor_property("revision")) == s["old_damage_revision"] for r in rows))
            q["check"]("old_inflight_remaining_contacts_still_damage", all(a < b for a,b in zip(q["healths"](q["owned"]), s["old_damage_before_health"])))
            s["old_damage_root"] = q["guid_hex"](rows[0].get_editor_property("root_cast_id"))
            if int(ls.get_editor_property("revision")) <= s["old_damage_revision"] or int(ls.get_editor_property("revision")) != int(ss.get_editor_property("revision")):
                return
            q["check"]("replacement_status_payload_accepted", not str(ls.get_editor_property("error")))
            if naturally_ready(local, server):
                begin_cast(local, server, "new_status")
        elif s["phase"] == "observe_new_status":
            rows = hits(server, s["new_status_sequence"], "Propagation.Sequential")
            if rows and not s.get("reset_requested"):
                s["reset_requested"] = True
                s["reset_at_contacts"] = len(rows)
                q["check"]("reset_requested_while_real_status_chain_is_in_flight", len(rows) < 3)
                q["command"](local, "Reset")
            if len(rows) < 3:
                return
            q["check"]("withdrawal_does_not_cancel_the_old_accepted_chain", len(rows) == 3)
            q["check"]("status_chain_keeps_its_prior_frozen_revision", all(int(r.get_editor_property("revision")) == s["new_status_revision"] for r in rows))
            q["check"]("status_only_owner_has_no_hidden_fire_or_DOT_damage", q["healths"](q["owned"]) == s["new_status_before_health"])
            q["check"]("different_casts_have_different_root_identities", q["guid_hex"](rows[0].get_editor_property("root_cast_id")) != s["old_damage_root"])
            if int(ls.get_editor_property("revision")) <= s["new_status_revision"] or int(ls.get_editor_property("revision")) != int(ss.get_editor_property("revision")):
                return
            q["check"]("reset_restores_the_actual_default_graph", ls.get_editor_property("content_hash") == ls.get_editor_property("base_hash"))
            if naturally_ready(local, server):
                begin_cast(local, server, "restored_default")
        elif s["phase"] == "observe_restored_default":
            rows = hits(server, s["restored_default_sequence"], "Delivery.ProjectileAwait")
            if not rows or age < 3:
                return
            q["check"]("default_returns_to_one_rank1_original_projectile_contact", len(rows) == 1)
            q["check"]("default_has_no_remaining_propagation_rule", not hits(server, s["restored_default_sequence"], "Propagation.Sequential"))
            hp = q["healths"](q["owned"])
            q["check"]("restored_default_really_damages_only_first_life", hp[0] < s["restored_default_before_health"][0] and hp[1:] == s["restored_default_before_health"][1:])
            replicas = q["native_targets"](local.get_world(), local.get_controlled_pawn())
            q["check"]("final_real_health_converges_on_both_worlds", len(replicas) == 3 and all(abs(a-b) < .01 for a,b in zip(hp, q["healths"](replicas))))
            if other:
                q["check"]("other_player_recipe_untouched_across_edits_and_reset", q["preview"](other).export_text() == s["other_preview_before"])
            s["cast_evidence"] = [{"label": label, "revision": s[label + "_revision"], "before_health": s[label + "_before_health"]}
                                  for label in ("old_damage", "new_status", "restored_default")]
            s["public_observations"] = list(cache.values())
            q["finish"]()
    except Exception as error:
        s["public_observations"] = list(cache.values())
        if server:
            try:
                diagnostics(server, "failure")
            except Exception as diagnostic_error:
                s["diagnostic_error"] = repr(diagnostic_error)
        q["finish"](repr(error))


q["handle"] = u.register_slate_post_tick_callback(tick)
q["write"]()
u.log("DYNAMIC_MAGE_VERSIONS_STARTED " + str(q["run"] / "report.json"))
