import unreal as u,json
from pathlib import Path
assert not u.EditorLevelLibrary.get_game_world(), 'Asset metadata readback requires stopped PIE'
r={'assets':[]};root='/Game/BluePrints/Characters/Stander/'
for a in u.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(root.rstrip('/'),True):
 obj=a.get_asset();row={'path':obj.get_path_name(),'class':obj.get_class().get_name(),'owner':u.EditorAssetLibrary.get_metadata_tag(obj,'AuthoringOwner')}
 if isinstance(obj,u.Blueprint):row['status']=str(obj.get_editor_property('status'))
 r['assets'].append(row)
cfg=u.load_asset(root+'DA_Stander');r['strikes']=[]
for name,st in [('Combo'+str(i+1),v) for i,v in enumerate(cfg.get_editor_property('combo'))]+[(k,cfg.get_editor_property(k.lower())) for k in ['Counter','Spin','Dash']]:
 r['strikes'].append({'name':name,'montage':st.get_editor_property('montage').get_path_name(),'contact':st.get_editor_property('contact'),'damage':st.get_editor_property('damage'),'radius':st.get_editor_property('radius')})
r['dash']={k:cfg.get_editor_property(k) for k in ['dash_start','dash_end','dash_distance']}
bp=u.load_asset(root+'BP_StanderPlayer');cdo=u.get_default_object(bp.generated_class());r['startup']=[x.get_path_name() for x in cdo.get_editor_property('startup_ability')]
r['hit_effect']=str(cfg.get_editor_property('hit_effect'));r['passed']=len(r['startup'])==4 and all(x['owner']=='StanderShowcaseV1' and x.get('status','')!='BlueprintStatus.BS_ERROR' for x in r['assets'])
(Path(u.Paths.project_dir())/'Saved/GAAuthoring/Stander/readback.json').write_text(json.dumps(r,indent=2));assert r['passed']
