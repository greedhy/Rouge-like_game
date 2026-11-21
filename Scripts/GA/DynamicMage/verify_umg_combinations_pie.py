"""Select one approved cross-axis combination through the actual native UMG.

Set DYNAMIC_MAGE_UMG_SCENE to an allowlisted scene below. Each run has a separate
report and exact fixture/preview cleanup. This script uses only actual panel
selectors, finite spin controls and original button delegates, then original
T/cursor/Notify. It never bakes a graph, calls ApplyEdit directly, or injects an
activation/target/damage callback. Run with the current built UMG probe ABI.
"""
import time
from pathlib import Path
import unreal as u

library = {"_DYNAMIC_MAGE_UMG_LIBRARY_ONLY": True, "__name__": "dynamic_mage_umg_combinations"}
exec((Path(u.Paths.project_dir()) / "Scripts/GA/DynamicMage/verify_umg_interaction_pie.py").read_text(encoding="utf-8"), library)
q, ui = library["q"], library["ui"]
s = q["state"]
name = globals().get("DYNAMIC_MAGE_UMG_SCENE", "EmitterBeam")


def edit(node, modifier, value, fragment=None, port=None, radius=500, interval=.1, scope_for=None):
    return dict(node=node, modifier=modifier, value=value, fragment=fragment,
                port=port, radius=radius, interval=interval, scope_for=scope_for)


def proto(kind):
    return f"/Game/PureMass/Abilities/GA_Prototype_{kind}.GA_Prototype_{kind}_C"


def extension(kind):
    return f"/Game/PrototypeExtensions/Batch5/GA_Prototype_{kind}.GA_Prototype_{kind}_C"


scope = "Scope.Prototype"
scenes = {
    "Sequential": dict(path="/Game/BluePrints/AbilitySystem/Player/Ability/Fire/FireBolt/GA_FireBolt.GA_FireBolt_C", edits=[
        edit("Delivery.ProjectileAwait", "Propagation.Sequential", 1, radius=400, interval=.25),
        edit("Delivery.ProjectileAwait", "Append.Registered", 1.5, "Target.Petrify", "Hit")], min_targets=3),
    "ScatterHoming": dict(path=proto("Multishot"), edits=[edit(scope, "Set.Count", 3), edit(scope, "Set.Spread", 0),
        edit("Distribution.PrototypeMultishot", "Set.PelletTargetPolicy", 1),
        edit("Delivery.PrototypeFlight", "Set.Motion", 1),
        edit("Delivery.PrototypeFlight", "Append.Registered", 1.5, "Target.Petrify", "Contact")]),
    "ScatterPiercing": dict(path=proto("Multishot"), edits=[edit(scope, "Set.Count", 3), edit(scope, "Set.Spread", 0),
        edit("Delivery.PrototypeFlight", "Set.ImpactPolicy", 1), edit("Delivery.PrototypeFlight", "Set.ImpactLimit", 3),
        edit("Delivery.PrototypeFlight", "Set.DamageFalloff", .5),
        edit("Delivery.PrototypeFlight", "Append.Registered", 1.5, "Target.Petrify", "Contact")], min_targets=2, aim_index=2),
    "EmitterBeam": dict(path=proto("Emitter"), edits=[edit(scope, "Set.Count", 3), edit(scope, "Set.Period", .1),
        edit("Control.PrototypeEmitter", "Replace.RegisteredBranch", .3, "Prototype.Beam", "Launch", interval=.1)],
        state_required=False, beam_host_required=True),
    "Field": dict(path=proto("Field"), edits=[edit(scope, "Set.Radius", 450), edit(scope, "Set.Duration", 3),
        edit("Control.FieldCadence", "Append.Registered", 1.5, "Area.Petrify", "Period", radius=450)]),
    "Trap": dict(path=proto("Trap"), edits=[edit(scope, "Set.ArmDelay", .3),
        edit("Control.PrototypeTrap", "Append.Registered", 1.5, "Area.Petrify", "Action", radius=450)]),
    "Delay": dict(path=proto("Delay"), edits=[edit(scope, "Set.ArmDelay", .3),
        edit("Control.PrototypeDelay", "Append.Registered", 1.5, "Area.Petrify", "Expired", radius=450)]),
    "Attach": dict(path=extension("Attach"), edits=[edit(scope, "Set.Period", .2), edit(scope, "Set.Duration", 3),
        edit(scope, "Set.MaxTransfers", 0),
        edit("State.PrototypeAttach", "Append.Registered", 1.5, "Target.Petrify", "Period")]),
    "Stack": dict(path=extension("Stack"), edits=[edit(scope, "Set.StackThreshold", 2, scope_for="State.PrototypeStack"),
        edit(scope, "Set.DetonationDelay", .2, scope_for="State.PrototypeStack"),
        edit(scope, "Set.StackLifetime", 20, scope_for="State.PrototypeStack"),
        edit("State.PrototypeStack", "Append.Registered", 1.5, "Area.Petrify", "Threshold", radius=450)], casts=2),
    "Move": dict(path=extension("Displace"), edits=[edit(scope, "Set.MoveDistance", 100),
        edit(("Movement.PrototypeTargetDisplace", "Movement.PrototypeAreaDisplace"),
             "Append.Registered", 1.5, "Area.Petrify", "Completed", radius=450)], move_required=True, damage_required=False),
    "Wave": dict(path=proto("Wave"), edits=[edit(scope, "Set.Radius", 800), edit(scope, "Set.Speed", 400),
        edit(scope, "Set.Duration", 2),
        edit("Spatial.PrototypeWave", "Append.Registered", 1.5, "Target.Petrify", "TargetFound")]),
    "Shards": dict(path="/Game/BluePrints/AbilitySystem/Player/Ability/Arcane/ArcaneChard/GA_ArcaneShards.GA_ArcaneShards_C",
        edits=[edit("Effect.ShardDamageBatch", "Replace.Status.Petrify", 1.5)], damage_required=False),
    "Channel": dict(path="/Game/BluePrints/AbilitySystem/Player/Ability/Thunder/GA_Electrocute.GA_Electrocute_C",
        edits=[edit("Effect.ElectrocutePeriod", "Replace.Status.Petrify", 1.5)], hold=1.5),
    "Orbit": dict(path="/Game/BluePrints/AbilitySystem/Player/Ability/PassiveSpell/OrbitingFireBolt/GA_OrbitingFireBolt.GA_OrbitingFireBolt_C",
        edits=[edit("Effect.InputDamage", "Append.Status.Petrify", 1.5)], primary_required=True),
}
if name not in scenes:
    raise ValueError("Unknown bounded UMG scene: " + str(name))
scene = scenes[name]
s.update(scenario="RealUMG." + name, requested_scene=scene,
         scope="Actual trusted catalog row, real UMG controls/RPC, original local input and cursor/Notify; real actor HP/status.",
         limitations=["Not OS mouse/keyboard injection", "Detailed cancellation/mixed-Mass boundaries are separate automated cases", "Visual quality remains user acceptance"])
cache = {}
host_refs = []


def vector_value(value):
    return dict(x=float(value.x), y=float(value.y), z=float(value.z))


def save_evidence(server):
    """Persist observations before any assertion, including a failing path."""
    s["public_observations"] = list(cache.values())
    if q["owned"]:
        s["current_health"] = q["healths"](q["owned"])
        s["current_positions"] = [vector_value(a.get_actor_location()) for a in q["owned"]]
    s["source_position"] = vector_value(server.get_controlled_pawn().get_actor_location())
    s["source_forward"] = vector_value(server.get_controlled_pawn().get_actor_forward_vector())
    s["current_mana"] = q["attribute"](q["component"](server, u.MyAbilitySystemComponent), "mana")


def phase(value):
    q["phase"](value)


def nodes(local):
    row = next(r for r in q["dev"](local).get_definitions() if str(r.get_editor_property("definition_id")) == s["definition_id"])
    return list(row.get_editor_property("base").get_editor_property("default_graph").get_editor_property("nodes"))


def scope_owning(local, target_type):
    """Follow actual incoming edges to the closest unique Scope, not first Kind.

    Stack's real asset is a projectile whose child Scope owns shared layers;
    configuring the projectile root would falsely edit/reject the wrong axis.
    """
    row = next(r for r in q["dev"](local).get_definitions() if str(r.get_editor_property("definition_id")) == s["definition_id"])
    graph = row.get_editor_property("base").get_editor_property("default_graph")
    items = list(graph.get_editor_property("nodes"))
    ids = {q["guid_hex"](n.get_editor_property("node_id")): i for i, n in enumerate(items)}
    target = next(i for i, n in enumerate(items) if str(n.get_editor_property("type_id")) == target_type)
    parents = {}
    for edge in graph.get_editor_property("edges"):
        before = q["guid_hex"](q["property_value"](edge, "From", "from", "from_"))
        after = q["guid_hex"](q["property_value"](edge, "To", "to"))
        parents.setdefault(after, []).append(before)
    frontier = [q["guid_hex"](items[target].get_editor_property("node_id"))]
    visited = set()
    for _ in range(len(items)):
        frontier = [p for child in frontier for p in parents.get(child, []) if p not in visited]
        visited.update(frontier)
        candidates = {ids[p] for p in frontier if str(items[ids[p]].get_editor_property("type_id")) == scope}
        if candidates:
            if len(candidates) != 1:
                raise AssertionError("Ambiguous actual owning Scope for " + target_type)
            return candidates.pop()
    raise AssertionError("No actual Scope feeds " + target_type)


def select_edit(local, position):
    request = scene["edits"][position]
    types = request["node"] if isinstance(request["node"], tuple) else (request["node"],)
    index = (scope_owning(local, request["scope_for"]) if request.get("scope_for") else
             next(i for i, n in enumerate(nodes(local)) if str(n.get_editor_property("type_id")) in types))
    commands = [f"UMG.Select RecipeNode {index}", "UMG.Select RecipeModifier " + request["modifier"]]
    if request["fragment"]:
        commands += ["UMG.Select RecipeFragment " + request["fragment"], "UMG.Select RecipePort " + request["port"]]
    if request["modifier"].startswith("Propagation."):
        commands += [f"UMG.Value RecipeRadius {request['radius']}", f"UMG.Value RecipeInterval {request['interval']}"]
    else:
        commands += [f"UMG.Value RecipeValue {request['value']}"]
    if request["fragment"] == "Area.Petrify":
        commands += [f"UMG.Value RecipeRadius {request['radius']}"]
    if request["fragment"] == "Prototype.Beam":
        commands += [f"UMG.Value RecipeInterval {request['interval']}"]
    library["queue"](commands, "remove_click" if s.get("removing") else "apply_click")


def observe(server, local):
    # Capture public observations every actual frame. High-frequency Wave/
    # Scatter events can legitimately evict the initial Cost from the ring.
    for record in q["records"](server):
        sequence = int(record.get_editor_property("sequence"))
        if sequence > s.get("before_sequence", 2**63 - 1):
            cache[sequence] = dict(cost=record.get_editor_property("kind") == u.AbilityRuntimeObservationKind.COST,
                                   paid=bool(record.get_editor_property("cost_succeeded")),
                                   kind=str(record.get_editor_property("kind")),
                                   end_reason=str(record.get_editor_property("end_reason")),
                                   frozen_hash=str(record.get_editor_property("frozen_hash")),
                                   ability_tag=record.get_editor_property("ability_tag").export_text(),
                                   attempted=bool(record.get_editor_property("damage_attempted")),
                                   cast=q["guid_hex"](record.get_editor_property("root_cast_id")),
                                   invocation=q["guid_hex"](record.get_editor_property("invocation_id")),
                                   stage=str(record.get_editor_property("stage")),
                                   node_type=str(record.get_editor_property("node_type")),
                                   resource_before=float(record.get_editor_property("resource_before")),
                                   resource_after=float(record.get_editor_property("resource_after")),
                                   revision=int(record.get_editor_property("revision")))
    if len(cache) > 20000:
        raise AssertionError("Bounded QA observation cache exceeded")
    replicas = q["native_targets"](local.get_world(), local.get_controlled_pawn())
    for side, actors in (("authority_status", q["owned"]), ("remote_status", replicas)):
        if len(actors) == 3:
            for index, actor in enumerate(actors):
                s[side][index] |= q["petrified"](actor)
    for actor in q["effect_hosts"](server):
        # Keep native object identities separately from reusable actor paths.
        # References do not stop Destroy/EndPlay; they only let QA distinguish
        # different observed lifetimes if an old name is reused after expiry.
        birth = next((i for i, old in enumerate(host_refs) if u.SystemLibrary.is_valid(old) and old == actor), None)
        if birth is None:
            birth = len(host_refs)
            host_refs.append(actor)
        projection = dict(birth=birth, actor=actor.get_path_name(), cls=actor.get_class().get_name(),
                          position=vector_value(actor.get_actor_location()), direction=vector_value(actor.get_actor_forward_vector()),
                          server_seconds=float(u.GameplayStatics.get_time_seconds(server.get_world())))
        s.setdefault("actual_hosts", {})[actor.get_path_name()] = actor.get_class().get_name()
        if isinstance(actor, (u.PrototypeRuntime, u.PureMassRuntime, u.ExtensionRuntime)):
            projection["frozen_radius_cm"] = float(actor.get_presentation_radius())
            projection["frozen_kind"] = int(actor.get_presentation_kind())
            # Pure presentation hosts intentionally do not populate legacy
            # Prototype. Read only the received immutable projection; source
            # assets cannot identify a runtime replacement's actual kind.
            if projection["frozen_kind"] == u.AbilityPrototypeKind.BEAM.value:
                s.setdefault("actual_beam_hosts", {})[str(birth)] = dict(actor=actor.get_path_name(),
                    frozen_kind=projection["frozen_kind"])
        s.setdefault("host_projections", {})[str(birth)] = projection
    s.setdefault("sampled_actor_lives", {})[str(len(s.get("sampled_actor_lives", {})))] = dict(
        server_seconds=float(u.GameplayStatics.get_time_seconds(server.get_world())),
        health=q["healths"](q["owned"]), position=[vector_value(a.get_actor_location()) for a in q["owned"]])
    # A few seconds at ordinary frame rates stays well below this diagnostic cap.
    if len(s["sampled_actor_lives"]) > 20000:
        raise AssertionError("Bounded QA actor-life sampling capacity exceeded")


def tick(_delta):
    local = server = other = None
    try:
        if s["phase"].startswith("cleanup"):
            q["cleanup_tick"]()
            return
        local, server, other = q["participants"]()
        if not local or not server:
            return
        age = time.monotonic() - s.get("at", s["started"])
        if s["phase"] in ("recover_open", "recover_close"):
            library["tick"](_delta)
            return
        if time.monotonic() - s["started"] > 140:
            raise AssertionError("Timed out in " + s["phase"])
        if q["pinned"]:
            q["check"]("pinned_participant_lives_unchanged", q["same_participant_lives"]())
        ls, ss = q["preview"](local), q["preview"](server)
        if "before_sequence" in s:
            observe(server, local)
        if s["phase"] == "setup":
            q["check"]("built_frozen_kind_projection_api_available", hasattr(u.PureMassRuntime, "get_presentation_kind")
                         and hasattr(u.PrototypeRuntime, "get_presentation_kind") and hasattr(u.ExtensionRuntime, "get_presentation_kind"))
            if not q["setup_fixture"]((250, 450, 650)):
                return
            if scene.get("move_required") and not s.get("movement_fixture_restored"):
                for actor in q["owned"]:
                    movement = actor.get_component_by_class(u.CharacterMovementComponent)
                    if movement:
                        # This scenario must exercise original CharacterMovement
                        # root motion. The shared immobile firing-range fixture
                        # deliberately disables it for all other scenarios.
                        movement.set_movement_mode(u.MovementMode.MOVE_WALKING, 0)
                s["movement_fixture_restored"] = True
                s["movement_settle_at"] = time.monotonic()
                return
            if scene.get("move_required") and time.monotonic() - s["movement_settle_at"] < .5:
                return
            q["check"]("no_preexisting_recipe_panel", library["panel"](local) is None)
            s["original_handles"] = {p.get_path_name(): library["handles"](p) for p in {local, server}}
            q["command"](local, "UMG.Open")
            phase("open")
        elif s["phase"] == "open":
            p = library["panel"](local)
            if not p or not p.is_visible():
                return
            ui["panel"] = p
            row = next(r for r in q["dev"](local).get_definitions()
                       if r.get_editor_property("base").get_editor_property("ability_class").get_path_name() == scene["path"]
                       and r.get_editor_property("base").get_editor_property("selectable"))
            s["definition_id"] = str(row.get_editor_property("definition_id"))
            base_nodes = list(row.get_editor_property("base").get_editor_property("default_graph").get_editor_property("nodes"))
            s["actual_base_nodes"] = [str(n.get_editor_property("type_id")) for n in base_nodes]
            s["base_scope_parameters"] = []
            for i, node in enumerate(base_nodes):
                if str(node.get_editor_property("type_id")) != scope:
                    continue
                parameters = node.get_editor_property("config").get_editor_property("parameters")
                values = {"index": i, "class": parameters.get_class().get_name(), "source": parameters.get_path_name()}
                for field in ("kind", "duration", "period", "radius", "speed", "arm_delay", "count", "range"):
                    value = parameters.get_editor_property(field)
                    values[field] = value if isinstance(value, (int, float, bool, str)) else str(value)
                s["base_scope_parameters"].append(values)
            library["queue"](["UMG.Select RecipeDefinition " + s["definition_id"]], "begin_click")
        elif s["phase"] == "ui_queue":
            if age >= .1:
                if s["ui_queue"]:
                    q["command"](local, s["ui_queue"].pop(0))
                    s["at"] = time.monotonic()
                else:
                    phase(s["ui_next"])
        elif s["phase"] == "begin_click":
            s["begin_state_sequences"] = {p.get_path_name(): int(q["preview"](p).get_editor_property("state_sequence")) for p in {local, server}}
            s["owns_preview_request"] = True
            q["command"](local, "UMG.Click RecipeBegin")
            phase("begin")
        elif s["phase"] == "begin":
            if not q["preview_started"](local, server):
                return
            if not all(len(library["handles"](p)) == len(s["original_handles"][p.get_path_name()]) + 1 for p in {local, server}):
                return
            s["preview_handles"] = {p.get_path_name(): library["handles"](p) for p in {local, server}}
            s["edit_position"] = 0
            select_edit(local, 0)
        elif s["phase"] in ("apply_click", "remove_click"):
            request = scene["edits"][s["edit_position"]]
            q["check"]("actual_widget_selection_" + str(s["edit_position"]),
                         library["control"]("RecipeModifier", u.ComboBoxString).get_selected_option() == request["modifier"])
            s["revision_before"] = int(ls.get_editor_property("revision"))
            q["command"](local, "UMG.Click " + ("RecipeRemove" if s.get("removing") else "RecipeApply"))
            phase("accepted")
        elif s["phase"] == "accepted":
            library["reject_error"](ls)
            if int(ls.get_editor_property("revision")) <= s["revision_before"] or int(ls.get_editor_property("revision")) != int(ss.get_editor_property("revision")):
                return
            if not s.get("removing"):
                request = scene["edits"][s["edit_position"]]
                expected = ({"RecipeRadius": request["radius"], "RecipeInterval": request["interval"]}
                            if request["modifier"].startswith("Propagation.") else {"RecipeValue": request["value"]})
                if request["fragment"] == "Area.Petrify":
                    expected["RecipeRadius"] = request["radius"]
                if request["fragment"] == "Prototype.Beam":
                    expected["RecipeInterval"] = request["interval"]
                actual = {key: float(library["control"](key, u.SpinBox).get_value()) for key in expected}
                s.setdefault("actual_after_click_numeric_values", []).append(dict(
                    edit=s["edit_position"], modifier=request["modifier"], expected=expected, actual=actual))
                q["check"]("actual_requested_numeric_values_survive_focus_commit_" + str(s["edit_position"]),
                             all(abs(actual[key] - value) < 1e-4 for key, value in expected.items()))
            next_position = s["edit_position"] + (-1 if s.get("removing") else 1)
            if 0 <= next_position < len(scene["edits"]):
                s["edit_position"] = next_position
                select_edit(local, next_position)
            elif s.get("removing"):
                q["check"]("all_actual_ui_edits_withdrawn", ls.get_editor_property("content_hash") == ls.get_editor_property("base_hash"))
                q["check"]("removal_preserves_exact_temporary_spec", all(library["handles"](p) == s["preview_handles"][p.get_path_name()] for p in {local, server}))
                q["command"](local, "UMG.Click RecipeClose")
                phase("closed")
            else:
                q["check"]("actual_umg_authored_new_runtime_version", ls.get_editor_property("content_hash") != ls.get_editor_property("base_hash"))
                q["check"]("real_umg_controls_own_focus", ui["panel"].has_user_focused_descendants(local))
                s["before_sequence"] = max([int(r.get_editor_property("sequence")) for r in q["records"](server)] + [0])
                s["accepted_revision"] = int(ss.get_editor_property("revision"))
                s["accepted_hash"] = str(ss.get_editor_property("content_hash"))
                s["selected_tag"] = ss.get_editor_property("ability_tag").export_text()
                s["before_health"] = q["healths"](q["owned"])
                ui["before_positions"] = [a.get_actor_location() for a in q["owned"]]
                s["before_positions"] = [str(p) for p in ui["before_positions"]]
                s["authority_status"] = [False] * 3
                s["remote_status"] = [False] * 3
                s["before_mana"] = q["attribute"](q["component"](server, u.MyAbilitySystemComponent), "mana")
                s["casts_sent"] = 0
                q["command"](local, "UMG.Click RecipeHide")
                phase("hidden")
        elif s["phase"] == "hidden":
            if ui["panel"].is_visible():
                return
            q["check"]("hide_restores_actual_viewport_focus", ui["panel"].has_owner_viewport_focus())
            phase("aim")
        elif s["phase"] == "aim":
            replicas = (q["ready_replicas"]() if s["casts_sent"] == 0 else
                        q["native_targets"](local.get_world(), local.get_controlled_pawn()))
            if not replicas or len(replicas) != 3:
                return
            # Real fixtures have lower capsule centres than the player's muzzle.
            # A piercing ray aims at the far endpoint so the legitimate 3D line
            # traverses near/middle capsules before the map's actual floor.
            # Homing and other scenes retain their original nearest aim.
            aim_index = scene.get("aim_index", 0)
            s["actual_aim_index"] = aim_index
            q["aim_at"](replicas[aim_index])
            if age > .5 and q["dev"](local).is_preview_frontend_idle() and q["dev"](server).is_preview_frontend_idle():
                q["command"](local, "Press")
                s["casts_sent"] += 1
                phase("press")
        elif s["phase"] == "press":
            if age >= scene.get("hold", .12):
                q["command"](local, "Release")
                phase("primary_wait" if scene.get("primary_required") else "observe")
        elif s["phase"] == "primary_wait":
            if age > 1:
                replicas = q["native_targets"](local.get_world(), local.get_controlled_pawn())
                if len(replicas) == 3:
                    q["aim_at"](replicas[0])
                    s["owns_primary_press"] = True
                    q["command"](local, "PressPrimary")
                    phase("primary")
        elif s["phase"] == "primary":
            if age > .12:
                q["command"](local, "ReleasePrimary")
                s["owns_primary_press"] = False
                phase("observe")
        elif s["phase"] == "observe":
            if s["casts_sent"] < scene.get("casts", 1) and age > .5:
                if q["dev"](local).is_preview_frontend_idle() and q["dev"](server).is_preview_frontend_idle():
                    phase("aim")
                return
            if age < 7:
                return
            save_evidence(server)
            hp = q["healths"](q["owned"])
            s["after_mana"] = q["attribute"](q["component"](server, u.MyAbilitySystemComponent), "mana")
            q["check"]("original_resource_never_increases_with_fixture_regen_disabled", s["after_mana"] <= s["before_mana"])
            selected = [r for r in cache.values() if r["ability_tag"] == s["selected_tag"]]
            costs = [r for r in selected if r["cost"]]
            q["check"]("original_frontend_cost_observed", bool(costs) and all(r["paid"] for r in costs))
            q["check"]("real_input_uses_only_the_accepted_recipe_revision", bool(selected) and all(r["revision"] == s["accepted_revision"] for r in selected))
            q["check"]("real_input_uses_only_the_accepted_recipe_hash", all(r["frozen_hash"] == s["accepted_hash"] for r in selected))
            bad_endings = {str(u.AbilityGraphEndReason.FAILED), str(u.AbilityGraphEndReason.BUDGET_EXCEEDED), str(u.AbilityGraphEndReason.SOURCE_INVALID)}
            q["check"]("no_payload_branch_failed_after_partial_success", all(r["end_reason"] not in bad_endings for r in selected
                         if r["kind"] == str(u.AbilityRuntimeObservationKind.GRAPH_FINISHED)))
            if scene.get("damage_required", True):
                q["check"]("original_payload_causes_real_hp_change", any(a < b for a, b in zip(hp, s["before_health"])))
            else:
                q["check"]("replaced_damage_has_no_hidden_original_hit", hp == s["before_health"])
            if scene.get("state_required", True):
                common = sum(a and b for a, b in zip(s["authority_status"], s["remote_status"]))
                q["check"]("runtime_authored_state_really_applies_and_replicates", common >= scene.get("min_targets", 1))
            if name.startswith("Scatter"):
                carriers = {r["invocation"] for r in selected if r["node_type"] == "Delivery.PrototypeFlight" and r["stage"] == "Contact"}
                q["check"]("three_independent_scatter_carriers_really_contact", len(carriers) == 3)
            if scene.get("beam_host_required"):
                q["check"]("replacement_creates_three_distinct_real_canonical_beam_hosts", len(s.get("actual_beam_hosts", {})) == 3)
            if scene.get("move_required"):
                q["check"]("original_move_actually_moves_a_target_horizontally", any(
                    (a.get_actor_location().x - b.x)**2 + (a.get_actor_location().y - b.y)**2 > 25
                    for a, b in zip(q["owned"], ui["before_positions"])))
            s["public_observations"] = list(cache.values())
            s["after_health"] = hp
            q["command"](local, "UMG.Open")
            phase("reopen")
        elif s["phase"] == "reopen":
            if ui["panel"].is_visible():
                s["removing"] = True
                s["edit_position"] = len(scene["edits"]) - 1
                select_edit(local, s["edit_position"])
        elif s["phase"] == "closed":
            requested = q["guid_hex"](ui["panel"].get_last_close_request_id())
            received = q["guid_hex"](q["property_value"](q["dev"](local), "last_close_acknowledged_id", "LastCloseAcknowledgedId"))
            if requested == "0" * 32 or requested != received or library["panel"](local):
                return
            if ls.get_editor_property("preview_active") or ss.get_editor_property("preview_active"):
                return
            if not all(library["handles"](p) == s["original_handles"][p.get_path_name()] for p in {local, server}):
                return
            q["check"]("exact_original_panel_close_ack", q["property_value"](q["dev"](local), "last_close_succeeded", "bLastCloseSucceeded"))
            q["check"]("exact_preview_grant_reclaimed_on_both_worlds", True)
            q["check"]("close_restores_actual_viewport_focus", ui["panel"].has_owner_viewport_focus())
            if other:
                q["check"]("other_player_recipe_isolated", q["preview"](other).export_text() == s["other_preview_before"])
            s["owns_preview_request"] = False
            library["finish"]()
    except Exception as error:
        if server and "before_sequence" in s:
            save_evidence(server)
        if s.get("owns_primary_press"):
            q["command"](local, "ReleasePrimary")
            s["owns_primary_press"] = False
        # Keep reports JSON-safe even when a phase fails before normal sampling.
        if "before_positions" in s:
            s["before_positions"] = [str(p) for p in s["before_positions"]]
        library["finish"](repr(error))


q["handle"] = u.register_slate_post_tick_callback(tick)
q["write"]()
u.log("DYNAMIC_MAGE_UMG_COMBINATION_STARTED " + str(q["run"] / "report.json"))
