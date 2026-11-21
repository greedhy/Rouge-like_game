"""Actual fourth-tab controls, ordered edits, new rules and concurrent FireBolt.

Run only on the empty L_Batch6Test in Standalone or two-player Listen PIE.
Uses the existing native Slate/UMG probe and original T/cursor/animation events.
No direct recipe mutations, injected contacts, profession preset or asset saves.
"""
import time
import uuid
from pathlib import Path
import unreal as u

w = {"_WORKSHOP_PIE_LIBRARY_ONLY": True, "WORKSHOP_ALLOW_UNSELECTED": True,
     "WORKSHOP_ENTRY_MODE": "CharacterMenu",
     "WORKSHOP_PIE_MODE": globals().get("GRAPH_EXPANSION_MODE", "Standalone"),
     "__name__": "graph_expansion_workshop_fixture"}
for old, new in (("GRAPH_EXPANSION_LOADED_BUILD", "WORKSHOP_PIE_LOADED_BUILD"),
                 ("GRAPH_EXPANSION_SCREENSHOTS", "WORKSHOP_SCREENSHOT_DIR")):
    if globals().get(old):
        w[new] = globals()[old]
project = Path(u.Paths.project_dir()).resolve()
exec((project / "Scripts/GA/RuntimeGraphWorkshop/verify_workshop_pie.py").read_text(encoding="utf-8"), w)
q, ui, s = w["q"], w["ui"], w["s"]
q["run"].rmdir()
q["root"] = Path(u.Paths.project_saved_dir()).resolve() / "GAAuthoring/GraphExpansion/RealCasting"
q["run"] = q["root"] / uuid.uuid4().hex
q["run"].mkdir(parents=True)
s.update(scenario="ConcurrentForkAndOrderedQueryConditions", observations=[], graphs={}, edit_index=0)
prop, guid, check, phase = w["prop"], w["guid"], w["check"], w["phase"]
command, queue = w["command"], w["queue"]
panel, control, handles = w["panel"], w["control"], w["handles"]

# Each operation selects an actual accepted node/output, previews and applies its
# exact server draft. Repeating MaxTargets deliberately exercises ordered replay.
edits = [
    ("fork", "Delivery.ProjectileAwait", None, "Propagation.Forked", None,
     {"RecipeCount": 3, "RecipeInterval": .3, "RecipeRadius": 700,
      "RecipeFalloff": .8, "RecipeMaxDepth": 1, "RecipeBranches": 2}),
    ("count6", "Propagation.Forked", None, "Set.MaxTargets", None, {"RecipeValue": 6}),
    ("count5", "Propagation.Forked", None, "Set.MaxTargets", None, {"RecipeValue": 5}),
    ("delay", "Propagation.Forked", None, "Set.JumpDelay", None, {"RecipeValue": .2}),
    ("nearest", "Propagation.Forked", None, "Set.TargetSelection", None, {"RecipeValue": 1}),
    ("survived", "Effect.InputDamage", "DamageResolved", "Append.Registered", "Condition.Killed", {"RecipeValue": 0}),
    ("query", "Condition.Killed", "Passed", "Append.Registered", "Query.SelectTargets", {"RecipeCount": 1, "RecipeRadius": 700}),
    ("lowhealth", "Query.SelectTargets", None, "Set.TargetSelection", None, {"RecipeValue": 3}),
    ("targetstate", "Query.SelectTargets", "TargetFound", "Append.Registered", "Target.Petrify", {"RecipeValue": 1.25}),
    ("chance", "Propagation.Forked", "Completed", "Append.Registered", "Condition.Chance", {"RecipeValue": 1}),
    ("areastate", "Condition.Chance", "Passed", "Append.Registered", "Area.Petrify", {"RecipeValue": 1.25, "RecipeRadius": 700}),
]


def begin_edit(view):
    name, type_id, port, modifier, fragment, values = edits[s["edit_index"]]
    node = w["unique_node"](view, type_id)
    s.update(edit=name, previous_graph="base" if s["edit_index"] == 0 else edits[s["edit_index"] - 1][0],
             modifier=modifier, fragment=fragment, values=values, selected_node=node)
    commands = ["UMG.Click RecipeGraphFit", "UMG.GraphNode " + node]
    if port:
        commands.append(f"UMG.GraphPort {node} {port}")
    commands.append("UMG.Select RecipeModifier " + modifier)
    if fragment:
        commands.append("UMG.Select RecipeFragment " + fragment)
    commands.extend(f"UMG.Value {key} {value}" for key, value in values.items())
    queue(commands, "preview_click")


def verify_candidate(view):
    before = s["graphs"][s["previous_graph"]]
    name = s["edit"]
    check(name + "_next_revision_and_private_content", view["revision"] == before["revision"] + 1
          and view["hash"] != before["hash"])
    check(name + "_one_ordered_source", view["sources"][:-1] == before["sources"]
          and len(view["sources"]) == len(before["sources"]) + 1)
    if name == "fork":
        node = s["selected_node"]
        s["delivery_id"] = node
        values = w["parameters"](view, node)
        check("new_fork_preserves_identity_and_all_base_wires", view["nodes"][node]["type"] == "Propagation.Forked"
              and view["nodes"].keys() == before["nodes"].keys() and view["edges"] == before["edges"])
        check("new_fork_defaults_to_real_concurrent_siblings", values.get("bConcurrentBranches") == 1
              and values.get("BranchesPerTarget") == 2 and values.get("MaxDepth") == 1)
    elif name in {"count6", "count5", "delay", "nearest", "lowhealth"}:
        check(name + "_no_identity_or_wire_rewrite", view["nodes"].keys() == before["nodes"].keys()
              and view["edges"] == before["edges"])
        key, expected = {"count6": ("MaxTargets", 6), "count5": ("MaxTargets", 5),
                         "delay": ("JumpDelay", .2), "nearest": ("TargetSelection", 1),
                         "lowhealth": ("TargetSelection", 3)}[name]
        check(name + "_actual_config_consumes_value", abs(w["parameters"](view, s["selected_node"]).get(key, -99) - expected) < .001)
    else:
        expected = {"survived": "Condition.Killed", "query": "Query.SelectTargets", "targetstate": "Status.Petrify",
                    "chance": "Condition.Chance", "areastate": "Query.PetrifyArea"}[name]
        check(name + "_real_registered_fragment_added", any(n["type"] == expected for n in view["nodes"].values()))


def begin_cast(local, server):
    s["cast_sequence"] = max([int(prop(r, "sequence")) for r in q["records"](server)] + [0])
    s["health_before"] = q["healths"](q["owned"])
    s["mana_before"] = q["attribute"](q["component"](server, u.MyAbilitySystemComponent), "mana")
    s["projectile_peak"] = {"authority": 0, "local": 0}
    s["status_seen"] = {"authority": [False] * 3, "local": [False] * 3}
    ui["rows"] = {}
    command(local, "UMG.Click RecipeHide")
    phase("hidden")


def verify_cast(local, server):
    rows = [r for r in ui["rows"].values() if r["ability"] == s["ability_tag"]]
    s["observations"] = sorted(rows, key=lambda r: r["sequence"])
    hits = [r for r in rows if r["hit"] and r["node"] == "Propagation.Forked" and r["stage"] == "Hit"]
    costs = [r for r in rows if r["kind"] == str(u.AbilityRuntimeObservationKind.COST)]
    accepted = s["graphs"][edits[-1][0]]
    check("real_sibling_arrows_overlap_in_both_worlds", all(n >= 2 for n in s["projectile_peak"].values()))
    check("three_fork_contacts_on_distinct_original_lives", len(hits) == 3 and len({r["target"] for r in hits}) == 3)
    check("one_original_root_and_cost", len(costs) == 1 and costs[0]["paid"]
          and len({r["cast"] for r in hits + costs}) == 1 and costs[0]["resource_after"] < costs[0]["resource_before"])
    check("cast_uses_exact_applied_graph", all(r["revision"] == accepted["revision"] and r["hash"] == accepted["hash"] for r in rows))
    for node, stage in (("Condition.Killed", "Passed"), ("Query.SelectTargets", "TargetFound"), ("Condition.Chance", "Passed")):
        check("real_" + node + "_" + stage, any(r["node"] == node and r["stage"] == stage for r in rows))
    s["health_after"] = q["healths"](q["owned"])
    replicas = q["native_targets"](local.get_world(), local.get_controlled_pawn())
    s["replica_health_after"] = q["healths"](replicas)
    check("all_targets_take_real_damage", all(a < b for a, b in zip(s["health_after"], s["health_before"])))
    check("actual_damage_replicates", len(replicas) == 3 and all(abs(a - b) < .01 for a, b in zip(s["health_after"], s["replica_health_after"])))
    check("query_and_condition_successors_apply_replicated_state", all(all(x) for x in s["status_seen"].values()))
    finished = [r for r in rows if r["kind"] == str(u.AbilityRuntimeObservationKind.GRAPH_FINISHED)]
    check("actual_root_completes", bool(finished) and all(r["end"] == str(u.AbilityGraphEndReason.COMPLETED) for r in finished))


def tick(delta):
    local = None
    try:
        name = s["phase"]
        if name.startswith(("cleanup", "recover_")):
            w["tick"](delta)
            return
        if time.monotonic() - s["started"] > 180:
            raise AssertionError("Timed out in " + name)
        local, server, other = q["participants"]()
        if not local or not server:
            return
        age = time.monotonic() - s.get("at", s["started"])
        ls, ss = q["preview"](local), q["preview"](server)
        if q["pinned"]:
            check("original_participant_lives_preserved", q["same_participant_lives"]())
        if name == "setup":
            expected = s["requested_mode"]
            if expected == "ListenServerRemote" and (local == server or not other):
                return
            check("explicit_mode", expected == ("Standalone" if local == server else "ListenServerRemote"))
            if "pre_fixture_specs" not in s:
                check("no_preexisting_panel", panel(local) is None)
                s["pre_fixture_specs"] = {pc.get_path_name(): handles(pc) for pc in {local, server, other} if pc}
            if not q["setup_fixture"]((250, 600, 600)):
                return
            if not s.get("spread_targets"):
                pawn = server.get_controlled_pawn()
                for index, side in ((1, -150), (2, 150)):
                    q["owned"][index].set_actor_location(pawn.get_actor_location() + pawn.get_actor_forward_vector() * 600
                                                       + pawn.get_actor_right_vector() * side, False, False)
                    q["owned"][index].force_net_update()
                s["spread_targets"] = True
            s["original_handles"] = {pc.get_path_name(): handles(pc) for pc in {local, server}}
            command(local, "UMG.Open")
            phase("open")
        elif name == "open":
            widget = panel(local)
            if not widget or not widget.is_visible():
                return
            ui["panel"] = widget
            check("actual_fourth_page", local.get_hud().get_character_menu().get_active_page() == 3)
            row = next(r for r in q["dev"](local).get_definitions()
                       if prop(prop(r, "base"), "ability_class").get_path_name() == w["FIRE_BOLT"] and prop(prop(r, "base"), "selectable"))
            queue(["UMG.Select RecipeDefinition " + str(prop(row, "definition_id"))], "begin_click")
        elif name == "ui_queue":
            if age >= .12:
                if s["ui_queue"]:
                    command(local, s["ui_queue"].pop(0))
                    s["at"] = time.monotonic()
                else:
                    phase(s["ui_next"])
        elif name == "begin_click":
            s["begin_state_sequences"] = {pc.get_path_name(): w["checkpoint"](q["preview"](pc)) for pc in {local, server}}
            s["owns_preview_request"] = True
            command(local, "UMG.Click RecipeBegin")
            phase("begin")
        elif name == "begin":
            if not q["preview_started"](local, server):
                return
            view = w["matching_graph"](ls, ss)
            if not view:
                return
            s["session"] = guid(prop(ls, "preview_session_id"))
            s["ability_tag"] = prop(ls, "ability_tag").export_text()
            s["graphs"]["base"] = view
            w["remember_node_ids"](ls)
            begin_edit(view)
        elif name == "preview_click":
            check(s["edit"] + "_actual_modifier_selected", control("RecipeModifier", u.ComboBoxString).get_selected_option() == s["modifier"])
            check(s["edit"] + "_actual_spin_values", all(abs(control(k, u.SpinBox).get_value() - v) < .001 for k, v in s["values"].items()))
            s["edit_request_checkpoint"] = w["checkpoint"](ls)
            command(local, "UMG.Click RecipePreviewEdit")
            phase("draft")
        elif name == "draft":
            if any(w["checkpoint"](snap) <= s["edit_request_checkpoint"] for snap in (ls, ss)):
                return
            w["reject_errors"](ls, ss)
            draft = w["matching_graph"](ls, ss, "draft_graph")
            if not draft or w["draft_id"](ls) == w["EMPTY_GUID"] or w["draft_id"](ls) != w["draft_id"](ss):
                return
            w["require_same_session"](ls, ss)
            check(s["edit"] + "_preview_preserves_accepted", w["matching_graph"](ls, ss) == s["graphs"][s["previous_graph"]])
            verify_candidate(draft)
            s["graphs"][s["edit"] + "_draft"] = draft
            command(local, "UMG.Click RecipeApplyDraft")
            phase("applied")
        elif name == "applied":
            w["reject_errors"](ls, ss)
            view = w["matching_graph"](ls, ss)
            if view != s["graphs"][s["edit"] + "_draft"] or w["draft_id"](ls) != w["EMPTY_GUID"]:
                return
            s["graphs"][s["edit"]] = view
            w["remember_node_ids"](ls)
            check(s["edit"] + "_exact_draft_promoted", True)
            s["edit_index"] += 1
            if s["edit_index"] < len(edits):
                begin_edit(view)
            else:
                queue(["UMG.Click RecipeGraphFit"], "capture")
        elif name == "capture":
            w["capture_step"](local, "01-complete-concurrent-rule-graph")
            phase("capture_hold")
        elif name == "capture_hold":
            if age >= .3:
                queue(["UMG.GraphNode " + s["delivery_id"]], "capture_parameters")
        elif name == "capture_parameters":
            w["capture_step"](local, "02-in-place-fork-parameters", 220)
            phase("capture_parameters_hold")
        elif name == "capture_parameters_hold":
            if age >= .3:
                if w["capture_dir"] and s["requested_mode"] == "Standalone":
                    w["content_scroll"]().set_scroll_offset(0.)
                begin_cast(local, server)
        elif name == "hidden":
            if not ui["panel"].is_visible():
                check("hidden_workshop_keeps_session", bool(prop(ls, "preview_active")))
                phase("aim")
        elif name == "aim":
            replicas = q["ready_replicas"]()
            if not replicas or (not s.get("corridor_verified") and not q["projectile_corridor_ready"]()):
                return
            q["aim_at"](replicas[0])
            if age > .5 and q["dev"](local).is_preview_frontend_idle() and q["dev"](server).is_preview_frontend_idle():
                command(local, "Press")
                phase("press")
        elif name in {"press", "observe"}:
            w["observe_cast"](local, server)
            if name == "press" and age >= .12:
                command(local, "Release")
                phase("observe")
            elif name == "observe" and age >= 6:
                verify_cast(local, server)
                command(local, "UMG.Open")
                phase("reopen")
        elif name == "reopen":
            if ui["panel"].is_visible():
                command(local, "UMG.Click RecipeUndoLast")
                phase("undo")
        elif name == "undo":
            view = w["matching_graph"](ls, ss)
            expected = s["graphs"][edits[-2][0]]
            if view and view["hash"] == expected["hash"]:
                check("undo_replays_ordered_history", view["nodes"] == expected["nodes"] and view["edges"] == expected["edges"])
                command(local, "UMG.Click RecipeReset")
                phase("reset")
        elif name == "reset":
            view = w["matching_graph"](ls, ss)
            if view and view["hash"] == s["graphs"]["base"]["hash"]:
                check("reset_restores_original_base", view["nodes"] == s["graphs"]["base"]["nodes"])
                command(local, "UMG.Character.Close")
                phase("closed")
        elif name == "closed":
            ack = w["close_ack"](local)
            if not ack or not ack["succeeded"] or panel(local) or any(prop(snap, "preview_active") for snap in (ls, ss)):
                return
            if not all(handles(pc) == s["original_handles"][pc.get_path_name()] for pc in {local, server}):
                return
            s["owns_preview_request"] = False
            s["exact_close"] = ack
            check("preview_spec_reclaimed", True)
            check("profession_never_changed", all(pc.get_editor_property("player_state").get_editor_property("profession")
                                                  == u.PlayerProfession.UNSELECTED for pc in {local, server}))
            if other:
                check("nonowner_host_unchanged", q["preview"](other).export_text() == s["other_preview_before"])
            q["finish"]()
    except Exception as error:
        w["recover"](repr(error), local)


s["binary"] = w["binary_evidence"]()
q["handle"] = u.register_slate_post_tick_callback(tick)
q["write"]()
u.log("GRAPH_EXPANSION_PIE_STARTED " + str(q["run"] / "report.json"))
