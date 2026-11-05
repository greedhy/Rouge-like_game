import unreal as u
for path in ['/Game/CompositionBatch6/DA_SolarExplosion','/Game/CompositionBatch6/DA_FrostField']:
 a=u.load_asset(path);u.log(path+' class='+a.get_class().get_path_name()+' expected='+u.load_class(None,'/Script/test_25d.Batch6FieldPrototype').get_path_name())
