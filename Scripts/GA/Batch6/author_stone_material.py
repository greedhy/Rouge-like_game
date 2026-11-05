"""MCP editor: create independent stone material, no source assets modified."""
import unreal as u
BASE='/Game/CompositionBatch6'; name='M_PetrifiedStone'; path=BASE+'/'+name
lib=u.MaterialEditingLibrary
m=u.load_asset(path)
if not m:
 m=u.AssetToolsHelpers.get_asset_tools().create_asset(name,BASE,u.Material,u.MaterialFactoryNew())
 m.set_editor_property('material_domain',u.MaterialDomain.MD_SURFACE)
 m.set_editor_property('shading_model',u.MaterialShadingModel.MSM_DEFAULT_LIT)
 color=lib.create_material_expression(m,u.MaterialExpressionConstant3Vector,-350,-100)
 color.set_editor_property('constant',u.LinearColor(.22,.24,.25,1))
 lib.connect_material_property(color,'',u.MaterialProperty.MP_BASE_COLOR)
 rough=lib.create_material_expression(m,u.MaterialExpressionConstant,-350,100)
 rough.set_editor_property('r',.92)
 lib.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS)
lib.set_material_usage(m,u.MaterialUsage.MATUSAGE_SKELETAL_MESH)
lib.set_material_usage(m,u.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
lib.recompile_material(m)
assert u.EditorAssetLibrary.save_loaded_asset(m,False)
u.log('BATCH6_STONE_MATERIAL '+path)
