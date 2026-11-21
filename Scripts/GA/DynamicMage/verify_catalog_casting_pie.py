"""Real-input default-cast census, one exact catalog class per temporary lease.

Run in the dedicated empty L_Batch6Test PIE. Uses the shared owned-fixture and
acknowledged-cleanup library, trusted BeginDefinition IDs and real local T/cursor
input. Never sends TargetData, Notify, damage, activation packets or private GA
callbacks. A per-class input smoke test supplements numerical parity automation;
it does not certify every final tick, Mass-only outcome or visual quality.
"""
import json
import time
import uuid
from pathlib import Path

import unreal as u

library = Path(u.Paths.project_dir()) / "Scripts/GA/DynamicMage/verify_real_casting_pie.py"
output = Path(u.Paths.project_saved_dir()).resolve() / "GAAuthoring/DynamicMage/CatalogCasting"
batch_run = output / uuid.uuid4().hex
batch_run.mkdir(parents=True, exist_ok=True)
batch = {"started": time.monotonic(), "finished": False, "passed": False,
         "scope": "Exact catalog default recipe; original local cursor/T/TargetData/Notify input smoke.",
         "limitations": ["Numerical parity is recorded separately", "Not OS keyboard injection",
                         "Visual quality and complete natural tails remain separate acceptance"],
         "rows": [], "next": 0, "error": None}
binary_record = Path(u.Paths.project_saved_dir()).resolve() / "GAAuthoring/DynamicMage/20261119-implementation/pie-loaded-build.json"
batch["loaded_binary"] = json.loads(binary_record.read_text(encoding="utf-8-sig"))
active = None
definitions = None
catalog_lookup = None


def write_batch():
    (batch_run / "report.json").write_text(json.dumps(batch, ensure_ascii=False, indent=2), encoding="utf-8")
    (output / "latest.json").write_text(json.dumps({"report": str(batch_run / "report.json")}), encoding="utf-8")


def serialize_record(q, record):
    """Only public immutable observations are recorded; never retain a game object."""
    result = {"root": q["guid_hex"](record.get_editor_property("root_cast_id"))}
    for field in ("sequence", "kind", "node_type", "stage", "revision", "frozen_hash",
                  "confirmed_hit", "damage_attempted", "actual_damage_known", "actual_damage",
                  "applied_known", "applied", "cost_succeeded", "end_reason"):
        value = record.get_editor_property(field)
        result[field] = value if isinstance(value, (bool, int, float)) else str(value)
    return result


def authored_cost_contract(base):
    """Read the actual original frontend; an Offensive tag does not imply a fee."""
    cdo = u.get_default_object(base.get_editor_property("ability_class"))
    if isinstance(cdo, u.PrototypeAbility):
        prototype = cdo.get_editor_property("prototype")
        if prototype:
            value = float(prototype.get_editor_property("mana_cost"))
            return {"required": value > 0, "source": "Actual original Prototype.ManaCost", "value": value,
                    "periodic_upkeep": float(prototype.get_editor_property("mana_per_second"))}
    return {"required": "PASSIVE" not in str(base.get_editor_property("ability_type")).upper(),
            "source": "Native frontend cost observation; original passive may have no activation fee"}


def create_case(expected):
    q = {"_DYNAMIC_MAGE_LIBRARY_ONLY": True, "__name__": "dynamic_mage_catalog_case"}
    exec(library.read_text(encoding="utf-8"), q)
    state = q["state"]
    observation_cache = {}
    state.update(scenario="ExactCatalogDefaultRealInput", expected=expected,
                 limitations=batch["limitations"], catalog_batch=True)

    def case_tick(_delta):
        try:
            if state["phase"].startswith("cleanup"):
                q["cleanup_tick"]()
                return
            if time.monotonic() - state["started"] > 45:
                raise AssertionError("Exact class input timed out in " + state["phase"])
            local, server, _other = q["participants"]()
            if not local or not server:
                return
            if q["pinned"]:
                q["check"]("exact_pinned_lives_remain", q["same_participant_lives"]())
            age = time.monotonic() - state.get("at", state["started"])
            if state["phase"] == "setup":
                if not q["setup_fixture"]((125, 250, 400)):
                    return
                matches = [row for row in q["dev"](local).get_definitions()
                           if str(row.get_editor_property("definition_id")) == expected["definition_id"]]
                q["check"]("trusted_exact_definition_exists", len(matches) == 1)
                actual = matches[0].get_editor_property("base").get_editor_property("ability_class")
                q["check"]("trusted_definition_is_exact_original_class", actual.get_path_name() == expected["class"])
                state["begin_state_sequences"] = {
                    pc.get_path_name(): int(q["preview"](pc).get_editor_property("state_sequence"))
                    for pc in {local, server}}
                state["owns_preview_request"] = True
                q["command"](local, "BeginDefinition " + expected["definition_id"])
                q["phase"]("begin")
            elif state["phase"] == "begin":
                if not q["preview_started"](local, server):
                    return
                # Different Actor/Mixed classes can share the same original
                # cooldown tag. Let that real cooldown expire; never remove it.
                if not all(q["dev"](pc).is_preview_frontend_idle() for pc in {local, server}):
                    return
                replicas = q["ready_replicas"]()
                if not replicas:
                    return
                q["check"]("both_worlds_selected_exact_definition", all(
                    str(q["preview"](pc).get_editor_property("selected_id")) == expected["definition_id"]
                    for pc in {local, server}))
                state["before_health"] = q["healths"](q["owned"])
                state["before_mana"] = q["attribute"](q["component"](server, u.MyAbilitySystemComponent), "mana")
                state["before_sequence"] = max([int(r.get_editor_property("sequence")) for r in q["records"](server)] + [0])
                state["expected_hash"] = str(q["preview"](server).get_editor_property("content_hash"))
                state["expected_revision"] = int(q["preview"](server).get_editor_property("revision"))
                q["aim_at"](replicas[0])
                q["phase"]("aim")
            elif state["phase"] == "aim":
                replicas = q["ready_replicas"]()
                if replicas:
                    q["aim_at"](replicas[0])
                if age >= .5:
                    q["command"](local, "Press")
                    q["phase"]("observe")
            elif state["phase"] == "observe":
                for record in q["new_records"](server, state["before_sequence"]):
                    value = serialize_record(q, record)
                    observation_cache[value["sequence"]] = value
                rows = list(observation_cache.values())
                if age >= 2 and not state.get("released"):
                    q["command"](local, "Release")
                    state["released"] = True
                if age >= 2.25 and not state.get("confirmation_requested") and not any(
                    r["kind"] in (str(u.AbilityRuntimeObservationKind.GRAPH_EVENT),
                                  str(u.AbilityRuntimeObservationKind.GRAPH_FINISHED))
                    for r in rows):
                    # Some original frontends wait for a second real input to
                    # confirm the cursor target. Do not inject its TargetData.
                    q["command"](local, "Press")
                    state["confirmation_requested"] = True
                if age >= 3 and state.get("confirmation_requested") and not state.get("confirmation_released"):
                    q["command"](local, "Release")
                    state["confirmation_released"] = True
                if age < 4:
                    return
                graph_rows = [r for r in rows if r["kind"] in (
                    str(u.AbilityRuntimeObservationKind.GRAPH_EVENT), str(u.AbilityRuntimeObservationKind.GRAPH_FINISHED))]
                costs = [r for r in rows if r["kind"] == str(u.AbilityRuntimeObservationKind.COST)]
                actions = [r for r in graph_rows if r["kind"] == str(u.AbilityRuntimeObservationKind.GRAPH_EVENT)]
                state["after_health"] = q["healths"](q["owned"])
                state["after_mana"] = q["attribute"](q["component"](server, u.MyAbilitySystemComponent), "mana")
                state["actual_observations"] = rows
                inventory_count = sum(a.get_owner() == server.get_controlled_pawn()
                                      for a in u.GameplayStatics.get_all_actors_of_class(server.get_world(), u.MyProjectile))
                orbit_inventory = "ORBIT" in expected["frontend"].upper() and inventory_count > 0 and bool(costs)
                q["check"]("original_input_reached_actual_action_or_paid_orbit_inventory", bool(actions) or orbit_inventory)
                finished = [r for r in graph_rows if r["kind"] == str(u.AbilityRuntimeObservationKind.GRAPH_FINISHED)]
                q["check"]("no_failed_budget_or_invalid_source_terminal", all(
                    r["end_reason"] not in (str(u.AbilityGraphEndReason.FAILED),
                        str(u.AbilityGraphEndReason.BUDGET_EXCEEDED), str(u.AbilityGraphEndReason.SOURCE_INVALID))
                    for r in finished))
                q["check"]("actual_cast_uses_accepted_default_revision", all(
                    int(r["revision"]) == state["expected_revision"] for r in graph_rows + costs))
                q["check"]("actual_cast_uses_accepted_default_hash", all(
                    r["frozen_hash"] == state["expected_hash"] for r in graph_rows + costs))
                if expected["cost_contract"]["required"]:
                    state["cost_delta_diagnostic"] = state["before_mana"] - state["after_mana"]
                    q["check"]("original_paid_frontend_reports_a_real_cost_attempt", bool(costs))
                    state["cost_evidence"] = "Actual original frontend cost receipts, sampled each frame"
                q["check"]("observed_cost_attempts_succeeded", all(r["cost_succeeded"] for r in costs))
                state["observed_cost_count"] = len(costs)
                state["observed_action_ports"] = len(actions)
                state["observed_owned_projectiles"] = inventory_count
                state["observed_natural_terminals"] = [r["end_reason"] for r in finished]
                if _other:
                    q["check"]("other_player_recipe_isolated", q["preview"](_other).export_text() == state["other_preview_before"])
                q["finish"]()
        except Exception as error:
            q["finish"](repr(error))

    q["handle"] = u.register_slate_post_tick_callback(case_tick)
    q["write"]()
    return q


def tick_batch(_delta):
    global active, definitions, catalog_lookup
    try:
        if active:
            state = active["state"]
            if not state["finished"]:
                return
            batch["rows"].append({"definition": state["expected"], "passed": state["passed"],
                                  "report": str(active["run"] / "report.json"),
                                  "error": state.get("error") or state.get("test_error"),
                                  "cleanup_succeeded": state.get("cleanup_succeeded")})
            if not state.get("cleanup_succeeded"):
                batch["outstanding_owned_objects"] = [a.get_path_name() for a in
                    active["owned"] + active["owned_controllers"] if u.SystemLibrary.is_valid(a)]
                batch["outstanding_restore_bases"] = active["original_attributes"]
                raise AssertionError("Unsafe cleanup; stop catalog census rather than touching another life")
            active = None
            batch["next"] += 1
            write_batch()
        if definitions is None:
            if catalog_lookup is None:
                catalog_lookup = {"_DYNAMIC_MAGE_LIBRARY_ONLY": True, "__name__": "dynamic_mage_catalog_lookup"}
                exec(library.read_text(encoding="utf-8"), catalog_lookup)
            lookup = catalog_lookup
            local, _server, _other = lookup["participants"]()
            if not local:
                return
            catalog = lookup["dev"](local).get_definitions()
            if not catalog:
                if not batch.get("catalog_read_requested"):
                    # The existing native input probe initializes the public
                    # catalog before dispatch. Releasing T is harmless without
                    # a preview and does not grant or activate an ability.
                    lookup["command"](local, "Release")
                    batch["catalog_read_requested"] = time.monotonic()
                elif time.monotonic() - batch["catalog_read_requested"] > 10:
                    raise AssertionError("Public catalog did not initialize after the native input preflight")
                return
            definitions = []
            for row in catalog:
                base = row.get_editor_property("base")
                if not base.get_editor_property("selectable"):
                    continue
                definitions.append({"definition_id": str(row.get_editor_property("definition_id")),
                                    "class": base.get_editor_property("ability_class").get_path_name(),
                                    "frontend": str(base.get_editor_property("frontend_kind")),
                                    "cost_contract": authored_cost_contract(base),
                                    "target_contract": str(base.get_editor_property("target_contract"))})
            definitions.sort(key=lambda row: row["class"])
            if len(definitions) != 82 or len({row["class"] for row in definitions}) != 82:
                raise AssertionError("Catalog census must cover 82 exact gameplay classes; got " + str(len(definitions)))
            batch["catalog_class_count"] = len(definitions)
            selected_classes = globals().get("DYNAMIC_MAGE_CATALOG_CLASSES")
            if selected_classes is not None:
                selected_classes = set(selected_classes)
                if not selected_classes or not selected_classes.issubset({row["class"] for row in definitions}):
                    raise AssertionError("Regression subset must contain exact trusted catalog classes")
                definitions = [row for row in definitions if row["class"] in selected_classes]
                batch["scope"] += " Explicit regression subset of the full 82-class catalog."
            batch["expected_definitions"] = definitions
        if batch["next"] == len(definitions):
            batch.update(finished=True, passed=all(row["passed"] for row in batch["rows"]),
                         elapsed=time.monotonic() - batch["started"])
            write_batch()
            u.unregister_slate_post_tick_callback(batch_handle)
            u.log("DYNAMIC_MAGE_CATALOG_FINISHED " + str(batch_run / "report.json"))
            return
        active = create_case(definitions[batch["next"]])
    except Exception as error:
        batch.update(finished=True, passed=False, error=repr(error))
        write_batch()
        u.unregister_slate_post_tick_callback(batch_handle)
        u.log_error("DYNAMIC_MAGE_CATALOG_ABORTED " + repr(error))


batch_handle = u.register_slate_post_tick_callback(tick_batch)
write_batch()
u.log("DYNAMIC_MAGE_CATALOG_STARTED " + str(batch_run / "report.json"))
