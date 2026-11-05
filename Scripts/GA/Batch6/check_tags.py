import unreal as u
c=u.load_asset('/Game/CompositionBatch6/DA_StarFlameHalo')
for k in ['kind','damage_type','cooldown_tag','cooldown','duration','range','radius']:u.log(k+'='+(c.get_editor_property(k).export_text() if k.endswith('tag') or k=='damage_type' else str(c.get_editor_property(k))))
u.log('native='+str(u.load_class(None,'/Script/test_25d.Batch6FieldAbility')))

