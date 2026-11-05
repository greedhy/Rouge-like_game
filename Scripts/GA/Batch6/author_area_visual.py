"""Create an independently visible icy area material; no shared materials modified."""
import unreal as u
path='/Game/CompositionBatch6/M_GlacialArea';lib=u.MaterialEditingLibrary
m=u.load_asset(path)
if not m:
 m=u.AssetToolsHelpers.get_asset_tools().create_asset('M_GlacialArea','/Game/CompositionBatch6',u.Material,u.MaterialFactoryNew())
 m.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT)
 m.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
 m.set_editor_property('two_sided',True)
 color=lib.create_material_expression(m,u.MaterialExpressionConstant3Vector,-300,-100)
 color.set_editor_property('constant',u.LinearColor(.18,.9,1.8,1))
 lib.connect_material_property(color,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
 opacity=lib.create_material_expression(m,u.MaterialExpressionConstant,-300,100);opacity.set_editor_property('r',.32)
 lib.connect_material_property(opacity,'',u.MaterialProperty.MP_OPACITY)
lib.set_material_usage(m,u.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
lib.recompile_material(m);assert u.EditorAssetLibrary.save_loaded_asset(m,False)
# Increase projectile presentation only. Its authority collision remains 35 cm.
a=u.load_asset('/Game/CompositionBatch6/DA_SolarFireball');a.set_editor_property('loop_vfx_scale',u.Vector(5,5,5));assert u.EditorAssetLibrary.save_loaded_asset(a,False)
u.log('BATCH6_AREA_MATERIAL_READY')
