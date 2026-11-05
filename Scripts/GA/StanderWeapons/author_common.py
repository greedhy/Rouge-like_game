"""Owned-asset authoring helpers. Import only after checking no PIE/dirty work."""
import unreal as u,json,uuid,shutil
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve();BASE='/Game/BluePrints/Characters/Stander/Weapons';OWNER='StanderWeaponsV1'
assert not u.EditorLevelLibrary.get_game_world(),'PIE active'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(),'Unsaved content'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(),'Unsaved map'
RUN=ROOT/'Saved/GAAuthoring/StanderWeapons'/('author-'+uuid.uuid4().hex);RUN.mkdir(parents=True)
TOOLS=u.AssetToolsHelpers.get_asset_tools();SAVED=[]
def existing(path):
 if not u.EditorAssetLibrary.does_asset_exist(path):return None
 a=u.load_asset(path);assert u.EditorAssetLibrary.get_metadata_tag(a,'AuthoringOwner')==OWNER,'Unowned asset '+path
 file=ROOT/'Content'/(path.removeprefix('/Game/')+'.uasset')
 if file.exists():shutil.copy2(file,RUN/(path.replace('/','_')+'.uasset'))
 return a
def save(a):
 u.EditorAssetLibrary.set_metadata_tag(a,'AuthoringOwner',OWNER)
 if isinstance(a,u.Blueprint):
  u.BlueprintEditorLibrary.compile_blueprint(a);assert str(a.get_editor_property('status'))!='BlueprintStatus.BS_ERROR'
 assert u.EditorAssetLibrary.save_loaded_asset(a,False);SAVED.append(a.get_path_name());return a
def duplicate(source,path):return existing(path) or u.EditorAssetLibrary.duplicate_asset(source,path)
def data(path,cls):
 a=existing(path)
 if not a:
  f=u.DataAssetFactory();f.set_editor_property('data_asset_class',cls);folder,name=path.rsplit('/',1);a=TOOLS.create_asset(name,folder,cls,f)
 assert isinstance(a,cls);return a
def bp(path,cls):
 a=existing(path)
 if not a:
  f=u.BlueprintFactory();f.set_editor_property('parent_class',cls.static_class());folder,name=path.rsplit('/',1);a=TOOLS.create_asset(name,folder,u.Blueprint,f)
 u.BlueprintEditorLibrary.compile_blueprint(a);return a,u.get_default_object(a.generated_class())
def tag(name):
 t=u.GameplayTag();t.import_text('(TagName="'+name+'")');assert name in t.export_text(),'Unregistered tag '+name;return t
def clip(source,path):
 a=duplicate(source,path)
 for k,v in dict(enable_root_motion=False,force_root_lock=True,root_motion_root_lock=u.RootMotionRootLock.ZERO).items():a.set_editor_property(k,v)
 for e in u.AnimationLibrary.get_animation_notify_events(a):u.AnimationLibrary.remove_animation_notify_events_by_name(a,e.get_editor_property('notify_name'))
 return save(a)
def montage(sequence,path):
 a=existing(path)
 if not a:
  f=u.AnimMontageFactory();f.set_editor_property('target_skeleton',sequence.get_editor_property('skeleton'));f.set_editor_property('source_animation',sequence);folder,name=path.rsplit('/',1);a=TOOLS.create_asset(name,folder,u.AnimMontage,f)
 for key in ['blend_in','blend_out']:
  b=a.get_editor_property(key);b.set_editor_property('blend_time',.06);a.set_editor_property(key,b)
 return save(a)
def transform(row):
 return u.Transform(location=u.Vector(*row['translation']),rotation=u.Quat(*row['rotation']).rotator(),scale=u.Vector(*row['scale']))
def merge_equipment(additions):
 table=u.load_asset('/Game/Equipment/DT_Equipment');assert table
 raw=u.DataTableFunctionLibrary.export_data_table_to_json_string(table);(RUN/'DT_Equipment-before.json').write_text(raw,encoding='utf-8');rows=json.loads(raw);byname={r['Name']:r for r in rows}
 for addition in additions:
  name=addition['Name']
  if name in byname:
   assert str(byname[name].get('WeaponSet','')).split('.')[0]==addition['WeaponSet'].split('.')[0],'Existing row identity conflict '+name
   continue
  rows.append(addition)
 for row in rows:
  identity=row.get('ItemDefinitionId')
  if isinstance(identity,dict):row['ItemDefinitionId']='BackpackItem:'+identity['PrimaryAssetName']
 assert u.DataTableFunctionLibrary.fill_data_table_from_json_string(table,json.dumps(rows,ensure_ascii=False))
 assert u.EditorAssetLibrary.save_loaded_asset(table,False)
 after=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table));assert len(after)==len(rows)
 (RUN/'DT_Equipment-after.json').write_text(json.dumps(after,ensure_ascii=False,indent=2),encoding='utf-8')

def montage_stages(sequences,path):
 """Compose a single slot timeline; returns real per-stage offsets for event timing."""
 result=montage(sequences[0],path);slot_tracks=list(result.get_editor_property('slot_anim_tracks'));assert slot_tracks
 slot=slot_tracks[0];track=slot.get_editor_property('anim_track');segments=[];offsets=[];cursor=0.
 for sequence in sequences:
  segment=u.AnimSegment()
  segment.import_text('(AnimReference="'+sequence.get_path_name()+'",StartPos='+str(cursor)+',AnimStartTime=0,AnimEndTime='+str(sequence.get_play_length())+',AnimPlayRate=1,LoopingCount=1)')
  segments.append(segment);offsets.append(cursor);cursor+=sequence.get_play_length()
 track.set_editor_property('anim_segments',segments);slot.set_editor_property('anim_track',track);slot_tracks[0]=slot;result.set_editor_property('slot_anim_tracks',slot_tracks)
 # Recompute native montage duration after changing reflected segment tracks.
 assert u.StanderWeaponActionAbility.rebuild_action_montage(result),path
 save(result)
 assert abs(result.get_play_length()-cursor)<.01,'Montage stage length requires editor refresh API: '+path
 save(result);return result,offsets
