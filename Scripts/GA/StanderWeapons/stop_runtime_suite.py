import gc,types,unreal as u
for fn in list(gc.get_objects()):
 if isinstance(fn,types.FunctionType) and fn.__name__=='tick' and fn.__code__.co_filename.replace('\\','/').endswith('StanderWeapons/run_runtime_suite.py'):
  g=fn.__globals__
  if g.get('report',{}).get('status')=='running':
   u.unregister_slate_post_tick_callback(g['handle']);g['report'].update(status='interrupted_probe_fix',passed=False)
   if g.get('current') and g['current'].get('entity'):g['sm'].remove_monster(g['current']['entity'],u.PureMassRemovalReason.RECYCLED)
   g['ps'].import_backpack_snapshot(g['original']);g['flush']();print('Weapon suite stopped for enum fix')
