"""Read-only RPGHeroSquad inventory; never saves assets or changes the level."""
import unreal as u
import json
from pathlib import Path

report={'root':'/Game/RPGHeroSquad','assets':[],'errors':[],'players':[]}
registry=u.AssetRegistryHelpers.get_asset_registry()
assets=list(registry.get_assets_by_path('/Game/RPGHeroSquad',recursive=True))
assets+=list(registry.get_assets_by_path('/Game/Assets/Characters/Aura/Animations/Motion/01_Sword_Shield',recursive=True))
for data in assets:
    kind=str(data.asset_class_path.asset_name)
    if kind not in ['AnimSequence','Skeleton','SkeletalMesh','Blueprint','AnimBlueprint','AnimMontage','BlendSpace','BlendSpace1D']:
        continue
    row={'name':str(data.asset_name),'class':kind,'path':str(data.package_name)}
    if kind=='Blueprint':
        row['parent_tag']=str(data.get_tag_value('ParentClass'))
    try:
        obj=data.get_asset()
        for key in ['skeleton','parent_class','enable_root_motion','physics_asset']:
            try:
                value=obj.get_editor_property(key)
                row[key]=value.get_path_name() if isinstance(value,u.Object) else str(value)
            except Exception: pass
        if isinstance(obj,u.AnimSequence):
            row['duration']=obj.get_play_length()
            row['notifies']=[event.export_text() for event in u.AnimationLibrary.get_animation_notify_events(obj)]
            opts=u.AnimPoseEvaluationOptions(evaluation_type=u.AnimDataEvalType.RAW,should_retarget=False,extract_root_motion=False)
            pose=u.AnimPoseExtensions.get_anim_pose_at_time(obj,0,opts)
            bones=u.AnimPoseExtensions.get_bone_names(pose)
            row['bone_count']=len(bones)
            row['bones']=[str(b) for b in bones]
        if isinstance(obj,u.SkeletalMesh):
            component=u.new_object(u.SkeletalMeshComponent)
            component.set_skeletal_mesh_asset(obj)
            row['socket_and_bone_names']=[str(s) for s in component.get_all_socket_names()]
    except Exception as e: report['errors'].append({'path':row['path'],'error':str(e)})
    report['assets'].append(row)
for path in ['/Game/BluePrints/Characters/Player/BP_Player','/Game/RoguelikeGeneration/Gameplay/BP_DungeonPlayer','/Game/BluePrints/Characters/Player/ABP_Player_SwordShield']:
    data=registry.get_asset_by_object_path(path+'.'+path.rsplit('/',1)[-1])
    obj=u.load_asset(path)
    row={'path':path,'parent':str(data.get_tag_value('ParentClass')),'class':str(data.asset_class_path.asset_name)}
    try:row['skeleton']=str(obj.get_editor_property('target_skeleton'))
    except Exception:pass
    report['players'].append(row)
folder=Path(u.Paths.project_dir()).resolve()/'Saved/PlayerAssetAudit'
folder.mkdir(parents=True,exist_ok=True)
(folder/'rpg-hero-inventory.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('Player asset audit complete:',len(report['assets']),'assets;',len(report['errors']),'errors')
