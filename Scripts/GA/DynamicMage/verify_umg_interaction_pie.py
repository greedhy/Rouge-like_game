"""Real local native UMG controls, original T casting, and owner-only preview RPC.

Use a built development module with RuntimeRecipe.Probe UMG.* and the panel's
read-only focus/close diagnostics. The bounded probe selects actual WidgetTree
controls and broadcasts their original button delegates on a native tick. It
does not substitute ApplyEdit, inject TargetData/Notify, or synthesize OS clicks.
Supports the explicit empty Standalone or two-player Listen QA map only.
"""
import time
from pathlib import Path
import unreal as u

q = {"_DYNAMIC_MAGE_LIBRARY_ONLY": True, "__name__": "dynamic_mage_umg_qa"}
exec((Path(u.Paths.project_dir()) / "Scripts/GA/DynamicMage/verify_real_casting_pie.py").read_text(encoding="utf-8"), q)
s = q["state"]
s.update(scenario="RealWidgetTreeSelectionApplyHideCastRemoveClose",
         scope="Actual native UMG selectors and original OnClicked delegates; original local T/cursor/Notify and preview RPC.",
         limitations=["Not OS mouse/keyboard automation", "No artistic visual-quality certification"])
ui = {"panel": None}
observed = {}


def panel(local):
    hud = local.get_hud()
    menu = hud.get_character_menu() if isinstance(hud, u.MyHUD) else None
    embedded = menu.get_runtime_recipe_panel() if menu else None
    if menu and menu.is_in_viewport() and embedded and not embedded.is_session_closed():
        return embedded
    return next((w for w in u.WidgetLibrary.get_all_widgets_of_class(local.get_world(), u.AbilityRuntimeRecipePanel, False)
                 if w.get_owning_player() == local and w.is_in_viewport()), None)


def control(name, cls):
    for widget in u.ObjectIterator(cls):
        if widget.get_name() != name:
            continue
        outer = widget.get_outer()
        for _ in range(8):
            if outer == ui["panel"]:
                return widget
            if not outer:
                break
            outer = outer.get_outer()
    raise AssertionError("The real panel has no semantic control " + name)


def handles(controller):
    # Public GAS Blueprint API; canonical reflected struct serialization avoids
    # Python wrapper identity and does not read private native Spec members.
    return sorted(h.export_text() for h in q["component"](controller, u.MyAbilitySystemComponent).get_all_abilities())


def queue(commands, next_phase):
    s["ui_queue"] = list(commands)
    s["ui_next"] = next_phase
    q["phase"]("ui_queue")


def reject_error(snapshot):
    error = str(snapshot.get_editor_property("error"))
    if error:
        raise AssertionError("Original panel request rejected: " + error)


def finish(error=None):
    # First let this panel finish its own exact asynchronous close, then let the
    # shared cleanup send its independently identified close if still needed.
    local, _server, _other = q["participants"]()
    if error and local and panel(local):
        s["pending_ui_error"] = str(error)
        q["command"](local, "UMG.Open")
        q["phase"]("recover_open")
    else:
        q["finish"](error)


def tick(_delta):
    try:
        if s["phase"].startswith("cleanup"):
            q["cleanup_tick"]()
            return
        local, server, other = q["participants"]()
        if not local or not server:
            return
        age = time.monotonic() - s.get("at", s["started"])
        if s["phase"] == "recover_open":
            p = panel(local)
            if p and p.is_visible():
                q["command"](local, "UMG.Click RecipeClose")
                q["phase"]("recover_close")
            elif age > 3:
                q["finish"](s["pending_ui_error"])
            return
        if s["phase"] == "recover_close":
            if not panel(local) or age > 3:
                q["finish"](s["pending_ui_error"])
            return
        if time.monotonic() - s["started"] > 100:
            raise AssertionError("Timed out in " + s["phase"])
        if q["pinned"]:
            q["check"]("pinned_participant_lives_unchanged", q["same_participant_lives"]())
        ls, ss = q["preview"](local), q["preview"](server)
        if "observation_sequence" in s:
            for record in q["new_records"](server, s["observation_sequence"]):
                observed[int(record.get_editor_property("sequence"))] = dict(
                    kind=str(record.get_editor_property("kind")),
                    paid=bool(record.get_editor_property("cost_succeeded")),
                    tag=record.get_editor_property("ability_tag").export_text(),
                    revision=int(record.get_editor_property("revision")),
                    frozen_hash=str(record.get_editor_property("frozen_hash")),
                    end_reason=str(record.get_editor_property("end_reason")))
        if s["phase"] == "setup":
            if not q["setup_fixture"]((250, 450, 650)):
                return
            q["check"]("no_preexisting_recipe_panel", panel(local) is None)
            s["original_handles"] = {p.get_path_name(): handles(p) for p in {local, server}}
            q["command"](local, "UMG.Open")
            q["phase"]("open")
        elif s["phase"] == "open":
            p = panel(local)
            if not p or not p.is_visible():
                return
            ui["panel"] = p
            q["check"]("actual_owned_panel_is_visible", p.get_owning_player() == local)
            # The original Open path initializes the local trusted catalog.
            # A fresh PlayerState need not expose any definition before it.
            row = next(r for r in q["dev"](local).get_definitions()
                       if r.get_editor_property("base").get_editor_property("ability_class").get_path_name() ==
                       "/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/GA_FireBolt.GA_FireBolt_C")
            s["definition_id"] = str(row.get_editor_property("definition_id"))
            queue(["UMG.Select RecipeDefinition " + s["definition_id"]], "begin_click")
        elif s["phase"] == "ui_queue":
            if age < .1:
                return
            if s["ui_queue"]:
                q["command"](local, s["ui_queue"].pop(0))
                s["at"] = time.monotonic()
            else:
                q["phase"](s["ui_next"])
        elif s["phase"] == "begin_click":
            s["begin_state_sequences"] = {p.get_path_name(): int(q["preview"](p).get_editor_property("state_sequence"))
                                          for p in {local, server}}
            s["owns_preview_request"] = True
            q["command"](local, "UMG.Click RecipeBegin")
            q["phase"]("begin")
        elif s["phase"] == "begin":
            if not q["preview_started"](local, server):
                return
            current = {p.get_path_name(): handles(p) for p in {local, server}}
            if not all(len(current[p.get_path_name()]) == len(s["original_handles"][p.get_path_name()]) + 1 for p in {local, server}):
                return
            q["check"]("actual_ui_begin_grants_one_temporary_spec_each_world", True)
            s["preview_handles"] = current
            index = q["node_index"](local, "Delivery.ProjectileAwait")
            s["node"] = index
            queue([f"UMG.Select RecipeNode {index}", "UMG.Select RecipeModifier Append.Registered",
                   "UMG.Select RecipeFragment Target.Petrify", "UMG.Select RecipePort Hit",
                   "UMG.Value RecipeValue 1.5"], "apply_click")
        elif s["phase"] == "apply_click":
            q["check"]("actual_widget_modifier_selection", control("RecipeModifier", u.ComboBoxString).get_selected_option() == "Append.Registered")
            q["check"]("actual_widget_fragment_selection", control("RecipeFragment", u.ComboBoxString).get_selected_option() == "Target.Petrify")
            q["check"]("actual_widget_port_selection", control("RecipePort", u.ComboBoxString).get_selected_option() == "Hit")
            q["check"]("actual_widget_finite_duration", abs(control("RecipeValue", u.SpinBox).get_value() - 1.5) < .001)
            s["revision_before"] = int(ls.get_editor_property("revision"))
            q["command"](local, "UMG.Click RecipeApply")
            q["phase"]("applied")
        elif s["phase"] == "applied":
            reject_error(ls)
            if int(ls.get_editor_property("revision")) <= s["revision_before"] or int(ls.get_editor_property("revision")) != int(ss.get_editor_property("revision")):
                return
            s["actual_duration_after_click_focus_commit"] = float(control("RecipeValue", u.SpinBox).get_value())
            q["check"]("requested_duration_survives_original_apply_focus_commit",
                         abs(s["actual_duration_after_click_focus_commit"] - 1.5) < .001)
            q["check"]("original_umg_apply_reaches_real_authority_recipe", ls.get_editor_property("content_hash") != ls.get_editor_property("base_hash"))
            q["check"]("actual_control_focus_belongs_to_panel", ui["panel"].has_user_focused_descendants(local))
            s["observation_sequence"] = max([int(r.get_editor_property("sequence")) for r in q["records"](server)] + [0])
            s["accepted_revision"] = int(ss.get_editor_property("revision"))
            s["accepted_hash"] = str(ss.get_editor_property("content_hash"))
            s["selected_tag"] = ss.get_editor_property("ability_tag").export_text()
            s["before_health"] = q["healths"](q["owned"])
            s["authority_status"] = False
            s["remote_status"] = False
            q["command"](local, "UMG.Click RecipeHide")
            q["phase"]("hidden")
        elif s["phase"] == "hidden":
            if ui["panel"].is_visible():
                return
            q["check"]("hide_keeps_same_panel_and_preview_lease", panel(local) == ui["panel"] and ls.get_editor_property("preview_active"))
            q["check"]("hide_restores_actual_owner_viewport_focus", ui["panel"].has_owner_viewport_focus())
            replicas = q["ready_replicas"]()
            if not replicas:
                return
            q["aim_at"](replicas[0])
            q["phase"]("aim")
        elif s["phase"] == "aim":
            replicas = q["ready_replicas"]()
            if replicas:
                q["aim_at"](replicas[0])
            if age > .5 and q["dev"](local).is_preview_frontend_idle() and q["dev"](server).is_preview_frontend_idle():
                q["command"](local, "Press")
                q["phase"]("press")
        elif s["phase"] == "press":
            if age > .12:
                q["command"](local, "Release")
                q["phase"]("observe")
        elif s["phase"] == "observe":
            # Read current original lives after damage; setup readiness requires
            # full HP and is intentionally unsuitable after the first hit.
            replicas = q["native_targets"](local.get_world(), local.get_controlled_pawn())
            s["authority_status"] |= q["petrified"](q["owned"][0])
            s["remote_status"] |= bool(replicas) and q["petrified"](replicas[0])
            hp = q["healths"](q["owned"])
            if age < 4:
                return
            q["check"]("hidden_panel_allows_original_t_cursor_notify_and_damage", hp[0] < s["before_health"][0])
            q["check"]("actual_umg_authored_status_is_seen_on_both_worlds", s["authority_status"] and s["remote_status"])
            rows = [r for r in observed.values() if r["tag"] == s["selected_tag"]]
            q["check"]("original_frontend_charged_once", sum(r["kind"] == str(u.AbilityRuntimeObservationKind.COST) and r["paid"] for r in rows) == 1)
            q["check"]("all_cast_records_keep_accepted_revision_and_hash", bool(rows) and all(
                r["revision"] == s["accepted_revision"] and r["frozen_hash"] == s["accepted_hash"] for r in rows))
            bad_endings = {str(u.AbilityGraphEndReason.FAILED), str(u.AbilityGraphEndReason.BUDGET_EXCEEDED), str(u.AbilityGraphEndReason.SOURCE_INVALID)}
            q["check"]("no_branch_failure_hidden_by_a_successful_hit", all(r["end_reason"] not in bad_endings for r in rows
                         if r["kind"] == str(u.AbilityRuntimeObservationKind.GRAPH_FINISHED)))
            s["public_observations"] = rows
            q["command"](local, "UMG.Open")
            q["phase"]("reopen")
        elif s["phase"] == "reopen":
            if not ui["panel"].is_visible():
                return
            q["check"]("reopen_reuses_same_native_widget", panel(local) == ui["panel"])
            q["command"](local, "UMG.Click RecipeRemove")
            q["phase"]("removed")
        elif s["phase"] == "removed":
            reject_error(ls)
            if ls.get_editor_property("content_hash") != ls.get_editor_property("base_hash") or ss.get_editor_property("content_hash") != ss.get_editor_property("base_hash"):
                return
            q["check"]("actual_umg_remove_restores_original_recipe", True)
            q["check"]("removal_keeps_exact_preview_spec", all(handles(p) == s["preview_handles"][p.get_path_name()] for p in {local, server}))
            q["command"](local, "UMG.Click RecipeClose")
            q["phase"]("closed")
        elif s["phase"] == "closed":
            requested = q["guid_hex"](ui["panel"].get_last_close_request_id())
            received = q["guid_hex"](q["property_value"](q["dev"](local), "last_close_acknowledged_id", "LastCloseAcknowledgedId"))
            if requested == "0" * 32 or received != requested or panel(local):
                return
            q["check"]("original_ui_close_receives_its_exact_ack", q["property_value"](q["dev"](local), "last_close_succeeded", "bLastCloseSucceeded"))
            if ls.get_editor_property("preview_active") or ss.get_editor_property("preview_active"):
                return
            if not all(handles(p) == s["original_handles"][p.get_path_name()] for p in {local, server}):
                return
            q["check"]("exact_temporary_spec_removed_and_prior_grants_preserved", True)
            q["check"]("close_restores_actual_owner_viewport_focus", ui["panel"].has_owner_viewport_focus())
            if other:
                q["check"]("other_player_recipe_isolated", q["preview"](other).export_text() == s["other_preview_before"])
            s["owns_preview_request"] = False
            finish()
    except Exception as error:
        finish(repr(error))


if not globals().get("_DYNAMIC_MAGE_UMG_LIBRARY_ONLY", False):
    q["handle"] = u.register_slate_post_tick_callback(tick)
    q["write"]()
    u.log("DYNAMIC_MAGE_UMG_STARTED " + str(q["run"] / "report.json"))
