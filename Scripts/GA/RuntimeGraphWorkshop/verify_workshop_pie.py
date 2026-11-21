"""Run in an empty L_Batch6Test single-process PIE, after the current editor build loads.

Set WORKSHOP_PIE_MODE to Standalone or ListenServerRemote to require a mode.
The Listen Server case needs two players. Start after both pawns are ready.
An optional WORKSHOP_PIE_LOADED_BUILD JSON path supplies process/module evidence
from the launcher. Disk hashes are recorded separately from loaded evidence.
"""

import hashlib
import json
import os
import re
import time
import uuid
from pathlib import Path

import unreal as u


project = Path(u.Paths.project_dir()).resolve()
library = {"_DYNAMIC_MAGE_UMG_LIBRARY_ONLY": True, "__name__": "workshop_ui_fixture"}
exec((project / "Scripts/GA/DynamicMage/verify_umg_interaction_pie.py").read_text(encoding="utf-8"), library)
q, ui = library["q"], library["ui"]
q["ALLOW_UNSELECTED_MAGE"] = globals().get("WORKSHOP_ALLOW_UNSELECTED", False)
s = q["state"]
q["run"].rmdir()  # Import allocated an empty, private legacy run directory.
q["root"] = Path(u.Paths.project_saved_dir()).resolve() / "GAAuthoring/RuntimeGraphWorkshop/RealCasting"
q["run"] = q["root"] / uuid.uuid4().hex
q["run"].mkdir(parents=True)
s.update(scenario="FireBoltCanvasEdgeInsertAndInsertedNodeEdit",
         scope="Native Slate graph click geometry, actual UMG controls, exact owner RPCs, original T/cursor/Notify.",
         limitations=["Single-process PIE", "No visual quality certification"],
         requested_mode=globals().get("WORKSHOP_PIE_MODE"), graphs={}, observations=[])

FIRE_BOLT = "/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/GA_FireBolt.GA_FireBolt_C"
EMPTY_GUID = "0" * 32
PROPAGATION_VALUES = {"RecipeCount": 3, "RecipeInterval": .3, "RecipeRadius": 400, "RecipeFalloff": .8}
check = q["check"]
phase = q["phase"]
ENTRY_MODE = globals().get("WORKSHOP_ENTRY_MODE", "Console")


def command(pc, text):
    if text == "UMG.Open" and ENTRY_MODE == "CharacterMenu":
        # 顺序执行原属性按钮委托和真实第四页 Slate 按钮，不能用 HUD.ShowCharacterMenu(3) 冒充入口。
        q["command"](pc, "UMG.Character.Attributes")
        q["command"](pc, "UMG.Character.Page 3")
    else:
        q["command"](pc, text)


s["entry_mode"] = ENTRY_MODE
s["unselected_profession_validation"] = q["ALLOW_UNSELECTED_MAGE"]
prop = q["property_value"]
guid = q["guid_hex"]
control = library["control"]
panel = library["panel"]
handles = library["handles"]
capture_dir = Path(globals()["WORKSHOP_SCREENSHOT_DIR"]) if globals().get("WORKSHOP_SCREENSHOT_DIR") else None


def content_scroll():
    """Read the actual panel's root content scroller, excluding its observation log."""
    for widget in u.ObjectIterator(u.ScrollBox):
        if not widget.get_children_count() or not isinstance(widget.get_child_at(0), u.VerticalBox):
            continue
        outer = widget.get_outer()
        for _ in range(5):
            if outer == ui["panel"]:
                return widget
            outer = outer.get_outer() if outer else None
    raise AssertionError("Actual content scroll box is missing")


def capture_step(pc, name, scroll=0):
    """Capture unedited game UI; scrolling only reveals real controls, never changes a graph."""
    if not capture_dir or s["requested_mode"] != "Standalone":
        return
    capture_dir.mkdir(parents=True, exist_ok=True)
    content_scroll().set_scroll_offset(float(scroll))
    filename = capture_dir / (name + '.png')
    u.SystemLibrary.execute_console_command(pc.get_world(), 'shot showui filename="' + str(filename) + '" -nosuffix', pc)
    s.setdefault("screenshots", {})[name] = str(filename)
    s["capture_restore_at"] = time.monotonic() + .1


def binary_evidence():
    manifest_name = globals().get("WORKSHOP_PIE_MODULE_MANIFEST", "UnrealEditor.modules")
    manifest_path = project / "Binaries/Win64" / manifest_name
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    module_path = manifest_path.parent / manifest["Modules"]["test_25d"]
    digest = hashlib.sha256()
    with module_path.open("rb") as module:
        for chunk in iter(lambda: module.read(1024 * 1024), b""):
            digest.update(chunk)
    result = {"pid": os.getpid(), "engine": u.SystemLibrary.get_engine_version(),
              "disk_manifest": str(manifest_path), "disk_build_id": manifest["BuildId"],
              "disk_module": str(module_path), "disk_module_sha256": digest.hexdigest(),
              "disk_module_modified_ns": module_path.stat().st_mtime_ns,
              "canvas_reflection_available": hasattr(u, "AbilityRuntimeGraphCanvas")}
    loaded_path = globals().get("WORKSHOP_PIE_LOADED_BUILD")
    if loaded_path:
        result["loaded_build_evidence"] = json.loads(Path(loaded_path).read_text(encoding="utf-8-sig"))
    return result


def graph(snapshot, field="accepted_graph"):
    value = prop(snapshot, field)
    if not prop(value, "ready", "b_ready", "bReady"):
        return None
    return dict(revision=int(prop(value, "revision")), hash=str(prop(value, "content_hash")),
                sources=[str(x) for x in prop(value, "applied_sources")],
                nodes={guid(prop(node, "node_id")): dict(
                    type=str(prop(node, "type_id")), label=str(prop(node, "label")),
                    summary=str(prop(node, "parameter_summary")),
                    ports={str(prop(port, "name")): str(prop(port, "payload")) for port in prop(node, "ports")},
                    modifiers=[str(x) for x in prop(node, "supported_modifiers")]) for node in prop(value, "nodes")},
                edges=sorted((guid(prop(edge, "from", "from_", "From")), str(prop(edge, "output_port")), guid(prop(edge, "to")),
                              str(prop(edge, "payload"))) for edge in prop(value, "edges")))


def matching_graph(local, server, field="accepted_graph"):
    accepted = graph(local, field)
    if accepted is not None and accepted == graph(server, field):
        return accepted
    return None


def unique_node(view, type_id):
    matches = [node_id for node_id, node in view["nodes"].items() if node["type"] == type_id]
    check("unique_" + type_id, len(matches) == 1)
    return matches[0]


def canvas_position(node_id, port=None, to=None):
    canvas = control("RecipeGraphCanvas", u.AbilityRuntimeGraphCanvas)
    key = ui["graph_guids"][node_id]
    if to:
        target = ui["graph_guids"][to]
        value = canvas.get_canvas_edge_position(key, port, target, .86)
    elif port:
        value = canvas.get_canvas_port_position(key, port)
    else:
        value = canvas.get_canvas_node_position(key)
    if isinstance(value, tuple):
        valid, value = value
        if not valid:
            return None
    return [float(value.x), float(value.y)] if value is not None else None


def remember_node_ids(snapshot, field="accepted_graph"):
    for node in prop(prop(snapshot, field), "nodes"):
        node_id = prop(node, "node_id")
        ui.setdefault("graph_guids", {})[guid(node_id)] = node_id.copy()


def parameters(view, node_id):
    summary = view["nodes"][node_id]["summary"]
    return {name: float(value) for name, value in re.findall(r"(\w+):\s*([-+.\deE]+)", summary)}


def duration_is(view, node_id, seconds):
    values = parameters(view, node_id)
    duration = values.get("DurationSeconds", values.get("Duration"))
    return duration is not None and abs(duration - seconds) < .001


def verify_edit(view):
    previous = s["graphs"][s["previous_graph"]]
    edit = s["edit"]
    check(edit + "_new_hash_and_next_revision", view["hash"] != previous["hash"]
          and view["revision"] == previous["revision"] + 1)
    check(edit + "_exactly_one_new_source", len(view["sources"]) == len(previous["sources"]) + 1
          and view["sources"][:-1] == previous["sources"])
    if edit == "propagation":
        delivery = s["delivery_id"]
        check("delivery_strategy_replaced_at_same_node", view["nodes"][delivery]["type"] == "Propagation.Sequential"
              and view["nodes"].keys() == previous["nodes"].keys())
        check("delivery_preserves_all_existing_connections", view["edges"] == previous["edges"])
        values = parameters(view, delivery)
        check("accepted_projection_has_all_four_propagation_parameters", all(
            abs(values.get(name, -1) - expected) < .001 for name, expected in
            {"MaxTargets": 3, "JumpDelay": .3, "JumpRadius": 400, "DamageFalloff": .8}.items()))
    elif edit == "insertion":
        adapter = unique_node(view, "PassThrough.LiveTarget")
        status = unique_node(view, "Status.Petrify")
        src, port, dst, payload = s["inserted_at"]
        expected = (set(previous["edges"]) - {(src, port, dst, payload)}) | {
            (src, port, adapter, payload), (adapter, "Apply", status, payload), (adapter, "Continue", dst, payload)}
        check("exact_wire_replaced_and_every_other_wire_unchanged", set(view["edges"]) == expected)
        check("adapter_and_status_are_the_only_new_nodes", set(view["nodes"]) - set(previous["nodes"]) == {adapter, status}
              and all(view["nodes"].get(key) == node for key, node in previous["nodes"].items()))
        check("inserted_status_effective_duration_is_1_25", duration_is(view, status, 1.25))
        s["status_id"], s["adapter_id"] = status, adapter
    else:
        status = s["status_id"]
        check("scale_keeps_exact_inserted_node_and_edges", view["nodes"][status]["type"] == "Status.Petrify"
              and view["nodes"].keys() == previous["nodes"].keys() and view["edges"] == previous["edges"])
        check("inserted_status_effective_duration_is_2_5", duration_is(view, status, 2.5))


def queue(commands, next_phase):
    library["queue"](commands, next_phase)


def edit_graph(edit, previous, modifier, values, fragment=None, invalid=False):
    s.update(edit=edit, previous_graph=previous, modifier=modifier, values=values, fragment=fragment)
    commands = ["UMG.Click RecipeGraphFit"]
    if edit == "insertion":
        source, port, target, _payload = s["inserted_at"]
        commands.append(f"UMG.GraphEdge {source} {port} {target}")
    else:
        node = s["delivery_id"] if edit == "propagation" else s["status_id"]
        commands.append("UMG.GraphNode " + node)
    commands.append("UMG.Select RecipeModifier " + modifier)
    if fragment:
        commands.append("UMG.Select RecipeFragment " + fragment)
    commands.extend(f"UMG.Value {name} {value}" for name, value in values.items())
    queue(commands, "preview_invalid_click" if invalid else "preview_click")


def check_controls():
    selector = control("RecipeModifier", u.ComboBoxString)
    check(s["edit"] + "_real_canvas_selection_exposes_modifier", selector.get_selected_option() == s["modifier"])
    if s["edit"] == "insertion":
        check("wire_click_exposes_only_exact_edge_insertion", selector.get_option_count() == 1)
    if s["edit"] == "scale":
        options = [selector.get_option_at_index(i) for i in range(selector.get_option_count())]
        s["status_modifier_options"] = options
        check("status_has_no_projectile_only_edit", not any(
            option.startswith("Propagation.") or option in {"Set.Motion", "Set.ImpactPolicy", "Set.ImpactLimit"}
            for option in options))
    if s["fragment"]:
        check("actual_fragment_selector_uses_petrify",
              control("RecipeFragment", u.ComboBoxString).get_selected_option() == s["fragment"])
    check(s["edit"] + "_finite_values_in_actual_spin_boxes", all(
        abs(control(name, u.SpinBox).get_value() - value) < .0001 for name, value in s["values"].items()))


def checkpoint(snapshot):
    return int(prop(snapshot, "state_sequence"))


def draft_id(snapshot):
    return guid(prop(snapshot, "draft_token"))


def require_same_session(local, server):
    check("exact_preview_session_survives_every_edit", all(
        guid(prop(snapshot, "preview_session_id")) == s["session"] for snapshot in (local, server)))


def reject_errors(local, server):
    for snapshot in (local, server):
        library["reject_error"](snapshot)


def read_cast_observation(record):
    return dict(sequence=int(prop(record, "sequence")), kind=str(prop(record, "kind")),
                cast=guid(prop(record, "root_cast_id")), ability=prop(record, "ability_tag").export_text(),
                revision=int(prop(record, "revision")), hash=str(prop(record, "frozen_hash")),
                node=str(prop(record, "node_type")), stage=str(prop(record, "stage")),
                target=str(prop(record, "target_label")), hit=bool(prop(record, "confirmed_hit", "b_confirmed_hit")),
                paid=bool(prop(record, "cost_succeeded", "b_cost_succeeded")),
                resource_before=float(prop(record, "resource_before")), resource_after=float(prop(record, "resource_after")),
                end=str(prop(record, "end_reason")))


def observe_cast(local, server):
    if "cast_sequence" not in s:
        return
    for record in q["new_records"](server, s["cast_sequence"]):
        evidence = read_cast_observation(record)
        ui["rows"][evidence["sequence"]] = evidence
    replicas = q["native_targets"](local.get_world(), local.get_controlled_pawn())
    for side, targets in (("authority", q["owned"]), ("local", replicas)):
        if len(targets) == 3:
            for i, target in enumerate(targets):
                s["status_seen"][side][i] |= q["petrified"](target)
        controller = local if side == "local" else server
        count = sum(actor.get_owner() == controller.get_controlled_pawn() for actor in
                    u.GameplayStatics.get_all_actors_of_class(controller.get_world(), u.MyProjectile))
        s["projectile_peak"][side] = max(count, s["projectile_peak"][side])


def verify_cast(local, server):
    rows = [row for row in ui["rows"].values() if row["ability"] == s["ability_tag"]]
    s["observations"] = sorted(rows, key=lambda row: row["sequence"])
    hits = [row for row in rows if row["hit"] and row["node"] == "Propagation.Sequential" and row["stage"] == "Hit"]
    costs = [row for row in rows if row["kind"] == str(u.AbilityRuntimeObservationKind.COST)]
    s["health_after"] = q["healths"](q["owned"])
    replicas = q["native_targets"](local.get_world(), local.get_controlled_pawn())
    s["local_health_after"] = q["healths"](replicas)
    s["mana_after"] = q["attribute"](q["component"](server, u.MyAbilitySystemComponent), "mana")
    check("real_projectile_visible_in_both_worlds", all(count > 0 for count in s["projectile_peak"].values()))
    check("three_real_contacts_on_distinct_original_lives", len(hits) == 3 and len({h["target"] for h in hits}) == 3)
    check("one_original_cast_id", len({row["cast"] for row in hits + costs}) == 1)
    check("original_t_frontend_paid_once", len(costs) == 1 and costs[0]["paid"]
          and costs[0]["resource_after"] < costs[0]["resource_before"] and s["mana_after"] < s["mana_before"])
    accepted = s["graphs"]["scale"]
    check("all_cast_records_use_exactly_the_applied_draft", all(
        row["revision"] == accepted["revision"] and row["hash"] == accepted["hash"] for row in rows))
    check("all_three_targets_take_real_damage", all(
        after < before for before, after in zip(s["health_before"], s["health_after"])))
    check("all_three_targets_replicate_damage", len(replicas) == 3 and all(
        abs(authority - owner) < .01 for authority, owner in zip(s["health_after"], s["local_health_after"])))
    check("exact_edge_insert_applies_real_status_on_every_target", all(all(side) for side in s["status_seen"].values()))
    finished = [row for row in rows if row["kind"] == str(u.AbilityRuntimeObservationKind.GRAPH_FINISHED)]
    check("real_graph_completes_without_failed_payloads", bool(finished) and all(
        row["end"] == str(u.AbilityGraphEndReason.COMPLETED) for row in finished))


def begin_cast(local, server):
    s["canvas_before_observation"] = canvas_position(s["status_id"])
    check("applied_status_is_rendered_on_actual_canvas", s["canvas_before_observation"] is not None)
    check("applied_status_keeps_canvas_selection", control("RecipeModifier", u.ComboBoxString).get_selected_option() == "Scale.Duration")
    s["cast_sequence"] = max([int(prop(r, "sequence")) for r in q["records"](server)] + [0])
    s["health_before"] = q["healths"](q["owned"])
    s["mana_before"] = q["attribute"](q["component"](server, u.MyAbilitySystemComponent), "mana")
    s["projectile_peak"] = {"authority": 0, "local": 0}
    s["status_seen"] = {"authority": [False] * 3, "local": [False] * 3}
    ui["rows"] = {}
    command(local, "UMG.Click RecipeHide")
    phase("hidden")


original_finalize = q["finalize"]


def finalize(cleanup_succeeded):
    if not cleanup_succeeded:
        q["destroy_owned_fixtures"]()
    if cleanup_succeeded and "pre_fixture_specs" in s:
        actual = {pc.get_path_name(): handles(pc) for pc in q["participants"]() if pc}
        s["restored_specs"] = actual
        s["checks"]["pre_fixture_spec_handles_preserved_after_full_cleanup"] = actual == s["pre_fixture_specs"]
    original_finalize(cleanup_succeeded)


q["finalize"] = finalize


def close_ack(local):
    requested = guid(ui["panel"].get_last_close_request_id())
    received = guid(prop(q["dev"](local), "last_close_acknowledged_id", "LastCloseAcknowledgedId"))
    if requested == EMPTY_GUID or requested != received:
        return None
    return {"request": requested, "ack": received, "succeeded": bool(prop(
        q["dev"](local), "last_close_succeeded", "b_last_close_succeeded", "bLastCloseSucceeded"))}


def recover(error, local):
    s.setdefault("test_error_before_cleanup", str(error))
    s["observations"] = list(ui["rows"].values()) if "rows" in ui else s["observations"]
    if local and ui["panel"] and panel(local) == ui["panel"]:
        command(local, "Release")
        command(local, "UMG.Open")
        phase("recover_open")
    else:
        q["finish"](s["test_error_before_cleanup"])


def tick(_delta):
    local = None
    try:
        if s["phase"].startswith("cleanup"):
            q["cleanup_tick"]()
            return
        local, server, other = q["participants"]()
        name = s["phase"]
        age = time.monotonic() - s.get("at", s["started"])
        if time.monotonic() - s["started"] > 120 and not name.startswith("recover_"):
            raise AssertionError("Timed out in " + name)
        if not local or not server:
            return
        if q["pinned"]:
            check("original_participant_lives_preserved", q["same_participant_lives"]())
        ls, ss = q["preview"](local), q["preview"](server)
        if s.get("capture_restore_at") and time.monotonic() >= s["capture_restore_at"]:
            content_scroll().set_scroll_offset(0.)
            s.pop("capture_restore_at")

        if name == "recover_open":
            widget = panel(local)
            if widget and widget.is_visible():
                ui["panel"] = widget
                command(local, "UMG.Click RecipeClose")
                phase("recover_close")
            elif age > 3:
                q["finish"](s["test_error_before_cleanup"])
            return
        if name == "recover_close":
            ack = close_ack(local)
            if ack and ack["succeeded"] and not prop(ss, "preview_active") and not panel(local):
                s["error_recovery_close"] = ack
                s["owns_preview_request"] = False
                q["finish"](s["test_error_before_cleanup"])
            elif age > 6:
                s["error_recovery_close"] = ack
                s["cleanup_error"] = "Panel could not acknowledge its exact workshop close request"
                s["test_error"] = s["test_error_before_cleanup"]
                finalize(False)
            return

        if name == "setup":
            expected = s["requested_mode"]
            if expected == "ListenServerRemote" and (local == server or not other):
                return
            check("explicit_pie_mode_matches", expected in (None, "Standalone" if local == server else "ListenServerRemote"))
            if "before_fixture" not in s:
                s["before_fixture"] = True
                check("no_preexisting_workshop_panel", panel(local) is None)
                s["pre_fixture_specs"] = {pc.get_path_name(): handles(pc) for pc in {local, server, other} if pc}
            if not q["setup_fixture"]((250, 450, 650)):
                return
            s["original_handles"] = {pc.get_path_name(): handles(pc) for pc in {local, server}}
            command(local, "UMG.Open")
            phase("open")
        elif name == "open":
            widget = panel(local)
            if not widget or not widget.is_visible():
                return
            ui["panel"] = widget
            if ENTRY_MODE == "CharacterMenu":
                menu = local.get_hud().get_character_menu()
                check("real_attribute_button_and_fourth_tab_open_complete_embedded_workshop",
                      menu.is_visible() and menu.get_active_page() == 3
                      and menu.get_runtime_recipe_panel() == widget and not widget.is_in_viewport())
            check("native_canvas_in_owning_live_panel", widget.get_owning_player() == local
                  and bool(control("RecipeGraphCanvas", u.AbilityRuntimeGraphCanvas)))
            row = next(r for r in q["dev"](local).get_definitions()
                       if prop(prop(r, "base"), "ability_class").get_path_name() == FIRE_BOLT
                       and prop(prop(r, "base"), "selectable"))
            s["definition_id"] = str(prop(row, "definition_id"))
            s["ability_class"] = FIRE_BOLT
            queue(["UMG.Select RecipeDefinition " + s["definition_id"]], "begin_click")
        elif name == "ui_queue":
            if age >= .12:
                if s["ui_queue"]:
                    command(local, s["ui_queue"].pop(0))
                    s["at"] = time.monotonic()
                else:
                    phase(s["ui_next"])
        elif name == "begin_click":
            s["begin_state_sequences"] = {pc.get_path_name(): checkpoint(q["preview"](pc)) for pc in {local, server}}
            s["owns_preview_request"] = True
            command(local, "UMG.Click RecipeBegin")
            phase("begin")
        elif name == "begin":
            if not q["preview_started"](local, server):
                return
            base = matching_graph(ls, ss)
            if not base:
                return
            preview_handles = {pc.get_path_name(): handles(pc) for pc in {local, server}}
            if not all(len(current) == len(s["original_handles"][key]) + 1 for key, current in preview_handles.items()):
                return
            s["preview_handles"] = preview_handles
            s["graphs"]["base"] = base
            s["session"] = guid(prop(ls, "preview_session_id"))
            s["ability_tag"] = prop(ls, "ability_tag").export_text()
            check("assigned_exact_graph_preview_session", s["session"] != EMPTY_GUID)
            require_same_session(ls, ss)
            check("base_authoritative_graph_replicates_with_original_hash", bool(base["nodes"] and base["edges"] and base["hash"])
                  and all(prop(snapshot, "base_hash") == prop(snapshot, "content_hash") == base["hash"] for snapshot in (ls, ss)))
            check("original_specs_retained_with_one_exact_preview_grant", all(
                set(s["original_handles"][key]) < set(current) for key, current in preview_handles.items()))
            remember_node_ids(ls)
            s["delivery_id"] = unique_node(base, "Delivery.ProjectileAwait")
            source = unique_node(base, "Target.ResolveImpact")
            target = unique_node(base, "Effect.InputDamage")
            matches = [edge for edge in base["edges"] if edge[:3] == (source, "TargetFound", target)]
            check("real_base_contains_exact_target_found_damage_wire", len(matches) == 1)
            s["inserted_at"] = matches[0]
            if ENTRY_MODE == "CharacterMenu":
                queue([f"UMG.Character.Page {page}" for page in (0, 1, 2, 3)], "tabs_returned")
            else:
                queue(["UMG.Click RecipeGraphFit", f"UMG.GraphPort {source} TargetFound"], "port_selected")
        elif name == "tabs_returned":
            require_same_session(ls, ss)
            check("switching_all_four_tabs_preserves_unique_workshop_spec_and_session",
                  panel(local) == ui["panel"] and local.get_hud().get_character_menu().get_active_page() == 3
                  and all(handles(pc) == s["preview_handles"][pc.get_path_name()] for pc in {local, server}))
            if capture_dir and s["requested_mode"] == "Standalone":
                queue(["UMG.Click RecipeGraphFit"], "capture_fitted_base")
            else:
                queue(["UMG.Click RecipeGraphFit", f"UMG.GraphPort {s['inserted_at'][0]} TargetFound"], "port_selected")
        elif name == "capture_fitted_base":
            capture_step(local, "01-started-base-graph")
            phase("capture_fitted_base_hold")
        elif name == "capture_fitted_base_hold":
            if age >= .25:
                queue([f"UMG.GraphPort {s['inserted_at'][0]} TargetFound"], "port_selected")
        elif name == "port_selected":
            check("actual_output_port_click_exposes_parallel_append", control("RecipeModifier", u.ComboBoxString)
                  .get_selected_option() == "Append.Registered")
            capture_step(local, "02-selected-output-port")
            edit_graph("propagation", "base", "Propagation.Sequential", PROPAGATION_VALUES)
        elif name in {"preview_click", "preview_invalid_click"}:
            check_controls()
            if name == "preview_click" and s["edit"] == "propagation" and capture_dir and s["requested_mode"] == "Standalone":
                capture_step(local, "02-selected-delivery-node", 220)
                phase("captured_propagation_controls")
                return
            if name == "preview_click" and s["edit"] == "scale" and s["requested_mode"] == "Standalone":
                u.SystemLibrary.execute_console_command(local.get_world(), "shot showui", local)
            s["edit_request_checkpoint"] = checkpoint(ls)
            command(local, "UMG.Click RecipePreviewEdit")
            phase("invalid_draft" if name == "preview_invalid_click" else "draft")
        elif name == "captured_propagation_controls":
            if age >= .25:
                s["edit_request_checkpoint"] = checkpoint(ls)
                command(local, "UMG.Click RecipePreviewEdit")
                phase("draft")
        elif name == "invalid_draft":
            if any(checkpoint(snapshot) <= s["edit_request_checkpoint"] for snapshot in (ls, ss)):
                return
            check("zero_duration_scale_rejected_by_real_preview_rpc", all(prop(snapshot, "draft_error") for snapshot in (ls, ss)))
            check("invalid_preview_keeps_accepted_graph_and_no_candidate", matching_graph(ls, ss) == s["graphs"]["insertion"]
                  and draft_id(ls) == draft_id(ss) == EMPTY_GUID)
            edit_graph("scale", "insertion", "Scale.Duration", {"RecipeValue": 2})
        elif name == "draft":
            if any(checkpoint(snapshot) <= s["edit_request_checkpoint"] for snapshot in (ls, ss)):
                return
            reject_errors(ls, ss)
            require_same_session(ls, ss)
            draft = matching_graph(ls, ss, "draft_graph")
            token = draft_id(ls)
            if not draft or token == EMPTY_GUID or token != draft_id(ss):
                return
            edit = s["edit"]
            check(edit + "_accepted_is_unchanged_on_both_sides_during_preview",
                  matching_graph(ls, ss) == s["graphs"][s["previous_graph"]])
            check(edit + "_canvas_draft_is_read_only", not control("RecipePreviewEdit", u.Button).get_is_enabled())
            s["graphs"][edit + "_draft"] = draft
            s.setdefault("draft_tokens", {})[edit] = token
            verify_edit(draft)
            if edit == "propagation":
                capture_step(local, "03-propagation-draft", 220)
            remember_node_ids(ls, "draft_graph")
            if edit == "insertion":
                check("readonly_draft_renders_new_status", canvas_position(s["status_id"]) is not None)
                check("readonly_draft_no_longer_draws_selected_original_wire", canvas_position(*s["inserted_at"][:3]) is None)
                queue(["UMG.Click RecipeGraphAccepted"], "compare_accepted")
            elif edit == "propagation" and capture_dir and s["requested_mode"] == "Standalone":
                # 保持真实草稿至少一个渲染帧，避免可靠 Apply 在截图读回前就把它替换成已接受图。
                phase("captured_propagation_draft")
            else:
                command(local, "UMG.Click RecipeApplyDraft")
                phase("applied")
        elif name == "captured_propagation_draft":
            if age >= .25:
                command(local, "UMG.Click RecipeApplyDraft")
                phase("applied")
        elif name == "compare_accepted":
            check("accepted_comparison_renders_original_wire_while_draft_exists", canvas_position(*s["inserted_at"][:3]) is not None
                  and canvas_position(s["status_id"]) is None and draft_id(ls) == s["draft_tokens"]["insertion"])
            queue(["UMG.Click RecipeGraphDraft", "UMG.Click RecipeApplyDraft"], "applied")
        elif name == "applied":
            reject_errors(ls, ss)
            draft = s["graphs"][s["edit"] + "_draft"]
            view = matching_graph(ls, ss)
            if not view or view["revision"] < draft["revision"] or draft_id(ls) != EMPTY_GUID or draft_id(ss) != EMPTY_GUID:
                return
            require_same_session(ls, ss)
            edit = s["edit"]
            s["graphs"][edit] = view
            check(edit + "_exact_draft_promoted_on_both_worlds", view == draft)
            check(edit + "_stale_apply_button_disabled_after_commit", not control("RecipeApplyDraft", u.Button).get_is_enabled())
            remember_node_ids(ls)
            if edit == "propagation":
                check("same_delivery_node_remains_on_accepted_canvas", canvas_position(s["delivery_id"]) is not None)
                capture_step(local, "04-applied-propagation")
                edit_graph("insertion", "propagation", "Insert.Registered", {"RecipeValue": 1.25}, fragment="Target.Petrify")
            elif edit == "insertion":
                check("applied_canvas_replaced_exact_wire", canvas_position(*s["inserted_at"][:3]) is None
                      and canvas_position(s["status_id"]) is not None)
                edit_graph("scale", "insertion", "Scale.Duration", {"RecipeValue": 0}, invalid=True)
            else:
                begin_cast(local, server)
        elif name == "hidden":
            if ui["panel"].is_visible():
                return
            check("hide_keeps_same_owned_panel_and_accepted_session", panel(local) == ui["panel"] and prop(ls, "preview_active"))
            check("hide_restores_real_owner_viewport_focus", ui["panel"].has_owner_viewport_focus())
            phase("aim")
        elif name == "aim":
            replicas = q["ready_replicas"]()
            if not replicas or (not s.get("corridor_verified") and not q["projectile_corridor_ready"]()):
                return
            q["aim_at"](replicas[0])
            if age > .5 and q["dev"](local).is_preview_frontend_idle() and q["dev"](server).is_preview_frontend_idle():
                command(local, "Press")
                phase("press")
        elif name == "press":
            observe_cast(local, server)
            if age >= .12:
                command(local, "Release")
                phase("observe")
        elif name == "observe":
            observe_cast(local, server)
            if age >= 6:
                verify_cast(local, server)
                command(local, "UMG.Open")
                phase("reopen")
        elif name == "reopen":
            if not ui["panel"].is_visible():
                return
            require_same_session(ls, ss)
            check("reopen_keeps_same_panel", panel(local) == ui["panel"])
            check("observation_refresh_keeps_selected_status_and_camera_position",
                  control("RecipeModifier", u.ComboBoxString).get_selected_option() == "Scale.Duration"
                  and canvas_position(s["status_id"]) == s["canvas_before_observation"])
            command(local, "UMG.Click RecipeUndoLast")
            phase("undo_scale")
        elif name in {"undo_scale", "undo_insertion", "reset"}:
            reject_errors(ls, ss)
            key = {"undo_scale": "insertion", "undo_insertion": "propagation", "reset": "base"}[name]
            expected = s["graphs"][key]
            view = matching_graph(ls, ss)
            if not view or view["hash"] != expected["hash"]:
                return
            require_same_session(ls, ss)
            check(name + "_returns_to_expected_original_content", all(view[k] == expected[k] for k in ("nodes", "edges", "sources")))
            s["graphs"][name] = view
            if name == "undo_scale":
                check("undo_scale_keeps_inserted_status_at_1_25", duration_is(view, s["status_id"], 1.25))
                command(local, "UMG.Click RecipeUndoLast")
                phase("undo_insertion")
            elif name == "undo_insertion":
                check("undo_insertion_restores_exact_original_wire_on_canvas", canvas_position(*s["inserted_at"][:3]) is not None
                      and canvas_position(s["status_id"]) is None)
                command(local, "UMG.Click RecipeReset")
                phase("reset")
            else:
                check("history_actions_preserve_exact_spec_handles_while_panel_open", all(
                    handles(pc) == s["preview_handles"][pc.get_path_name()] for pc in {local, server}))
                command(local, "UMG.Character.Close" if ENTRY_MODE == "CharacterMenu" else "UMG.Click RecipeClose")
                phase("closed")
        elif name == "closed":
            ack = close_ack(local)
            if not ack or panel(local) or any(prop(snapshot, "preview_active") for snapshot in (ls, ss)):
                return
            s["exact_panel_close"] = ack
            check("exact_panel_close_ack_success", ack["succeeded"])
            s["owns_preview_request"] = False
            if not all(handles(pc) == s["original_handles"][pc.get_path_name()] for pc in {local, server}):
                return
            check("preview_spec_reclaimed_and_previous_grants_preserved", True)
            check("close_restores_real_owner_viewport_focus", ui["panel"].has_owner_viewport_focus())
            if q["ALLOW_UNSELECTED_MAGE"]:
                check("direct_pie_profession_remains_unselected_after_composing_and_casting", all(
                    pc.get_editor_property("player_state").get_editor_property("profession") == u.PlayerProfession.UNSELECTED
                    for pc in {local, server}))
            if other:
                check("listen_host_nonowner_preview_isolated", q["preview"](other).export_text() == s["other_preview_before"])
            q["finish"]()
    except Exception as error:
        if s["phase"].startswith(("cleanup", "recover_")):
            s["cleanup_error"] = repr(error)
            finalize(False)
        else:
            recover(repr(error), local)


if not globals().get("_WORKSHOP_PIE_LIBRARY_ONLY", False):
    try:
        s["binary"] = binary_evidence()
        check("built_graph_canvas_reflected", s["binary"]["canvas_reflection_available"])
        q["handle"] = u.register_slate_post_tick_callback(tick)
        q["write"]()
        u.log("RUNTIME_GRAPH_WORKSHOP_STARTED " + str(q["run"] / "report.json"))
    except Exception as error:
        s.update(finished=True, passed=False, error=repr(error))
        q["write"]()
        raise
