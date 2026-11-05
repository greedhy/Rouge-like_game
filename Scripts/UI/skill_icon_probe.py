"""Exercise actual shared globe functions in PIE without editing a save or loadout."""
import unreal as u
import json, time
from pathlib import Path
out=Path(u.Paths.project_dir()).resolve()/'Saved/GAAuthoring/SkillIconBackground'
pcs=[p for p in u.ObjectIterator(u.PlayerController) if 'UEDPIE_' in p.get_path_name() and p.is_local_controller()]
assert pcs,'PIE required'
pc=pcs[0]
cls=u.load_class(None,'/Game/BluePrints/UI/SpellGlobe/WBP_SpellGlobe.WBP_SpellGlobe_C')
create=u.get_default_object(u.load_class(None,'/Script/UMG.WidgetBlueprintLibrary'))
catalog=u.load_asset('/Game/BluePrints/AbilitySystem/Data/DA_AbilityInfo')
rows=[r for r in catalog.get_editor_property('ability_information') if r.icon and ('Batch6' in r.ability_tag.export_text() or 'Abilities.Fire.FireBolt' in r.ability_tag.export_text())]
skill_icon_probe_widgets=[]
checks=[]
for i,row in enumerate(rows):
 w=create.call_method('Create',(pc.get_world(),cls,pc));w.add_to_viewport(100)
 w.set_position_in_viewport(u.Vector2D(30+i*115,180),False);w.set_desired_size_in_viewport(u.Vector2D(100,100))
 icon=create.call_method('MakeBrushFromTexture',(row.icon,100,100))
 bg=create.call_method('MakeBrushFromMaterial',(row.background_material,100,100))
 w.call_method('SetIconAndBrush',(icon,bg));w.force_layout_prepass()
 images={x.get_name():x for x in u.ObjectIterator(u.Image) if x.get_path_name().startswith(w.get_path_name()+'.')}
 image=images['Image_Background']
 checks.append({'tag':row.ability_tag.export_text(),'has_background':bool(row.background_material),'opacity':image.get_render_opacity(),'resource':str(image.brush.resource_object)})
 assert image.get_render_opacity()==(1. if row.background_material else 0.),checks[-1]
 skill_icon_probe_widgets.append(w)
# Validate repeated transitions on the same widget (including clear/refill).
w=skill_icon_probe_widgets[0]
image=next(x for x in u.ObjectIterator(u.Image) if x.get_path_name().startswith(w.get_path_name()+'.') and x.get_name()=='Image_Background')
for material in [None,rows[0].background_material,None,rows[0].background_material]:
 w.call_method('SetIconAndBrush',(create.call_method('MakeBrushFromTexture',(rows[0].icon,100,100)),create.call_method('MakeBrushFromMaterial',(material,100,100))))
 assert image.get_render_opacity()==(1. if material else 0.)
w.call_method('ClearGlobe')
assert image.brush.tint_color.specified_color.a==0.,image.brush.export_text()
w.call_method('SetIconAndBrush',(create.call_method('MakeBrushFromTexture',(rows[0].icon,100,100)),create.call_method('MakeBrushFromMaterial',(rows[0].background_material,100,100))))
assert image.get_render_opacity()==1. and image.brush.resource_object==rows[0].background_material
menu_cls=u.load_class(None,'/Game/BluePrints/UI/SpellMenu/WBP_My_SpellGLobeButton.WBP_My_SpellGLobeButton_C')
menu=create.call_method('Create',(pc.get_world(),menu_cls,pc));menu.add_to_viewport(100)
menu.set_position_in_viewport(u.Vector2D(30,300),False)
menu_checks=[]
for row in [rows[1],rows[0],rows[1]]:
 menu.set_editor_property('AbilityTag',row.ability_tag)
 for status in ['Equipped','Eligible','Unlocked','Locked','Equipped']:
  tag=u.GameplayTag();tag.import_text('(TagName="Abilities.Status.'+status+'")')
  row.set_editor_property('status_tag',tag)
  menu.call_method('ReceiveAbiityInfo',(row,))
  bg=next(x for x in u.ObjectIterator(u.Image) if x.get_path_name().startswith(menu.get_path_name()+'.') and x.get_name()=='Image_Background')
  expected=1. if status in ['Eligible','Locked'] or row.background_material else 0.
  assert bg.get_render_opacity()==expected,(status,bg.get_render_opacity(),expected)
  menu_checks.append({'status':status,'tag':row.ability_tag.export_text(),'opacity':bg.get_render_opacity()})
menu.remove_from_parent()
(out/'probe-latest.json').write_text(json.dumps({'passed':True,'globe':checks,'same_globe_transitions':4,'clear_and_refill':True,'menu_transitions':menu_checks},indent=2),encoding='utf-8')
start=time.monotonic()
def capture(dt):
 if time.monotonic()-start<1:return
 u.unregister_slate_post_tick_callback(capture_handle)
 u.SystemLibrary.execute_console_command(pc.get_world(),'PureMass.QA.CaptureUI 0 SkillIconProbe')
capture_handle=u.register_slate_post_tick_callback(capture)
