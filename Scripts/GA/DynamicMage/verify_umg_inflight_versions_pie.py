"""Three original FireBolt casts, with all version edits through actual UMG.

The first real contact triggers panel replacement of the damage sink. The next
cast's first contact triggers two exact panel Remove actions, restoring default
while that accepted status chain is in flight. No direct edit/reset RPC, baked
graph, TargetData, Notify, damage, cooldown removal or activation is injected.
"""
import time
from pathlib import Path
import unreal as u

library = {"_DYNAMIC_MAGE_UMG_LIBRARY_ONLY": True, "__name__": "dynamic_mage_umg_versions"}
exec((Path(u.Paths.project_dir()) / "Scripts/GA/DynamicMage/verify_umg_interaction_pie.py").read_text(encoding="utf-8"), library)
q, ui = library["q"], library["ui"]
s = q["state"]
s.update(scenario="RealUMG.InFlightVersionReplacementAndWithdrawal",
         scope="Same actual panel/preview grant, original T/cursor/Notify, three real physical casts and frozen in-flight recipes.",
         limitations=["Not OS mouse injection", "Visual quality remains user acceptance"])
cache = {}


def owned_tag(asc, name):
    # Burn grants an owned tag through TargetTagsGameplayEffectComponent. The
    # active-effects-with-all-tags query checks asset tags and cannot prove
    # that a previous cast's naturally expiring DOT has finished.
    tags = u.GameplayTagContainer()
    tags.import_text('(GameplayTags=((TagName="' + name + '")))')
    return bool(asc.has_all_matching_gameplay_tags(tags))


def ready(local, server):
    replicas = q["native_targets"](local.get_world(), local.get_controlled_pawn())
    return (all(q["dev"](p).is_preview_frontend_idle() for p in {local, server}) and
            not any(owned_tag(a.get_component_by_class(u.MyAbilitySystemComponent), "Debuff.Burn") for a in q["owned"]) and
            not any(q["petrified"](a) for a in q["owned"]) and len(replicas) == 3 and
            not any(q["petrified"](a) for a in replicas))


def collect(server, local):
    for r in q["records"](server):
        sequence = int(r.get_editor_property("sequence"))
        if sequence > s.get("observation_start", 2**63 - 1):
            cache[sequence] = dict(sequence=sequence, kind=str(r.get_editor_property("kind")),
                stage=str(r.get_editor_property("stage")), node=str(r.get_editor_property("node_type")),
                confirmed=bool(r.get_editor_property("confirmed_hit")), paid=bool(r.get_editor_property("cost_succeeded")),
                tag=r.get_editor_property("ability_tag").export_text(), root=q["guid_hex"](r.get_editor_property("root_cast_id")),
                revision=int(r.get_editor_property("revision")), frozen_hash=str(r.get_editor_property("frozen_hash")),
                end_reason=str(r.get_editor_property("end_reason")), server_seconds=float(r.get_editor_property("server_seconds")))
    label = s.get("cast_label")
    replicas = q["native_targets"](local.get_world(), local.get_controlled_pawn())
    if label:
        for side, actors in (("server", q["owned"]), ("client", replicas)):
            if len(actors) == 3:
                for index, actor in enumerate(actors):
                    s[label + "_" + side + "_states"][index] |= q["petrified"](actor)


def diagnostics(local, server, label):
    """Read actual controls after focus commit and world-owned runtime copies."""
    value = dict(label=label, wall_seconds=time.monotonic() - s["started"],
                 game_seconds=float(u.GameplayStatics.get_time_seconds(server.get_world())), configs=[], targets=[])
    for actor in q["owned"]:
        asc = actor.get_component_by_class(u.MyAbilitySystemComponent)
        target = dict(path=actor.get_path_name(), health=q["healths"]([actor])[0],
                      owned_burn=owned_tag(asc, "Debuff.Burn"), effects=[])
        try:
            target["effects"] = [dict(handle=handle.export_text(), description=u.AbilitySystemLibrary.get_active_gameplay_effect_debug_string(handle),
                                      remaining=float(u.AbilitySystemLibrary.get_active_gameplay_effect_remaining_duration(server.get_world(), handle)))
                                 for handle in asc.get_active_effects(u.GameplayEffectQuery())]
        except Exception as error:
            # Keep optional GE-export diagnostics separate from the strict
            # health, owned-tag and original UMG transaction assertions.
            target["effect_diagnostic_error"] = repr(error)
        value["targets"].append(target)
    value["preview"] = {side: dict(revision=int(q["preview"](pc).get_editor_property("revision")),
                                  content_hash=str(q["preview"](pc).get_editor_property("content_hash")),
                                  error=str(q["preview"](pc).get_editor_property("error")))
                        for side, pc in (("local", local), ("server", server))}
    replicas = q["native_targets"](local.get_world(), local.get_controlled_pawn())
    value["actual_petrify_state"] = {"server": [q["petrified"](a) for a in q["owned"]],
                                      "client": [q["petrified"](a) for a in replicas]}
    value["actual_client_health"] = q["healths"](replicas)
    if ui["panel"] and u.SystemLibrary.is_valid(ui["panel"]):
        value["actual_spin_values"] = {name: float(library["control"](name, u.SpinBox).get_value())
                                       for name in ("RecipeValue", "RecipeInterval", "RecipeRadius")}
    for config in u.ObjectIterator(u.AbilityPropagationNodeConfig):
        if not u.SystemLibrary.is_valid(config) or config.get_name().startswith("Default__"):
            continue
        outer = config.get_outer()
        owned = False
        for _ in range(12):
            if not outer:
                break
            if outer == q["dev"](server) or outer == server.get_world():
                owned = True
                break
            outer = outer.get_outer()
        if owned:
            value["configs"].append(dict(path=config.get_path_name(), jump_delay=float(config.get_editor_property("jump_delay")),
                max_targets=int(config.get_editor_property("max_targets")), jump_radius=float(config.get_editor_property("jump_radius"))))
    s.setdefault("actual_configuration_diagnostics", []).append(value)


def hits(label, node):
    return [r for sequence, r in sorted(cache.items()) if sequence > s[label + "_sequence"]
            and r["tag"] == s["selected_tag"] and r["confirmed"] and r["stage"] == "Hit" and r["node"] == node]


def assert_cast(label, node, count):
    rows = hits(label, node)
    q["check"](label + "_has_exact_real_contact_count", len(rows) == count)
    root = rows[0]["root"]
    q["check"](label + "_one_valid_root", root != "0" * 32 and all(r["root"] == root for r in rows))
    records = [r for r in cache.values() if r["root"] == root]
    q["check"](label + "_all_records_keep_frozen_revision_hash", all(
        r["revision"] == s[label + "_revision"] and r["frozen_hash"] == s[label + "_hash"] for r in records))
    q["check"](label + "_one_original_successful_fee", sum(r["kind"] == str(u.AbilityRuntimeObservationKind.COST) and r["paid"] for r in records) == 1)
    forbidden = {str(u.AbilityGraphEndReason.FAILED), str(u.AbilityGraphEndReason.BUDGET_EXCEEDED), str(u.AbilityGraphEndReason.SOURCE_INVALID)}
    q["check"](label + "_no_hidden_branch_failure", all(r["end_reason"] not in forbidden for r in records
                 if r["kind"] == str(u.AbilityRuntimeObservationKind.GRAPH_FINISHED)))
    s[label + "_root"] = root


def start_cast(local, server, label):
    replicas = q["native_targets"](local.get_world(), local.get_controlled_pawn())
    q["check"]("three_actual_lives_for_" + label, len(replicas) == 3)
    q["check"]("previous_petrify_naturally_cleared_on_both_worlds_before_" + label,
                 not any(q["petrified"](a) for a in q["owned"] + replicas))
    if not s.get("corridor_verified"):
        q["check"]("real_world_flight_corridor", q["projectile_corridor_ready"]())
    q["aim_at"](replicas[0])
    s["cast_label"] = label
    s[label + "_sequence"] = max([int(r.get_editor_property("sequence")) for r in q["records"](server)] + [0])
    s[label + "_revision"] = int(q["preview"](server).get_editor_property("revision"))
    s[label + "_hash"] = str(q["preview"](server).get_editor_property("content_hash"))
    s[label + "_before_health"] = q["healths"](q["owned"])
    s[label + "_server_states"] = [False] * 3
    s[label + "_client_states"] = [False] * 3
    diagnostics(local, server, label + "_before_original_input")
    q["phase"]("aim")


def tick(_delta):
    local = server = other = None
    try:
        if s["phase"].startswith("cleanup"):
            q["cleanup_tick"]()
            return
        local, server, other = q["participants"]()
        if not local or not server:
            return
        if s["phase"] in ("recover_open", "recover_close"):
            library["tick"](_delta)
            return
        if time.monotonic() - s["started"] > 140:
            raise AssertionError("Timed out in " + s["phase"])
        if q["pinned"]:
            q["check"]("pinned_participant_lives_unchanged", q["same_participant_lives"]())
        age = time.monotonic() - s.get("at", s["started"])
        ls, ss = q["preview"](local), q["preview"](server)
        collect(server, local)
        if s["phase"] == "setup":
            if not q["setup_fixture"]((250, 450, 650)):
                return
            q["check"]("no_preexisting_recipe_panel", library["panel"](local) is None)
            s["original_handles"] = {p.get_path_name(): library["handles"](p) for p in {local, server}}
            q["command"](local, "UMG.Open")
            q["phase"]("open")
        elif s["phase"] == "open":
            p = library["panel"](local)
            if not p or not p.is_visible():
                return
            ui["panel"] = p
            row = next(r for r in q["dev"](local).get_definitions()
                       if r.get_editor_property("base").get_editor_property("ability_class").get_path_name() ==
                       "/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/GA_FireBolt.GA_FireBolt_C")
            s["definition_id"] = str(row.get_editor_property("definition_id"))
            library["queue"](["UMG.Select RecipeDefinition " + s["definition_id"]], "begin_click")
        elif s["phase"] == "ui_queue":
            if age >= .1:
                if s["ui_queue"]:
                    q["command"](local, s["ui_queue"].pop(0))
                    s["at"] = time.monotonic()
                else:
                    q["phase"](s["ui_next"])
        elif s["phase"] == "begin_click":
            s["begin_state_sequences"] = {p.get_path_name(): int(q["preview"](p).get_editor_property("state_sequence")) for p in {local, server}}
            s["owns_preview_request"] = True
            q["command"](local, "UMG.Click RecipeBegin")
            q["phase"]("begin")
        elif s["phase"] == "begin":
            if not q["preview_started"](local, server):
                return
            if not all(len(library["handles"](p)) == len(s["original_handles"][p.get_path_name()]) + 1 for p in {local, server}):
                return
            s["preview_handles"] = {p.get_path_name(): library["handles"](p) for p in {local, server}}
            s["await_node"] = q["node_index"](local, "Delivery.ProjectileAwait")
            s["sink_node"] = q["node_index"](local, "Effect.InputDamage")
            s["selected_tag"] = ss.get_editor_property("ability_tag").export_text()
            s["before_chain_revision"] = int(ls.get_editor_property("revision"))
            library["queue"]([f"UMG.Select RecipeNode {s['await_node']}", "UMG.Select RecipeModifier Propagation.Sequential",
                "UMG.Value RecipeRadius 400", "UMG.Value RecipeInterval 1", "UMG.Click RecipeApply"], "chain_ack")
        elif s["phase"] == "chain_ack":
            library["reject_error"](ls)
            if int(ls.get_editor_property("revision")) <= s["before_chain_revision"] or int(ls.get_editor_property("revision")) != int(ss.get_editor_property("revision")):
                return
            diagnostics(local, server, "initial_apply_after_focus_commit")
            q["check"]("actual_interval_control_retains_requested_one_second_after_apply",
                         abs(library["control"]("RecipeInterval", u.SpinBox).get_value() - 1) < .001)
            s["observation_start"] = max([int(r.get_editor_property("sequence")) for r in q["records"](server)] + [0])
            q["command"](local, "UMG.Click RecipeHide")
            q["phase"]("first_hide")
        elif s["phase"] == "first_hide":
            if not ui["panel"].is_visible() and q["ready_replicas"]() and ready(local, server):
                q["check"]("hide_restores_actual_viewport_focus", ui["panel"].has_owner_viewport_focus())
                start_cast(local, server, "old_damage")
        elif s["phase"] == "aim":
            replicas = q["native_targets"](local.get_world(), local.get_controlled_pawn())
            if len(replicas) == 3:
                q["aim_at"](replicas[0])
            if age > .5:
                q["command"](local, "Press")
                q["phase"]("press")
        elif s["phase"] == "press":
            if age >= .12:
                q["command"](local, "Release")
                q["phase"]("observe_" + s["cast_label"])
        elif s["phase"] == "observe_old_damage":
            rows = hits("old_damage", "Propagation.Sequential")
            if rows and not s.get("status_edit_requested"):
                s["status_edit_requested"] = True
                q["check"]("actual_panel_edit_requested_while_first_chain_in_flight", len(rows) < 3)
                library["queue"](["UMG.Open", f"UMG.Select RecipeNode {s['sink_node']}",
                    "UMG.Select RecipeModifier Replace.Status.Petrify", "UMG.Value RecipeValue 1.25",
                    "UMG.Click RecipeApply"], "status_ack")
        elif s["phase"] == "status_ack":
            library["reject_error"](ls)
            if int(ls.get_editor_property("revision")) <= s["old_damage_revision"] or int(ls.get_editor_property("revision")) != int(ss.get_editor_property("revision")):
                return
            diagnostics(local, server, "status_replace_ack")
            q["check"]("actual_ui_status_version_accepted_before_all_old_hits", len(hits("old_damage", "Propagation.Sequential")) < 3)
            q["command"](local, "UMG.Click RecipeHide")
            q["phase"]("old_tail")
        elif s["phase"] == "old_tail":
            if len(hits("old_damage", "Propagation.Sequential")) < 3:
                return
            assert_cast("old_damage", "Propagation.Sequential", 3)
            q["check"]("old_frozen_damage_survives_ui_replacement", all(a < b for a, b in zip(q["healths"](q["owned"]), s["old_damage_before_health"])))
            if ready(local, server):
                start_cast(local, server, "new_status")
        elif s["phase"] == "observe_new_status":
            rows = hits("new_status", "Propagation.Sequential")
            if rows and not s.get("withdrawal_requested"):
                s["withdrawal_requested"] = True
                q["check"]("actual_ui_withdrawal_requested_while_status_chain_in_flight", len(rows) < 3)
                s["before_remove_status_revision"] = int(ls.get_editor_property("revision"))
                library["queue"](["UMG.Open", f"UMG.Select RecipeNode {s['sink_node']}",
                    "UMG.Select RecipeModifier Replace.Status.Petrify", "UMG.Click RecipeRemove"], "remove_status_ack")
        elif s["phase"] == "remove_status_ack":
            library["reject_error"](ls)
            if int(ls.get_editor_property("revision")) <= s["before_remove_status_revision"] or int(ls.get_editor_property("revision")) != int(ss.get_editor_property("revision")):
                return
            if ls.get_editor_property("content_hash") != ss.get_editor_property("content_hash"):
                return
            diagnostics(local, server, "first_remove_server_and_owner_ack")
            q["check"]("first_ui_remove_keeps_separate_chain_contribution", ls.get_editor_property("content_hash") != ls.get_editor_property("base_hash"))
            q["check"]("first_ui_remove_ack_while_status_chain_in_flight", len(hits("new_status", "Propagation.Sequential")) < 3)
            s["before_remove_chain_revision"] = int(ls.get_editor_property("revision"))
            # The second transaction is issued only after the first owner RPC
            # acknowledgment is visible. A queue of two clicks can reuse the
            # old revision on a remote client even when Standalone succeeds.
            library["queue"]([f"UMG.Select RecipeNode {s['await_node']}", "UMG.Select RecipeModifier Propagation.Sequential",
                "UMG.Click RecipeRemove"], "withdrawal_ack")
        elif s["phase"] == "withdrawal_ack":
            library["reject_error"](ls)
            if int(ls.get_editor_property("revision")) <= s["before_remove_chain_revision"] or int(ls.get_editor_property("revision")) != int(ss.get_editor_property("revision")):
                return
            if ls.get_editor_property("content_hash") != ls.get_editor_property("base_hash") or ss.get_editor_property("content_hash") != ss.get_editor_property("base_hash"):
                return
            diagnostics(local, server, "both_remove_ack")
            q["check"]("original_ui_remove_restored_actual_default_while_in_flight", len(hits("new_status", "Propagation.Sequential")) < 3)
            q["check"]("all_versions_keep_exact_same_preview_grant", all(library["handles"](p) == s["preview_handles"][p.get_path_name()] for p in {local, server}))
            q["command"](local, "UMG.Click RecipeHide")
            q["phase"]("status_tail")
        elif s["phase"] == "status_tail":
            if len(hits("new_status", "Propagation.Sequential")) < 3:
                return
            assert_cast("new_status", "Propagation.Sequential", 3)
            q["check"]("status_chain_has_no_hidden_old_fire_or_dot_damage", q["healths"](q["owned"]) == s["new_status_before_health"])
            q["check"]("new_payload_really_petrified_all_three_lives_on_both_worlds", all(s["new_status_server_states"]) and all(s["new_status_client_states"]))
            q["check"]("old_and_new_casts_have_independent_roots", s["old_damage_root"] != s["new_status_root"])
            if ready(local, server):
                start_cast(local, server, "restored_default")
        elif s["phase"] == "observe_restored_default":
            if age < 3 or not hits("restored_default", "Delivery.ProjectileAwait"):
                return
            assert_cast("restored_default", "Delivery.ProjectileAwait", 1)
            q["check"]("withdrawn_ui_rule_no_longer_propagates", not hits("restored_default", "Propagation.Sequential"))
            hp = q["healths"](q["owned"])
            q["check"]("restored_actual_default_only_damages_first_life", hp[0] < s["restored_default_before_health"][0] and hp[1:] == s["restored_default_before_health"][1:])
            q["check"]("withdrawn_ui_status_is_absent_from_subsequent_default_cast", not any(s["restored_default_server_states"]) and not any(s["restored_default_client_states"]))
            s["public_observations"] = list(cache.values())
            library["queue"](["UMG.Open", "UMG.Click RecipeClose"], "closed")
        elif s["phase"] == "closed":
            requested = q["guid_hex"](ui["panel"].get_last_close_request_id())
            acknowledged = q["guid_hex"](q["property_value"](q["dev"](local), "last_close_acknowledged_id", "LastCloseAcknowledgedId"))
            if requested == "0" * 32 or acknowledged != requested or library["panel"](local):
                return
            if ls.get_editor_property("preview_active") or ss.get_editor_property("preview_active"):
                return
            if not all(library["handles"](p) == s["original_handles"][p.get_path_name()] for p in {local, server}):
                return
            q["check"]("same_actual_panel_exact_close_ack", q["property_value"](q["dev"](local), "last_close_succeeded", "bLastCloseSucceeded"))
            q["check"]("temporary_spec_reclaimed_preserving_all_original_grants", True)
            q["check"]("final_owner_viewport_focus_restored", ui["panel"].has_owner_viewport_focus())
            if other:
                q["check"]("other_player_recipe_isolated_through_ui_version_edits", q["preview"](other).export_text() == s["other_preview_before"])
            s["owns_preview_request"] = False
            library["finish"]()
    except Exception as error:
        s["public_observations"] = list(cache.values())
        if local and server:
            try:
                diagnostics(local, server, "failure")
            except Exception as diagnostic_error:
                s["diagnostic_error"] = repr(diagnostic_error)
        library["finish"](repr(error))


q["handle"] = u.register_slate_post_tick_callback(tick)
q["write"]()
u.log("DYNAMIC_MAGE_UMG_VERSIONS_STARTED " + str(q["run"] / "report.json"))
