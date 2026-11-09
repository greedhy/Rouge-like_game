"""Explicitly launch the native standalone PIE petrify probe and await its report.

Run only after the matching Editor module has been built and loaded:
  py ".../Scripts/GA/Unified/verify_petrify_pie.py"

Prerequisites: an existing standalone PIE session, focused game viewport, original
local player with >=30 Mana, enabled Mass, no DungeonRunDirector, no Actor/Mass
enemy population, and no existing delayed-petrify grant/cooldown. A blank QA play
area such as a locally prepared L_Batch6Test session is appropriate. This script
does not open a map, start/stop PIE, change input mode, or save any asset.

The native command grants the real sample only temporarily, projects its own
targets into the mouse viewport, and activates the ordinary player ASC path.
TargetData is collected by UPureMassTargetData; neither script nor probe injects
a target or invokes TargetReady. Stay idle for about 7 seconds. To stop early:
  UnifiedGA.CancelProbe

A successful result verifies standalone state behavior, not remote networking,
animation readability, ground alignment, or visual quality. If viewport/target
preflight rejects the run, fix the stated prerequisite and rerun; do not interpret
that as an effect test pass.
"""
import json
import time
from pathlib import Path

import unreal


def launch():
    """Start only in an existing PIE world and accept only a newly created report."""
    world = unreal.EditorLevelLibrary.get_game_world()
    if not world or "UEDPIE_" not in world.get_path_name():
        raise RuntimeError("An existing standalone PIE game world is required; no session was started.")
    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    if not controller or not controller.is_local_controller():
        raise RuntimeError("The current PIE world must have its original local player controller.")
    latest = Path(unreal.Paths.project_saved_dir()).resolve() / "GAAuthoring/UnifiedGA/pie-latest.txt"
    previous = latest.read_text(encoding="utf-8-sig").strip() if latest.exists() else None
    started = time.monotonic()
    state = {"report": None, "handle": None}

    def finish(message, failed=False):
        """Remove only this monitor callback; the native probe owns runtime cleanup."""
        unreal.unregister_slate_post_tick_callback(state["handle"])
        if failed:
            unreal.log_error(message)
        else:
            unreal.log(message)

    def tick(_delta):
        """Poll bounded fresh evidence without blocking the editor or simulating game time."""
        try:
            if time.monotonic() - started > 25:
                # Native probe has its own 20-second deadline and WorldCleanup handler.
                # Do not cancel an unrelated pre-existing probe if this invocation was rejected.
                if state["report"]:
                    unreal.SystemLibrary.execute_console_command(world, "UnifiedGA.CancelProbe", controller)
                finish("UnifiedGA PIE report timeout; verification incomplete.", True)
                return
            if not latest.exists():
                return
            selected = latest.read_text(encoding="utf-8-sig").strip()
            if not selected or selected == previous:
                return
            report_file = Path(selected)
            if state["report"] and report_file != state["report"]:
                finish("A second probe replaced the latest report; this monitor refuses mixed evidence.", True)
                return
            state["report"] = report_file
            try:
                report = json.loads(report_file.read_text(encoding="utf-8-sig"))
            except (FileNotFoundError, json.JSONDecodeError):
                return
            if not report.get("finished"):
                return
            checks = report.get("checks", {})
            complete = report.get("passed") is True and bool(checks) and all(checks.values())
            # These are actual behavior observations, not merely absence of errors.
            expected = [f"{case}_{check}" for case in ("actor", "mass", "cancel") for check in (
                "normal_activation", "one_10_mana_commit", "one_cooldown", "cooldown_duration_2s", "observed_pre_delay",
                "fixture_live_and_unchanged",
                "expected_effect_observed", "state_recovered", "ga_and_tasks_finished", "cooldown_expired")]
            expected += ["actor_stone_material", "actor_material_restored", "temporary_grant_removed",
                         "mana_base_restored", "temporary_mass_removed"]
            complete = complete and all(checks.get(name) is True for name in expected)
            if complete:
                finish(f"UnifiedGA standalone PIE probe passed ({len(checks)} checks): {report_file}. "
                       "Network and visual quality remain unverified.")
            else:
                finish(f"UnifiedGA PIE not verified: {report.get('error', 'missing expected checks')}. "
                       f"Report: {report_file}", True)
        except Exception as error:
            finish(f"UnifiedGA PIE monitor failed: {error!r}; native cleanup remains responsible.", True)

    state["handle"] = unreal.register_slate_post_tick_callback(tick)
    unreal.SystemLibrary.execute_console_command(world, "UnifiedGA.VerifyPetrify", controller)
    unreal.log("UnifiedGA standalone PIE probe requested. Keep the viewport focused and stay idle; no assets are saved.")


launch()
