"""Editor MCP only: independent persistent Actor QA health GE. No player/source GE edits."""
import json
from pathlib import Path
import unreal as u
BASE='/Game/CompositionBatch6/QA';NAME='GE_Batch6QAHealth';PATH=BASE+'/'+NAME
A=u.EditorAssetLibrary;bp=u.load_asset(PATH) if A.does_asset_exist(PATH) else None
if not bp:
 factory=u.BlueprintFactory();factory.set_editor_property('parent_class',u.GameplayEffect)
 bp=u.AssetToolsHelpers.get_asset_tools().create_asset(NAME,BASE,u.Blueprint,factory)
assert bp,PATH
u.BlueprintEditorLibrary.compile_blueprint(bp)
cls=A.load_blueprint_class(PATH)
assert u.MathLibrary.class_is_child_of(cls,u.GameplayEffect.static_class()),PATH
cdo=u.get_default_object(cls)
cdo.set_editor_property('duration_policy',u.GameplayEffectDurationType.INSTANT)
mods=[]
for attribute in ['MaxHealth','Health']:
 mod=u.GameplayModifierInfo()
 mod.import_text('(Attribute=(AttributeName="'+attribute+'",Attribute=/Script/test_25d.MyAttributeSet:'+attribute+',AttributeOwner="/Script/CoreUObject.Class\'/Script/test_25d.MyAttributeSet\'"),ModifierOp=Override,ModifierMagnitude=(MagnitudeCalculationType=ScalableFloat,ScalableFloatMagnitude=(Value=10000.0,Curve=(CurveTable=None,RowName=""))))')
 mods.append(mod)
cdo.set_editor_property('modifiers',mods)
u.BlueprintEditorLibrary.compile_blueprint(bp)
assert A.save_loaded_asset(bp,False)
# Independent CDO readback after compilation/save.
cdo=u.get_default_object(A.load_blueprint_class(PATH))
assert cdo.get_editor_property('duration_policy')==u.GameplayEffectDurationType.INSTANT
actual=cdo.get_editor_property('modifiers');assert len(actual)==2
for attribute,mod in zip(['MaxHealth','Health'],actual):
 text=mod.export_text();assert 'MyAttributeSet:'+attribute in text and 'ModifierOp=Override' in text and 'Value=10000' in text,text
out=Path(u.Paths.project_dir()).resolve()/'Saved/GAAuthoring/CompositionBatch6/qa-health-effect.json'
out.write_text(json.dumps({'passed':True,'class':cls.get_path_name(),'duration':'Instant','modifiers':[m.export_text() for m in actual]},indent=2),encoding='utf-8')
u.log('BATCH6_QA_HEALTH_GE_AUTHORED '+cls.get_path_name())
