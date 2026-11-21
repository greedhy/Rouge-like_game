"""Explicit developer preview + original casting in an empty L_Batch6Test PIE.

Supports Standalone or two-player Listen Server. Preview/edit/close RPCs and T
input are queued through the native RuntimeRecipe.Probe command for a later
native tick, outside Python's editor script RPC callspace override. TargetData
comes only from the original local cursor task. No activation packet, Notify,
damage or private GA callback is injected. Actor fixtures and attributes belong
only to this temporary PIE run. Never saves Content or a player save.

Run after the current module is built and the requested PIE session is ready.
The test currently exercises FireBolt + sequential real delivery + appended
petrify; field/passive/network breadth is recorded separately, not inferred.
"""
import json
import time
import uuid
from pathlib import Path

import unreal as u


root = Path(u.Paths.project_saved_dir()).resolve() / "GAAuthoring/DynamicMage/RealCasting"
run = root / uuid.uuid4().hex
run.mkdir(parents=True)
state = {
    "phase": "setup", "started": time.monotonic(), "checks": {}, "samples": [],
    "scope": "Original local cursor and T InputKey path; real preview/edit RPC when remote; no injected TargetData/Notify/damage.",
    "limitations": ["Not OS keyboard injection", "No visual quality certification", "Field/passive cases remain separate"],
    "finished": False, "passed": False, "owns_preview_request": False,
}
owned = []
owned_controllers = []
original_attributes = {}
pinned = None


def write():
    (run / "report.json").write_text(json.dumps(state, indent=2, ensure_ascii=False), encoding="utf-8")
    (root / "latest.json").write_text(json.dumps({"report": str(run / "report.json")}), encoding="utf-8")


def check(name, value):
    state["checks"][name] = bool(value)
    if not value:
        raise AssertionError(name)


def phase(name):
    state.update(phase=name, at=time.monotonic())
    write()


def pcs():
    return [p for p in u.ObjectIterator(u.MyPlayerController)
            if u.SystemLibrary.is_valid(p) and "UEDPIE_" in p.get_path_name()
            and p.get_controlled_pawn() and p.get_editor_property("player_state")]


def participants():
    if pinned:
        return pinned["local"], pinned["server"], pinned["other"]
    players = pcs()
    remote = next((p for p in players if p.is_local_controller()
                   and p.get_local_role() == u.NetRole.ROLE_AUTONOMOUS_PROXY), None)
    if remote:
        server = next((p for p in players if not p.is_local_controller()
                       and p.get_local_role() == u.NetRole.ROLE_AUTHORITY), None)
        other = next((p for p in players if p.is_local_controller()
                      and p.get_local_role() == u.NetRole.ROLE_AUTHORITY), None)
        return remote, server, other
    local = next((p for p in players if p.is_local_controller()
                  and p.get_local_role() == u.NetRole.ROLE_AUTHORITY), None)
    return local, local, None


def pin_participants(local, server, other):
    global pinned
    players = pcs()
    if local != server:
        check("exactly_one_remote_client", sum(p.is_local_controller()
              and p.get_local_role() == u.NetRole.ROLE_AUTONOMOUS_PROXY for p in players) == 1)
        check("exactly_one_remote_authority", sum(not p.is_local_controller()
              and p.get_local_role() == u.NetRole.ROLE_AUTHORITY for p in players) == 1)
        check("listen_host_exists_in_same_authority_world", bool(other)
              and other.get_world() == server.get_world())
        local_id = dev(local).get_owner_player_id()
        server_id = dev(server).get_owner_player_id()
        if local_id < 0 or server_id < 0:
            return
        check("remote_player_state_identity_matches", local_id == server_id)
    pinned = {"local": local, "server": server, "other": other,
              "lives": [(p, p.get_world(), p.get_editor_property("player_state"), p.get_controlled_pawn())
                        for p in {local, server, other} if p]}
    state["pinned_lives"] = [{"pc": p.get_path_name(), "world": w.get_path_name(),
                              "state": ps.get_path_name(), "pawn": pawn.get_path_name()}
                             for p, w, ps, pawn in pinned["lives"]]


def same_participant_lives():
    return pinned and all(u.SystemLibrary.is_valid(p) and u.SystemLibrary.is_valid(w)
                          and u.SystemLibrary.is_valid(ps) and u.SystemLibrary.is_valid(pawn)
                          and p.get_world() == w and p.get_editor_property("player_state") == ps
                          and p.get_controlled_pawn() == pawn
                          for p, w, ps, pawn in pinned["lives"])


def property_value(obj, *names):
    for name in names:
        try:
            return obj.get_editor_property(name)
        except Exception:
            pass
    raise AssertionError("Missing required native readback: " + ", ".join(names))


def guid_hex(value):
    return "".join(f"{int(property_value(value, key)) & 0xffffffff:08x}" for key in ("a", "b", "c", "d"))


def petrified(actor):
    # EnemyCharacter also owns a weapon skeletal mesh. Read ACharacter.Mesh
    # explicitly; GetComponentByClass can pick the weapon and invent a failure.
    mesh = property_value(actor, "mesh", "Mesh")
    return bool(property_value(mesh, "b_pause_anims", "pause_anims", "bPauseAnims"))


def component(pc, cls):
    return pc.get_editor_property("player_state").get_component_by_class(cls)


def dev(pc):
    return component(pc, u.AbilityRuntimeRecipeDeveloperComponent)


def preview(pc):
    return dev(pc).get_developer_state()


def attribute(asc, name, base=False):
    aset = asc.get_attribute_set(u.MyAttributeSet)
    field = aset.get_editor_property(name)
    return float(field.get_editor_property("base_value" if base else "current_value"))


def command(pc, text):
    u.SystemLibrary.execute_console_command(pc.get_world(), "RuntimeRecipe.Probe " + text, pc)
    state.setdefault("requests", []).append({"action": text, "local_pc": pc.get_path_name(), "time": time.monotonic()})


def set_base(authority, actor, name, value):
    # The native QA helper is authority-only and calls ASC.SetNumericAttributeBase.
    # Python never rewrites AttributeData, bypasses an aggregator, or sends a new gameplay RPC.
    command(authority, f"SetBase {actor.get_path_name()} {name} {value}")


def node_index(pc, type_id):
    selected = str(preview(pc).get_editor_property("selected_id"))
    for row in dev(pc).get_definitions():
        if str(row.get_editor_property("definition_id")) == selected:
            graph = row.get_editor_property("base").get_editor_property("default_graph")
            for index, node in enumerate(graph.get_editor_property("nodes")):
                if str(node.get_editor_property("type_id")) == type_id:
                    return index
    raise AssertionError("Actual selected base has no independent " + type_id)


def records(pc):
    return list(dev(pc).get_observations())


def native_targets(world, owner):
    origin = owner.get_actor_location()
    return sorted([a for a in u.GameplayStatics.get_all_actors_of_class(world, u.EnemyCharacter)
                   if a.get_owner() == owner],
                  key=lambda a: (a.get_actor_location() - origin).length_squared())



def ensure_mage_profession():
    """Configure only the explicit empty QA map's actual Mage pawn, then wait for its real replication."""
    local, server, _other = participants()
    server_state = server.get_editor_property("player_state")
    current = server_state.get_editor_property("profession")
    if globals().get("ALLOW_UNSELECTED_MAGE", False):
        # 专门覆盖真实的直接 PIE 入口：职业保持 Unselected，不能先设 Mage 掩盖资格校验缺陷。
        check("direct_pie_profession_stays_unselected_on_server", current == u.PlayerProfession.UNSELECTED)
        state["original_profession"] = "Unselected"
        state["owns_profession_setup"] = False
        return local.get_editor_property("player_state").get_editor_property("profession") == u.PlayerProfession.UNSELECTED
    if "original_profession" not in state:
        check("qa_initial_profession_is_restorable", current in (u.PlayerProfession.UNSELECTED, u.PlayerProfession.MAGE))
        state["original_profession"] = "Mage" if current == u.PlayerProfession.MAGE else "Unselected"
        state["owns_profession_setup"] = current != u.PlayerProfession.MAGE
        if state["owns_profession_setup"]:
            command(server, "Profession " + server.get_controlled_pawn().get_path_name() + " Mage")
            state["profession_requested_at"] = time.monotonic()
    if state.get("profession_requested_at") and time.monotonic() - state["profession_requested_at"] > 10:
        if current != u.PlayerProfession.MAGE:
            raise AssertionError("QA-only Mage profession setup was rejected or did not execute")
    return current == u.PlayerProfession.MAGE and local.get_editor_property("player_state").get_editor_property("profession") == u.PlayerProfession.MAGE


def request_begin(identity):
    local, server, _other = participants()
    state["begin_state_sequences"] = {p.get_path_name(): int(preview(p).get_editor_property("state_sequence")) for p in {local, server}}
    state["owns_preview_request"] = True
    command(local, "Begin " + identity)
    phase("begin")


def preview_started(local, server):
    for controller in {local, server}:
        snapshot = preview(controller)
        error = str(snapshot.get_editor_property("error"))
        if int(snapshot.get_editor_property("state_sequence")) > state["begin_state_sequences"].get(controller.get_path_name(), -1) and error:
            raise AssertionError("Actual BeginPreview rejected: " + error)
    return preview(local).get_editor_property("preview_active") and preview(server).get_editor_property("preview_active")


def fixture_resources_ready():
    local, server, _other = participants()
    asc = component(server, u.MyAbilitySystemComponent)
    maximum = attribute(asc, "max_mana")
    check("actual_current_max_mana_is_positive", maximum > 0)
    if abs(maximum - state["fixture_expected_mana"]) > .01:
        # An existing MMC can override MaxMana independently from its base.
        # Wait for that real value, then keep Mana base inside the actual cap.
        state["fixture_expected_mana"] = maximum
        set_base(server, server.get_controlled_pawn(), "mana", maximum)
        return False
    return all(abs(attribute(component(pc, u.MyAbilitySystemComponent), "mana", base=True) - maximum) < .01
               and abs(attribute(component(pc, u.MyAbilitySystemComponent), "mana") - maximum) < .01
               and abs(attribute(component(pc, u.MyAbilitySystemComponent), "max_mana") - maximum) < .01
               and abs(attribute(component(pc, u.MyAbilitySystemComponent), "mana_regeneration", base=True)) < .01
               for pc in {local, server})


def setup_fixture(offsets):
    """Shared explicit QA setup for the field/passive/version scenarios."""
    local, server, other = participants()
    if not local or not server:
        return False
    if state.get("fixture_setup_requested"):
        return fixture_resources_ready()
    if not pinned:
        pin_participants(local, server, other)
        if not pinned:
            return False
    sw = server.get_world()
    check("dedicated_empty_qa_map", u.GameplayStatics.get_current_level_name(sw, True) == "L_Batch6Test")
    check("not_running_dungeon_objective", not u.GameplayStatics.get_all_actors_of_class(sw, u.DungeonRunDirector))
    check("no_preexisting_enemy_population", not u.GameplayStatics.get_all_actors_of_class(sw, u.EnemyCharacter))
    check("preview_was_inactive", not preview(server).get_editor_property("preview_active"))
    if not ensure_mage_profession():
        return False
    state["owns_fixture_setup"] = True
    state["mode"] = "ListenServerRemote" if local != server else "Standalone"
    state["participants"] = {"local": local.get_path_name(), "authority": server.get_path_name(),
                             "other": other.get_path_name() if other else None}
    if other:
        state["other_preview_before"] = preview(other).export_text()
    asc = component(server, u.MyAbilitySystemComponent)
    for name in ("max_mana", "mana", "mana_regeneration"):
        original_attributes[name] = attribute(asc, name, base=True)
    state["fixture_expected_mana"] = attribute(asc, "max_mana")
    check("fixture_mana_uses_actual_current_maximum", state["fixture_expected_mana"] > 0)
    set_base(server, server.get_controlled_pawn(), "mana", state["fixture_expected_mana"])
    set_base(server, server.get_controlled_pawn(), "mana_regeneration", 0)
    cls = u.load_class(None, "/Game/BluePrints/Characters/Goblin/BP_GoblinSpear.BP_GoblinSpear_C")
    check("real_actor_fixture_class", bool(cls))
    origin = server.get_controlled_pawn().get_actor_location()
    direction = server.get_controlled_pawn().get_actor_forward_vector()
    library = u.get_default_object(u.GameplayStatics)
    for offset in offsets:
        transform = u.Transform(location=origin + direction * float(offset))
        actor = library.call_method("BeginDeferredActorSpawnFromClass", (
            sw, cls, transform, u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,
            server.get_controlled_pawn(), u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        owned.append(actor)
        actor.tags = list(actor.tags) + ["DynamicMageRealCastingQA"]
        library.call_method("FinishSpawningActor", (actor, transform, u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        movement = actor.get_component_by_class(u.CharacterMovementComponent)
        if movement:
            movement.disable_movement()
        if actor.get_controller():
            controller = actor.get_controller()
            if isinstance(controller, u.AIController):
                owned_controllers.append(controller)
            brain = controller.get_component_by_class(u.BrainComponent)
            if brain:
                brain.stop_logic("Owned dynamic mage QA fixture")
        # AutoPossessAI assigns its controller as Owner during FinishSpawning.
        # Restore this fixture's replicated identity after real possession and
        # keep the real brain/controller stopped rather than substituting tags.
        actor.set_owner(server.get_controlled_pawn())
        actor.force_net_update()
        for name, value in (("max_health", 100000), ("health", 100000), ("block_chance", 0), ("armor", 0)):
            set_base(server, actor, name, value)
    check("authority_fixture_owners_match_pinned_player", all(a.get_owner() == server.get_controlled_pawn() for a in owned))
    state["owned_ai_controller_paths"] = [c.get_path_name() for c in owned_controllers]
    u.get_default_object(u.load_class(None, "/Script/UMG.WidgetBlueprintLibrary")).call_method(
        "SetInputMode_GameOnly", (local, False))
    state["fixture_setup_requested"] = True
    # Requests run on the next native tick; no Begin is sent until both actual
    # Mana base/current have reached the original current MaxMana cap.
    return False


def ready_replicas():
    local, server, _other = participants()
    result = native_targets(local.get_world(), local.get_controlled_pawn())
    if len(result) != len(owned) or not all(
        attribute(a.get_component_by_class(u.MyAbilitySystemComponent), "health") >= 100000 for a in result
    ) or not all(attribute(a.get_component_by_class(u.MyAbilitySystemComponent), "health") >= 100000 for a in owned):
        return None
    return result


def projectile_corridor_ready():
    """Read-only sphere sweeps of the actual map; combatants are ignored, geometry never is."""
    local, server, _other = participants()
    static = getattr(u.ObjectTypeQuery, "WORLD_STATIC", None)
    dynamic = getattr(u.ObjectTypeQuery, "WORLD_DYNAMIC", None)
    if static is None:
        static = getattr(u.ObjectTypeQuery, "OBJECT_TYPE_QUERY1")
    if dynamic is None:
        dynamic = getattr(u.ObjectTypeQuery, "OBJECT_TYPE_QUERY2")
    for controller in {local, server}:
        pawn = controller.get_controlled_pawn()
        targets = native_targets(controller.get_world(), pawn)
        if len(targets) != len(owned):
            return False
        points = [pawn.get_actor_location()] + [a.get_actor_location() for a in targets]
        for index, (start, end) in enumerate(zip(points, points[1:])):
            obstruction = u.SystemLibrary.sphere_trace_single_for_objects(
                controller.get_world(), start, end, 32.0, [static, dynamic], False,
                [pawn] + targets, u.DrawDebugTrace.NONE, True)
            state.setdefault("corridor_preflight", []).append({
                "world": controller.get_world().get_path_name(), "segment": index,
                "start": str(start), "end": str(end), "radius_cm": 32.0,
                "obstruction": obstruction.export_text() if obstruction else None})
            check("actual_map_projectile_corridor_clear_" + controller.get_path_name() + "_" + str(index), not obstruction)
    state["corridor_verified"] = True
    return True


def aim_at(actor):
    local, _server, _other = participants()
    screen = local.project_world_location_to_screen(actor.get_actor_location())
    check("original_target_visible_in_local_viewport", bool(screen))
    local.set_mouse_location(int(screen.x), int(screen.y))


def healths(actors):
    return [attribute(a.get_component_by_class(u.MyAbilitySystemComponent), "health") for a in actors]


def first_node(pc, types):
    selected = str(preview(pc).get_editor_property("selected_id"))
    for row in dev(pc).get_definitions():
        if str(row.get_editor_property("definition_id")) != selected:
            continue
        for index, node in enumerate(row.get_editor_property("base").get_editor_property("default_graph").get_editor_property("nodes")):
            if str(node.get_editor_property("type_id")) in types:
                return index, str(node.get_editor_property("type_id"))
    raise AssertionError("Actual selected graph lacks expected typed axis: " + str(types))


def new_records(pc, after):
    return [r for r in records(pc) if int(r.get_editor_property("sequence")) > after]


def effect_hosts(pc):
    result = []
    for name in ("PrototypeRuntime", "PureMassRuntime", "ExtensionRuntime", "Batch6ChainPresentation", "TerrainSurface"):
        cls = getattr(u, name, None)
        if cls:
            result.extend(a for a in u.GameplayStatics.get_all_actors_of_class(pc.get_world(), cls)
                          if a.get_owner() == pc.get_controlled_pawn())
    return result


def finish(error=None):
    # Keep the callback alive until this run's exact close is acknowledged, then
    # restore authority bases and wait for readback. A failed cast is not a reason
    # to leave a queued RPC/upkeep effect racing an already-written restore.
    if state.get("phase", "").startswith("cleanup"):
        state["cleanup_error"] = str(error)
        return
    state["test_error"] = error
    state["cleanup_started"] = time.monotonic()
    local, server, _other = participants()
    if state["owns_preview_request"] and local and u.SystemLibrary.is_valid(local):
        state["close_request"] = uuid.uuid4().hex
        command(local, "Release")
        command(local, "Close " + state["close_request"])
        phase("cleanup_close")
    else:
        phase("cleanup_restore_request")


def finalize(cleanup_succeeded):
    u.unregister_slate_post_tick_callback(handle)
    error = state.get("test_error") or state.get("cleanup_error")
    state.update(finished=True, cleanup_succeeded=bool(cleanup_succeeded),
                 passed=cleanup_succeeded and error is None and bool(state["checks"])
                 and all(state["checks"].values()), error=error)
    write()
    u.log("DYNAMIC_MAGE_REAL_CASTING " + str(run / "report.json"))


def destroy_owned_fixtures():
    for actor in owned:
        if u.SystemLibrary.is_valid(actor):
            actor.destroy_actor()
    # UE normally destroys a controller without PlayerState from
    # PawnPendingDestroy. Only reclaim a surviving captured fixture controller
    # when it has no different pawn; never destroy a controller reassigned by
    # another system during cleanup.
    for controller in owned_controllers:
        if u.SystemLibrary.is_valid(controller):
            pawn = controller.get_controlled_pawn()
            if not pawn or pawn in owned:
                controller.destroy_actor()
            else:
                state["cleanup_error"] = "Owned fixture AI controller now controls a different pawn"
    owned.clear()


def cleanup_tick():
    local, server, _other = participants()
    if time.monotonic() - state["cleanup_started"] > 25:
        state["cleanup_error"] = state.get("cleanup_error") or "Cleanup acknowledgement/restoration timed out"
        destroy_owned_fixtures()
        finalize(False)
        return
    if pinned and not same_participant_lives():
        state["cleanup_error"] = "Pinned participant life changed; do not redirect cleanup into another pawn"
        destroy_owned_fixtures()
        finalize(False)
        return
    if state["phase"] == "cleanup_close":
        acknowledged = guid_hex(property_value(dev(local), "last_close_acknowledged_id", "LastCloseAcknowledgedId"))
        if acknowledged != state["close_request"]:
            return
        if not property_value(dev(local), "b_last_close_succeeded", "last_close_succeeded", "bLastCloseSucceeded"):
            state["cleanup_error"] = "Exact close acknowledgement rejected this run's cleanup"
            finalize(False)
            return
        if preview(local).get_editor_property("preview_active") or preview(server).get_editor_property("preview_active"):
            return
        state["checks"]["exact_close_request_acknowledged"] = True
        state["owns_preview_request"] = False
        phase("cleanup_restore_request")
    if state["phase"] == "cleanup_restore_request":
        if server:
            for name, value in original_attributes.items():
                set_base(server, server.get_controlled_pawn(), name, value)
        destroy_owned_fixtures()
        phase("cleanup_restore")
    if state["phase"] == "cleanup_restore":
        if server and not all(abs(attribute(component(server, u.MyAbilitySystemComponent), name, base=True) - value) < .01
                              for name, value in original_attributes.items()):
            return
        if server and not all(abs(attribute(component(server, u.MyAbilitySystemComponent), name) - value) < .01
                              for name, value in state.get("expected_restored_current", {}).items()):
            return
        for controller in {local, server}:
            if controller and state.get("owns_fixture_setup") and (native_targets(controller.get_world(), controller.get_controlled_pawn())
                               or any(a.get_owner() == controller.get_controlled_pawn()
                                      for a in u.GameplayStatics.get_all_actors_of_class(controller.get_world(), u.MyProjectile))
                               or effect_hosts(controller)):
                return
        state["checks"]["authoritative_original_attribute_bases_restored"] = True
        state["checks"]["owned_actor_and_projectile_fixtures_removed_on_both_worlds"] = True
        if any(u.SystemLibrary.is_valid(controller) for controller in owned_controllers):
            return
        state["checks"]["captured_fixture_ai_controllers_removed"] = True
        if state.get("owns_profession_setup"):
            command(server, "Profession " + server.get_controlled_pawn().get_path_name() + " " + state["original_profession"])
        phase("cleanup_profession")
    if state["phase"] == "cleanup_profession":
        if "original_profession" in state:
            expected = u.PlayerProfession.MAGE if state["original_profession"] == "Mage" else u.PlayerProfession.UNSELECTED
            if not all(p.get_editor_property("player_state").get_editor_property("profession") == expected for p in {local, server}):
                return
        state["checks"]["original_profession_restored_on_both_worlds"] = True
        state["restore_confirmed"] = True
        finalize(True)


def tick(_delta):
    try:
        if state["phase"].startswith("cleanup"):
            cleanup_tick()
            return
        if time.monotonic() - state["started"] > 100:
            raise AssertionError("Timed out in " + state["phase"])
        local, server, other = participants()
        if not local or not server:
            return
        if pinned:
            check("pinned_participant_lives_unchanged", same_participant_lives())
        age = time.monotonic() - state.get("at", state["started"])
        asc = component(server, u.MyAbilitySystemComponent)
        sw, cw = server.get_world(), local.get_world()
        if state["phase"] == "setup":
            if not setup_fixture((250, 450, 650)):
                return
            request_begin("Abilities.Fire.FireBolt")
            return
        ls, ss = preview(local), preview(server)
        if state["phase"] == "begin":
            if not preview_started(local, server):
                return
            check("same_server_acknowledged_base", ls.get_editor_property("base_hash") == ss.get_editor_property("base_hash"))
            check("authoritative_fixture_health_ready", all(
                attribute(a.get_component_by_class(u.MyAbilitySystemComponent), "health") >= 100000 for a in owned))
            state["revision"] = int(ss.get_editor_property("revision"))
            index = node_index(local, "Delivery.ProjectileAwait")
            state["delivery_node"] = index
            command(local, f"Edit Propagation.Sequential {index} 1 3 0.3 400 0.7")
            phase("propagation")
            return
        if state["phase"] == "propagation":
            if int(ls.get_editor_property("revision")) <= state["revision"]:
                return
            check("remote_propagation_edit_accepted", not str(ls.get_editor_property("error")))
            state["revision"] = int(ls.get_editor_property("revision"))
            command(local, f"Edit Append.Status.Petrify {state['delivery_node']} 1.25")
            phase("payload")
            return
        if state["phase"] == "payload":
            if int(ls.get_editor_property("revision")) <= state["revision"]:
                return
            check("remote_payload_edit_accepted", not str(ls.get_editor_property("error")))
            if int(ss.get_editor_property("revision")) != int(ls.get_editor_property("revision")):
                return
            state["revision"] = int(ss.get_editor_property("revision"))
            state["observation_sequence"] = max([int(r.get_editor_property("sequence")) for r in records(server)] + [0])
            state["before_health"] = [attribute(a.get_component_by_class(u.MyAbilitySystemComponent), "health") for a in owned]
            state["before_mana"] = attribute(asc, "mana")
            state["petrify_seen"] = [False, False, False]
            state["client_petrify_seen"] = [False, False, False]
            state["presentation_pairs"] = []
            state["server_projectile_peak"] = 0
            state["client_projectile_peak"] = 0
            phase("aim")
            return
        if state["phase"] == "aim":
            replicas = native_targets(cw, local.get_controlled_pawn())
            if len(replicas) != 3:
                return
            if not all(attribute(a.get_component_by_class(u.MyAbilitySystemComponent), "health") >= 100000
                       for a in replicas):
                return
            if not state.get("corridor_verified") and not projectile_corridor_ready():
                return
            origin = local.get_controlled_pawn().get_actor_location()
            first = min(replicas, key=lambda a: (a.get_actor_location() - origin).length_squared())
            screen = local.project_world_location_to_screen(first.get_actor_location())
            check("original_target_visible_in_local_viewport", bool(screen))
            local.set_mouse_location(int(screen.x), int(screen.y))
            if age < 0.5:
                return
            command(local, "Press")
            phase("press")
            return
        if state["phase"] == "press":
            if age >= 0.12:
                command(local, "Release")
                phase("observe")
            return
        if state["phase"] == "observe":
            rows = [r for r in records(server) if int(r.get_editor_property("sequence")) > state["observation_sequence"]]
            hits = [r for r in rows if r.get_editor_property("confirmed_hit")
                    and str(r.get_editor_property("node_type")) == "Propagation.Sequential"
                    and str(r.get_editor_property("stage")) == "Hit"]
            for index, actor in enumerate(owned):
                state["petrify_seen"][index] |= petrified(actor)
            replicas = native_targets(cw, local.get_controlled_pawn())
            if len(replicas) == 3:
                for index, actor in enumerate(replicas):
                    state["client_petrify_seen"][index] |= petrified(actor)
            for world, pawn, key in ((sw, server.get_controlled_pawn(), "server_projectile_peak"),
                                     (cw, local.get_controlled_pawn(), "client_projectile_peak")):
                count = sum(a.get_owner() == pawn for a in u.GameplayStatics.get_all_actors_of_class(world, u.MyProjectile))
                state[key] = max(state[key], count)
            source_shots = [a for a in u.GameplayStatics.get_all_actors_of_class(sw, u.MyProjectile)
                            if a.get_owner() == server.get_controlled_pawn()]
            replica_shots = [a for a in u.GameplayStatics.get_all_actors_of_class(cw, u.MyProjectile)
                             if a.get_owner() == local.get_controlled_pawn()]
            for shot in source_shots:
                if not replica_shots:
                    break
                replica = min(replica_shots, key=lambda a: (a.get_actor_location() - shot.get_actor_location()).length_squared())
                gap = (replica.get_actor_location() - shot.get_actor_location()).length()
                if gap <= 150 and len(state["presentation_pairs"]) < 100:
                    sf, cf = shot.get_actor_forward_vector(), replica.get_actor_forward_vector()
                    dot = sf.x * cf.x + sf.y * cf.y + sf.z * cf.z
                    ssphere = shot.get_component_by_class(u.SphereComponent)
                    csphere = replica.get_component_by_class(u.SphereComponent)
                    state["presentation_pairs"].append({
                        "server": shot.get_path_name(), "client": replica.get_path_name(), "time": age,
                        "position_gap_cm": gap, "direction_dot": dot,
                        "server_radius": ssphere.get_scaled_sphere_radius() if ssphere else None,
                        "client_radius": csphere.get_scaled_sphere_radius() if csphere else None,
                        "same_class": shot.get_class().get_path_name() == replica.get_class().get_path_name(),
                    })
            state["samples"].append({"time": age, "hits": len(hits), "mana": attribute(asc, "mana"),
                                      "health": [attribute(a.get_component_by_class(u.MyAbilitySystemComponent), "health") for a in owned]})
            if age < 6:
                return
            check("original_animation_notify_produced_three_real_contacts", len(hits) == 3)
            check("physical_contacts_cover_three_distinct_original_lives", len({str(r.get_editor_property("target_label")) for r in hits}) == 3)
            check("one_frozen_cast_identity", len({guid_hex(r.get_editor_property("root_cast_id")) for r in hits}) == 1)
            check("all_three_original_lives_really_damaged", all(
                attribute(a.get_component_by_class(u.MyAbilitySystemComponent), "health") < before
                for a, before in zip(owned, state["before_health"])))
            costs = [r for r in rows if r.get_editor_property("kind") == u.AbilityRuntimeObservationKind.COST]
            check("original_frontend_cost_observed", any(
                r.get_editor_property("cost_succeeded")
                and r.get_editor_property("resource_after") < r.get_editor_property("resource_before") for r in costs))
            check("appended_payload_really_petrified_each_original_life", all(state["petrify_seen"]))
            check("appended_status_replicated_to_each_local_original_life", all(state["client_petrify_seen"]))
            check("actual_server_projectile_observed", state["server_projectile_peak"] > 0)
            check("original_projectile_presentation_reached_local_world", state["client_projectile_peak"] > 0)
            check("server_frozen_revision_matches_ack", all(int(r.get_editor_property("revision")) == state["revision"] for r in hits))
            check("three_original_target_replicas_remain_identified", len(replicas) == 3)
            check("target_health_converges_on_both_worlds", all(abs(
                attribute(a.get_component_by_class(u.MyAbilitySystemComponent), "health")
                - attribute(b.get_component_by_class(u.MyAbilitySystemComponent), "health")) < .01
                for a, b in zip(owned, replicas)))
            check("replicated_projectile_cast_geometry_observed", any(
                p["same_class"] and p["direction_dot"] >= .8 and p["server_radius"] is not None
                and p["client_radius"] is not None and abs(p["server_radius"] - p["client_radius"]) <= .1
                for p in state["presentation_pairs"]))
            state["presentation_tolerance"] = {"position_cm": 150, "direction_dot_min": .8,
                                                "collision_radius_cm": .1,
                                                "meaning": "asynchronous replica readback; visual quality remains user acceptance"}
            if other:
                check("other_player_preview_isolated", preview(other).export_text() == state["other_preview_before"])
            finish()
    except Exception as error:
        finish(repr(error))


if not globals().get("_DYNAMIC_MAGE_LIBRARY_ONLY", False):
    handle = u.register_slate_post_tick_callback(tick)
    write()
    u.log("DYNAMIC_MAGE_REAL_CASTING_STARTED " + str(run / "report.json"))
