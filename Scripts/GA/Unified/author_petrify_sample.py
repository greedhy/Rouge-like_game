"""Create missing sample assets only. Re-run in a second UE process with UNIFIED_GA_VERIFY_ONLY=1."""
import json
import math
import os
from datetime import datetime, timezone
from pathlib import Path
import unreal as u

BASE = "/Game/UnifiedAbilities/Samples"
A = u.EditorAssetLibrary
TOOLS = u.AssetToolsHelpers.get_asset_tools()
VERIFY_ONLY = os.environ.get("UNIFIED_GA_VERIFY_ONLY") == "1"
created = []


def struct(cls, text):
    value = cls()
    value.import_text(text)
    return value


def blueprint(name, parent):
    path = BASE + "/" + name
    bp = u.load_asset(path)
    fresh = bp is None
    if fresh:
        assert not VERIFY_ONLY, "Missing asset: " + path
        factory = u.BlueprintFactory()
        factory.set_editor_property("parent_class", parent)
        bp = TOOLS.create_asset(name, BASE, u.Blueprint, factory)
        assert bp, path
        u.BlueprintEditorLibrary.compile_blueprint(bp)
        created.append(path)
    cls = A.load_blueprint_class(path)
    assert cls and u.MathLibrary.class_is_child_of(cls, parent.static_class()), path
    return bp, u.get_default_object(cls), fresh


def save(bp):
    u.BlueprintEditorLibrary.compile_blueprint(bp)
    assert A.save_loaded_asset(bp, False), bp.get_path_name()


graph_path = BASE + "/DA_DelayedPetrify"
graph = u.load_asset(graph_path)
if graph is None:
    assert not VERIFY_ONLY, "Missing graph"
    factory = u.DataAssetFactory()
    factory.set_editor_property("data_asset_class", u.AbilityGraphDefinition)
    graph = TOOLS.create_asset("DA_DelayedPetrify", BASE, u.AbilityGraphDefinition, factory)
    assert graph
    delay = u.new_object(u.AbilityDelayNodeConfig, outer=graph, name="DelayConfig")
    delay.set_editor_property("delay_seconds", 0.25)
    petrify = u.new_object(u.AbilityPetrifyNodeConfig, outer=graph, name="PetrifyConfig")
    petrify.set_editor_property("duration_seconds", 1.0)
    material = u.load_asset("/Game/CompositionBatch6/M_PetrifiedStone")
    assert material, "Existing stone material required"
    petrify.set_editor_property("stone_material", material)
    first_id = struct(u.Guid, "(A=1829101,B=1,C=1,D=1)")
    second_id = struct(u.Guid, "(A=1829101,B=1,C=1,D=2)")
    first = u.AbilityGraphNode()
    first.set_editor_property("node_id", first_id)
    first.set_editor_property("type_id", "Control.Delay")
    first.set_editor_property("config", delay)
    second = u.AbilityGraphNode()
    second.set_editor_property("node_id", second_id)
    second.set_editor_property("type_id", "Status.Petrify")
    second.set_editor_property("config", petrify)
    edge = u.AbilityGraphEdge()
    edge.set_editor_property("from", first_id)
    edge.set_editor_property("to", second_id)
    edge.set_editor_property("output_port", "Completed")
    graph.set_editor_property("entry_node_id", first_id)
    graph.set_editor_property("nodes", [first, second])
    graph.set_editor_property("edges", [edge])
    assert A.save_loaded_asset(graph, False)
    created.append(graph_path)
assert isinstance(graph, u.AbilityGraphDefinition)

cost_bp, cost, fresh = blueprint("GE_DelayedPetrifyCost", u.GameplayEffect)
if fresh:
    cost.set_editor_property("duration_policy", u.GameplayEffectDurationType.INSTANT)
    modifier = struct(u.GameplayModifierInfo,
        '(Attribute=(AttributeName="Mana",Attribute=/Script/test_25d.MyAttributeSet:Mana,'
        'AttributeOwner="/Script/CoreUObject.Class\'/Script/test_25d.MyAttributeSet\'"),'
        'ModifierOp=AddBase,ModifierMagnitude=(MagnitudeCalculationType=ScalableFloat,'
        'ScalableFloatMagnitude=(Value=-10.0)))')
    cost.set_editor_property("modifiers", [modifier])
    save(cost_bp)

cooldown_bp, cooldown, fresh = blueprint("GE_DelayedPetrifyCooldown", u.GameplayEffect)
if fresh:
    cooldown.set_editor_property("duration_policy", u.GameplayEffectDurationType.HAS_DURATION)
    cooldown.set_editor_property("duration_magnitude", struct(u.GameplayEffectModifierMagnitude,
        '(MagnitudeCalculationType=ScalableFloat,ScalableFloatMagnitude=(Value=2.0))'))
    component_class = u.load_class(None, "/Script/GameplayAbilities.TargetTagsGameplayEffectComponent")
    assert component_class
    component = u.new_object(component_class, outer=cooldown, name="CooldownTags")
    component.set_editor_property("InheritableGrantedTagsContainer", struct(u.InheritedTagContainer,
        '(CombinedTags=(GameplayTags=((TagName="Cooldown.Unified.DelayedPetrify"))),'
        'Added=(GameplayTags=((TagName="Cooldown.Unified.DelayedPetrify"))),Removed=(GameplayTags=()))'))
    cooldown.set_editor_property("ge_components", [component])
    save(cooldown_bp)

ga_bp, ga, fresh = blueprint("GA_DelayedPetrify", u.UnifiedGraphAbility)
if fresh:
    ga.set_editor_property("graph", graph)
    ga.set_editor_property("cast_range", 1200.0)
    ga.set_editor_property("ability_tags", struct(u.GameplayTagContainer,
        '(GameplayTags=((TagName="Abilities.Unified.DelayedPetrify")))'))
    ga.set_editor_property("activation_blocked_tags", struct(u.GameplayTagContainer,
        '(GameplayTags=((TagName="Debuff.Stun"),(TagName="Effects.HitReact")))'))
    ga.set_editor_property("cost_gameplay_effect_class", A.load_blueprint_class(BASE + "/GE_DelayedPetrifyCost"))
    ga.set_editor_property("cooldown_gameplay_effect_class", A.load_blueprint_class(BASE + "/GE_DelayedPetrifyCooldown"))
    save(ga_bp)

# Verify references and report actual values; never overwrite existing designer tuning.
ga = u.get_default_object(A.load_blueprint_class(BASE + "/GA_DelayedPetrify"))
assert ga.get_editor_property("graph") == graph
assert ga.get_editor_property("cost_gameplay_effect_class") == A.load_blueprint_class(BASE + "/GE_DelayedPetrifyCost")
assert ga.get_editor_property("cooldown_gameplay_effect_class") == A.load_blueprint_class(BASE + "/GE_DelayedPetrifyCooldown")
nodes = list(graph.get_editor_property("nodes"))
assert len(nodes) == 2 and len(graph.get_editor_property("edges")) == 1
assert str(nodes[0].get_editor_property("type_id")) == "Control.Delay"
assert str(nodes[1].get_editor_property("type_id")) == "Status.Petrify"
cfg = nodes[1].get_editor_property("config")
assert cfg.get_editor_property("stone_material")
# UE Python 将 bool 作为成功标志，返回唯一 out 参数（失败为 None），而不是 (bool, out) 元组。
errors = graph.validate_for_authoring()
valid = errors is not None and len(errors) == 0
assert valid, "Native graph validation failed: " + repr(errors)
edge = graph.get_editor_property("edges")[0]
first_id = nodes[0].get_editor_property("node_id").export_text()
second_id = nodes[1].get_editor_property("node_id").export_text()
assert graph.get_editor_property("entry_node_id").export_text() == first_id
assert edge.get_editor_property("from").export_text() == first_id
assert edge.get_editor_property("to").export_text() == second_id
assert str(edge.get_editor_property("output_port")) == "Completed"
assert math.isfinite(ga.get_editor_property("cast_range")) and ga.get_editor_property("cast_range") > 0
assert "Abilities.Unified.DelayedPetrify" in ga.get_editor_property("ability_tags").export_text()


def static_value(magnitude):
    """This sample supports editable static values; reject unsupported magnitude strategies explicitly."""
    assert magnitude.export_text().startswith("(MagnitudeCalculationType=ScalableFloat,")
    scalable = magnitude.get_editor_property("scalable_float_magnitude")
    curve = scalable.get_editor_property("curve")
    assert not curve.get_editor_property("curve_table")
    assert str(curve.get_editor_property("row_name")) in ("", "None"), "DataRegistry/curve rows are not static values"
    value = scalable.get_editor_property("value")
    assert math.isfinite(value)
    return value


cost = u.get_default_object(A.load_blueprint_class(BASE + "/GE_DelayedPetrifyCost"))
cooldown = u.get_default_object(A.load_blueprint_class(BASE + "/GE_DelayedPetrifyCooldown"))
assert cost.get_editor_property("duration_policy") == u.GameplayEffectDurationType.INSTANT
modifiers = list(cost.get_editor_property("modifiers"))
assert len(modifiers) == 1, "Sample requires one standard Mana cost modifier"
assert "MyAttributeSet:Mana" in modifiers[0].get_editor_property("attribute").export_text()
assert "ModifierOp=AddBase," in modifiers[0].export_text()
mana_delta = static_value(modifiers[0].get_editor_property("modifier_magnitude"))
assert mana_delta <= 0, "Cost must not grant Mana"
assert cooldown.get_editor_property("duration_policy") == u.GameplayEffectDurationType.HAS_DURATION
cooldown_seconds = static_value(cooldown.get_editor_property("duration_magnitude"))
assert cooldown_seconds > 0
granted_tag_config = []
for component in cooldown.get_editor_property("ge_components"):
    if component.get_class().get_name() == "TargetTagsGameplayEffectComponent":
        inherited = component.get_editor_property("InheritableGrantedTagsContainer")
        granted_tag_config.append(inherited.get_editor_property("combined_tags").export_text())
assert any('TagName="Cooldown.Unified.DelayedPetrify"' in value for value in granted_tag_config), \
    "Cooldown must actually configure the sample's granted tag"
report = {
    "success": True, "verifyOnly": VERIFY_ONLY, "created": created,
    "ga": ga.get_class().get_path_name(), "graph": graph.get_path_name(),
    "delaySeconds": nodes[0].get_editor_property("config").get_editor_property("delay_seconds"),
    "petrifySeconds": cfg.get_editor_property("duration_seconds"),
    "material": cfg.get_editor_property("stone_material").get_path_name(),
    "costModifiers": [item.export_text() for item in cost.get_editor_property("modifiers")],
    "cooldown": cooldown.get_editor_property("duration_magnitude").export_text(),
    "cooldownComponents": [item.get_class().get_path_name() for item in cooldown.get_editor_property("ge_components")],
    "nativeGraphValidation": valid, "nodeIds": [first_id, second_id],
    "manaDelta": mana_delta, "cooldownSeconds": cooldown_seconds, "grantedTagConfiguration": granted_tag_config,
    "autoGranted": False,
}
folder = Path(u.Paths.project_dir()).resolve() / "Saved/GAAuthoring/UnifiedGA"
folder.mkdir(parents=True, exist_ok=True)
output = folder / ("sample-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ") + ".json")
output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
u.log("UNIFIED_GA_SAMPLE_VERIFIED " + str(output))
