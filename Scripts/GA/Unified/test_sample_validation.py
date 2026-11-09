"""UE commandlet negative tests: mutate only loaded memory, restore in finally, never save assets."""
import json
import os
import runpy
from pathlib import Path
import unreal as u

os.environ["UNIFIED_GA_VERIFY_ONLY"] = "1"
root = Path(u.Paths.project_dir()).resolve()
author = root / "Scripts/GA/Unified/author_petrify_sample.py"
base = "/Game/UnifiedAbilities/Samples/"
graph = u.load_asset(base + "DA_DelayedPetrify")
cost = u.get_default_object(u.EditorAssetLibrary.load_blueprint_class(base + "GE_DelayedPetrifyCost"))
cooldown = u.get_default_object(u.EditorAssetLibrary.load_blueprint_class(base + "GE_DelayedPetrifyCooldown"))
assert graph and cost and cooldown
checks = {}


def rejects(name, mutate, restore):
    try:
        mutate()
        try:
            runpy.run_path(str(author), run_name="__main__")
        except AssertionError:
            checks[name] = True
        else:
            raise AssertionError("Malformed sample was certified: " + name)
    finally:
        restore()


def parsed(cls, text):
    value = cls()
    value.import_text(text)
    return value


entry = graph.get_editor_property("entry_node_id").export_text()
rejects("invalid_entry", lambda: graph.set_editor_property("entry_node_id", u.Guid()),
        lambda: graph.set_editor_property("entry_node_id", parsed(u.Guid, entry)))
saved_modifiers = [item.export_text() for item in cost.get_editor_property("modifiers")]
rejects("missing_mana_cost", lambda: cost.set_editor_property("modifiers", []),
        lambda: cost.set_editor_property("modifiers", [parsed(u.GameplayModifierInfo, text) for text in saved_modifiers]))
saved_components = list(cooldown.get_editor_property("ge_components"))
rejects("missing_cooldown_tag", lambda: cooldown.set_editor_property("ge_components", []),
        lambda: cooldown.set_editor_property("ge_components", saved_components))
saved_duration = cooldown.get_editor_property("duration_magnitude").export_text()
dynamic = '(MagnitudeCalculationType=ScalableFloat,ScalableFloatMagnitude=(Value=2.0,Curve=(CurveTable=None,RowName="RegistryRow")))'
rejects("dynamic_registry_row", lambda: cooldown.set_editor_property("duration_magnitude", parsed(u.GameplayEffectModifierMagnitude, dynamic)),
        lambda: cooldown.set_editor_property("duration_magnitude", parsed(u.GameplayEffectModifierMagnitude, saved_duration)))
runpy.run_path(str(author), run_name="__main__")
checks["restored_sample_valid"] = True
path = root / "Saved/GAAuthoring/UnifiedGA/sample-validation-tests.json"
path.write_text(json.dumps({"success": all(checks.values()), "checks": checks, "assetsSaved": False}, indent=2), encoding="utf-8")
u.log("UNIFIED_GA_SAMPLE_NEGATIVE_TESTS_PASSED " + str(path))
