import unreal as u,json
from pathlib import Path
r={}
for path in ['/Game/BluePrints/Characters/Goblin/BP_GoblinSpear','/Game/RoguelikeGeneration/Gameplay/DA_DungeonMassMelee','/Game/PureMass/Rendering/AB_MassGoblin']:
 a=u.load_asset(path);obj=u.get_default_object(a.generated_class()) if isinstance(a,u.Blueprint) else a
 d={}
 for p in ['mesh','weapon','attack_montages','attack_animation','animation_provider','sequences','attack_windup_seconds','mesh_offset']:
  try:d[p]=str(obj.get_editor_property(p))
  except:pass
 if isinstance(a,u.Blueprint):
  for p in ['mesh','weapon']:
   try:
    c=obj.get_editor_property(p)
    d[p+'_details']={q:str(c.get_editor_property(q)) for q in ['skeletal_mesh_asset','relative_location','relative_rotation','relative_scale3d']}
    d[p+'_socket']=str(c.get_attach_socket_name())
   except Exception as e:d[p+'_error']=str(e)
 r[path]=d
seq=u.load_asset('/Game/Assets/Enemies/Goblin/Animations/Spear/Attack_Spear')
r['attack_length']=seq.get_play_length()
r['attack_notifies']=str(u.AnimationLibrary.get_animation_notify_events(seq))
Path(u.Paths.project_dir(),'Saved/GAAuthoring/MassGoblinArmed/inspect.json').write_text(json.dumps(r,indent=2))
