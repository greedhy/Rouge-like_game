"""Actor/GAS two-player PIE: real remote EnhancedInput and replicated presentation.

Run in a two-player listening-server PIE session on L_Batch6Test. The script creates only its own
transient fixtures and never kills the map population. Learning/equipping RPCs
are setup; casts exclusively enter the remote EnhancedInput subsystem.
"""
import unreal as u
import json, time, uuid
from pathlib import Path

IDS=['StarFlameHalo','PetrifyingGaze','SolarFireball','GlacialStorm','FissionChain','EarthShockwave']
RUN=Path(u.Paths.project_dir()).resolve()/'Saved/GAAuthoring/CompositionBatch6'/uuid.uuid4().hex
RUN.mkdir(parents=True)
q={'phase':'setup','started':time.monotonic(),'checks':{},'samples':[],'index':0,'passed':False,
   'cast_transport':'remote EnhancedInputLocalPlayerSubsystem.inject_input_vector_for_action',
   'setup_transport':'server learning/equip RPCs only; never used to cast',
   'report':str(RUN/'actor-pie.json')}
owned=[]

def materials(actor):
    mesh=actor.get_component_by_class(u.SkeletalMeshComponent)
    return [mesh.get_material(i).get_path_name() if mesh.get_material(i) else None for i in range(mesh.get_num_materials())]

def spawn_fixture(world,position):
    cls=u.load_class(None,'/Game/BluePrints/Characters/Goblin/BP_GoblinSpear.BP_GoblinSpear_C');assert cls,'Actor fixture class missing'
    stat=u.get_default_object(u.GameplayStatics.static_class());tf=u.Transform(location=position)
    enemy=stat.call_method('BeginDeferredActorSpawnFromClass',(world,cls,tf,u.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
    assert enemy,'Deferred Actor fixture spawn failed'
    enemy.set_editor_property('auto_possess_ai',u.AutoPossessAI.DISABLED)
    stat.call_method('FinishSpawningActor',(enemy,tf,u.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT));owned.append(enemy)
    asc=enemy.get_component_by_class(u.MyAbilitySystemComponent)
    health_ge=u.load_class(None,'/Game/CompositionBatch6/QA/GE_Batch6QAHealth.GE_Batch6QAHealth_C');assert health_ge,'Persistent QA health GE missing'
    asc.call_method('BP_ApplyGameplayEffectToSelf',(health_ge,1.0,asc.make_effect_context()))
    assert attr(asc,'health')>=9000,'Stable QA health GE did not apply'
    return enemy
def write(): (RUN/'actor-pie.json').write_text(json.dumps(q,indent=2,ensure_ascii=False),encoding='utf8')
def tag(s):
    value=u.GameplayTag();value.import_text('(TagName="'+s+'")');return value
def attr(asc,name):
    aset=asc.get_attribute_set(u.MyAttributeSet);assert aset,'Missing production attribute set'
    field=aset.get_editor_property(name)
    return field.get_editor_property('current_value')
def check(name,value):
    q['checks'][name]=bool(value);assert value,name
def phase(name):q.update(phase=name,at=q['now']);write()
def objects(world,cls):return u.GameplayStatics.get_all_actors_of_class(world,cls)
def remote_fixture(world,server_actor):
    # Dynamic replicated Actors may receive different local object names when several spawn together.
    # Fixtures are stationary and spaced >100 cm apart, so require a unique replicated position match.
    position=server_actor.get_actor_location()
    matches=[a for a in objects(world,u.EnemyCharacter) if (a.get_actor_location()-position).length()<3.0]
    return matches[0] if len(matches)==1 else None
def ability_diagnostics(asc):
    # The project GetSpecFromAbilityTag helper is native-only. Export the reflected
    # ASC container as diagnostic evidence rather than inventing a Python API.
    try:return {'activatable_abilities':asc.get_editor_property('ActivatableAbilities').export_text()}
    except Exception as error:return {'active_spec_diagnostic_unavailable':repr(error)}
def runtimes(world):
    result=[]
    for actor in objects(world,u.PureMassRuntime)+objects(world,u.Batch6ChainPresentation):
        owner=actor.get_owner()
        if not owner or owner.get_path_name()!=q.get('caster_paths',{}).get(world.get_path_name()):continue
        if isinstance(actor,u.PureMassRuntime):
            config=actor.get_editor_property('prototype')
            if not config or not config.get_path_name().startswith('/Game/CompositionBatch6/'):continue
        elif IDS[q['index']]!='FissionChain':continue
        if actor.get_path_name() in q.get('preexisting_runtime_paths',[]):continue
        result.append(actor)
    return result
def finish(error=None):
    u.unregister_slate_post_tick_callback(handle)
    for actor in owned:
        if u.SystemLibrary.is_valid(actor):actor.destroy_actor()
    q.update(error=error,passed=error is None and all(q['checks'].values()))
    q['limitations']=['No screenshot readability certification','No save/reload or map travel test','No pool second-life test']
    write()
def inject(cp):
    lib=u.get_default_object(u.load_class(None,'/Script/Engine.SubsystemBlueprintLibrary'))
    subsystem=lib.call_method('GetLocalPlayerSubSystemFromPlayerController',(cp,u.EnhancedInputLocalPlayerSubsystem.static_class()))
    assert subsystem,'Remote EnhancedInput subsystem missing'
    action=u.load_asset('/Game/BluePrints/Input/InputActions/IA_Input4');assert action,'Slot 4 input action missing'
    subsystem.inject_input_vector_for_action(action,u.Vector(1,0,0),[],[])
    q.setdefault('input_events',[]).append({'skill':IDS[q['index']],'client':cp.get_path_name(),'at':q['now']})
def tick(dt):
    try:
        assert time.monotonic()-q['started']<240,'Timed out in '+q['phase']
        pcs=[p for p in u.ObjectIterator(u.MyPlayerController) if 'UEDPIE_' in p.get_path_name() and p.get_controlled_pawn()]
        cp=next((p for p in pcs if p.is_local_controller() and p.get_local_role()==u.NetRole.ROLE_AUTONOMOUS_PROXY),None)
        sp=next((p for p in pcs if not p.is_local_controller() and p.get_local_role()==u.NetRole.ROLE_AUTHORITY),None)
        if not cp or not sp:return
        sw,cw=sp.get_world(),cp.get_world();q['now']=u.GameplayStatics.get_time_seconds(sw)
        age=q['now']-q.get('at',q['now'])
        sa=sp.get_editor_property('player_state').get_component_by_class(u.MyAbilitySystemComponent)
        if q['phase']=='setup':
            check('open_qa_level',u.GameplayStatics.get_current_level_name(sw,True)=='L_Batch6Test')
            check('not_dungeon_room_gated',not objects(sw,u.DungeonRunDirector))
            check('remote_client_is_autonomous',cp.get_local_role()==u.NetRole.ROLE_AUTONOMOUS_PROXY)
            check('original_actor_player',sp.get_controlled_pawn().get_class().get_path_name()=='/Game/BluePrints/Characters/Player/BP_Player.BP_Player_C')
            points=sp.get_controlled_pawn().call_method('GetSpellPoints')
            if points<len(IDS):sp.get_controlled_pawn().call_method('AddToSpellPoints',(len(IDS)-points,))
            check('setup_learning_points_available',sp.get_controlled_pawn().call_method('GetSpellPoints')>=len(IDS))
            q['initial_population']={'actors':len(objects(sw,u.EnemyCharacter)),'points':points}
            check('qa_population_empty',q['initial_population']['actors']==0)
            for name in IDS:
                assert u.load_class(None,'/Game/CompositionBatch6/GA_'+name+'.GA_'+name+'_C'),name+' GA missing'
                sa.call_method('ServerSpendSpellPoint',(tag('Abilities.Batch6.'+name),))
            q['player_defaults']={'mana':attr(sa,'mana'),'max_mana':attr(sa,'max_mana'),'health':attr(sa,'health'),'max_health':attr(sa,'max_health')}
            u.get_default_object(u.load_class(None,'/Script/UMG.WidgetBlueprintLibrary')).call_method('SetInputMode_GameOnly',(cp,False))
            phase('prepare');return
        if q['phase']=='prepare':
            for actor in owned:
                if u.SystemLibrary.is_valid(actor):actor.destroy_actor()
            owned.clear()
            name=IDS[q['index']];origin=sp.get_controlled_pawn().get_actor_location()
            # Derive fixture position from the actual pawn; no map coordinate/population assumptions.
            config=u.load_asset('/Game/CompositionBatch6/DA_'+name);assert config,name+' config missing'
            fixture_distance=min(300,config.get_editor_property('radius')*.65) if name in ['StarFlameHalo','PetrifyingGaze'] else 300
            direction=sp.get_controlled_pawn().get_actor_forward_vector();center=origin+direction*fixture_distance
            actor_pos=center+u.Vector(0,65,0)
            enemy=spawn_fixture(sw,actor_pos)
            ea=enemy.get_component_by_class(u.MyAbilitySystemComponent)
            check(name+'_stable_actor_health',attr(ea,'health')>=9000)
            if name=='FissionChain':
                right=u.Vector(-direction.y,direction.x,0)
                for forward,lateral in [(160,-140),(160,140),(320,0)]:
                    spawn_fixture(sw,actor_pos+direction*forward+right*lateral)
                check(name+'_four_distinct_actor_fixtures',len(owned)==4 and len(set(a.get_path_name() for a in owned))==4)
            q['actor_baselines']={a.get_name():attr(a.get_component_by_class(u.MyAbilitySystemComponent),'health') for a in owned}
            q['material_before']=materials(enemy);q['client_material_changed']=False
            q['chain_segments_seen']=[]
            # Keep real Actor attributes; no direct imports that attribute recalculation can overwrite.
            q.update(enemy_name=enemy.get_name(),client_present=False,actor_control=False,actor_slow=False,
                     actor_before=attr(ea,'health'),server_present=False,mana_before=attr(sa,'mana'),baseline_velocity=0,
                     expected_upfront_cost=config.get_editor_property('mana_cost'),client_render_component=False)
            q['caster_name']=sp.get_controlled_pawn().get_name()
            q['caster_paths']={sw.get_path_name():sp.get_controlled_pawn().get_path_name(),cw.get_path_name():cp.get_controlled_pawn().get_path_name()}
            q['preexisting_runtime_paths']=[a.get_path_name() for world in [sw,cw] for cls in [u.PureMassRuntime,u.Batch6ChainPresentation] for a in objects(world,cls)]
            sa.call_method('ServerEquipAbility',(tag('Abilities.Batch6.'+name),tag('InputTag.4')))
            phase('settle');return
        name=IDS[q['index']];enemy=owned[0];ea=enemy.get_component_by_class(u.MyAbilitySystemComponent)
        check(name+'_same_player_life',sp.get_controlled_pawn().get_name()==q['caster_name'])
        assert u.SystemLibrary.is_valid(enemy),'QA Actor fixture destroyed; provide a stable high-health QA GE'
        client_enemy=remote_fixture(cw,enemy)
        if not client_enemy:
            assert age<8,'Actor fixture replication missing';return
        if q['phase']=='settle':
            if age<2:return
            q['base_actor_speed']=enemy.get_component_by_class(u.CharacterMovementComponent).get_editor_property('max_walk_speed')
            q['mana_before']=attr(sa,'mana');q['actor_before']=attr(ea,'health')
            check(name+'_enough_default_mana',q['mana_before']>=q['expected_upfront_cost']+15)
            q['last_mana']=q['mana_before'];q['mana_drops']=[];q['mana_frames']=[];q['last_mana_time']=q['now']
            q['ability_before_input']=ability_diagnostics(sa)
            aim=client_enemy.get_actor_location();screen=cp.project_world_location_to_screen(aim);assert screen,'Target cannot project to remote viewport'
            cp.set_mouse_location(int(screen.x),int(screen.y))
            # Give the remote cursor trace a full frame before injecting input.
            phase('aim');return
        if q['phase']=='aim':
            if age<.2:return
            q['last_mana']=attr(sa,'mana');q['last_mana_time']=q['now']
            inject(cp);phase('observe');return
        if q['phase']=='observe':
            current_mana=attr(sa,'mana');drop=q['last_mana']-current_mana
            interval=q['now']-q['last_mana_time'];regen=attr(sa,'mana_regeneration')
            q['mana_frames'].append({'at':age,'mana':current_mana,'regeneration':regen,'dt':interval})
            if drop>.25:q['mana_drops'].append({'at':age,'amount':drop,'regen_corrected':drop+regen*interval})
            q['last_mana']=current_mana
            q['last_mana_time']=q['now']
            if 'ability_after_input' not in q or q.get('ability_diag_index')!=q['index']:
                q['ability_after_input']=ability_diagnostics(sa);q['ability_diag_index']=q['index']
            q['latest_runtimes']={'server':[a.get_path_name() for a in runtimes(sw)],'client':[a.get_path_name() for a in runtimes(cw)]}
            q['server_present']|=bool(runtimes(sw));q['client_present']|=bool(runtimes(cw))
            for runtime in runtimes(cw):
                if isinstance(runtime,u.Batch6ChainPresentation):
                    segments=runtime.get_editor_property('segments')
                    if len(segments)>len(q['chain_segments_seen']):
                        q['chain_segments_seen']=[{'start':[s.start.x,s.start.y,s.start.z],'end':[s.end.x,s.end.y,s.end.z]} for s in segments]
                q['client_render_component']|=any(c.is_active() and c.is_visible() for c in runtime.get_components_by_class(u.NiagaraComponent))
                q['client_render_component']|=any(c.is_visible() and c.get_editor_property('static_mesh') for c in runtime.get_components_by_class(u.StaticMeshComponent))
            pc=enemy.get_component_by_class(u.Batch6PetrifyComponent)
            q['actor_control']|=ea.get_gameplay_tag_count(tag('Debuff.Stun'))>0
            speed=enemy.get_component_by_class(u.CharacterMovementComponent).get_editor_property('max_walk_speed')
            q['actor_slow']|=0<speed<q['base_actor_speed']*.8
            if name=='StarFlameHalo' and age>=2 and not q.get('toggle_off_'+name):
                inject(cp);q['toggle_off_'+name]=True
            if name=='PetrifyingGaze':
                ce=client_enemy.get_component_by_class(u.Batch6PetrifyComponent)
                q['client_present']|=bool(ce and client_enemy.get_component_by_class(u.MyAbilitySystemComponent).get_gameplay_tag_count(tag('Debuff.Stun'))>0)
                q['client_render_component']|=bool(ce and client_enemy.get_component_by_class(u.SkeletalMeshComponent).get_editor_property('bPauseAnims'))
                q['client_material_changed']|=materials(client_enemy)!=q['material_before']
            capture_after=1.4 if name=='GlacialStorm' else .2
            if age>=capture_after and q['client_present'] and not q.get('captured_'+name):
                u.SystemLibrary.execute_console_command(cw,'PureMass.QA.CaptureUI 1 Batch6_'+name)
                q['captured_'+name]=True
            if age<9:return
            # Record decreases at the cast frame; final mana is unsuitable because regeneration is active.
            check(name+'_remote_input_consumed_mana',bool(q['mana_drops']))
            upfront=[d for d in q['mana_drops'] if d['amount']>q['expected_upfront_cost']*.5]
            check(name+'_single_upfront_cost_event',len(upfront)==1)
            check(name+'_upfront_cost_within_one_mana',abs(upfront[0]['regen_corrected']-q['expected_upfront_cost'])<=1)
            if name=='StarFlameHalo':
                frames=[f for f in q['mana_frames'] if .4<=f['at']<=1.8]
                check(name+'_sustained_mana_samples',len(frames)>=2)
                elapsed=frames[-1]['at']-frames[0]['at']
                # ManaRegeneration is an attribute, not proof of an active regeneration GE.
                # In this QA loadout no regeneration ticks are applied; verify the measured drain.
                corrected=frames[0]['mana']-frames[-1]['mana']
                rate=u.load_asset('/Game/CompositionBatch6/DA_'+name).get_editor_property('mana_per_second')
                check(name+'_sustained_mana_rate_within_one',abs(corrected-rate*elapsed)<=1)
            if name=='PetrifyingGaze':
                check(name+'_actor_control',q['actor_control'])
                check(name+'_actor_recovered',ea.get_gameplay_tag_count(tag('Debuff.Stun'))==0)
                check(name+'_client_stone_material_seen',q['client_material_changed'])
                check(name+'_server_material_restored',materials(enemy)==q['material_before'])
                check(name+'_client_material_restored',materials(client_enemy)==q['material_before'])
                check(name+'_client_animation_resumed',not client_enemy.get_component_by_class(u.SkeletalMeshComponent).get_editor_property('bPauseAnims'))
            else:
                check(name+'_actor_damage',attr(ea,'health')<q['actor_before'])
            check(name+'_remote_presentation_observed',q['client_present'])
            check(name+'_remote_render_component_observed',q['client_render_component'])
            q['health_pair']={'server':attr(ea,'health'),'client':attr(client_enemy.get_component_by_class(u.MyAbilitySystemComponent),'health'),
                'server_path':enemy.get_path_name(),'client_path':client_enemy.get_path_name(),
                'server_location':str(enemy.get_actor_location()),'client_location':str(client_enemy.get_actor_location())}
            check(name+'_actor_health_replicated',abs(attr(client_enemy.get_component_by_class(u.MyAbilitySystemComponent),'health')-attr(ea,'health'))<.01)
            if name=='GlacialStorm':
                check(name+'_actor_slow',q['actor_slow'])
                check(name+'_actor_speed_restored',abs(enemy.get_component_by_class(u.CharacterMovementComponent).get_editor_property('max_walk_speed')-q['base_actor_speed'])<.01)
            if name=='FissionChain':
                damages={a.get_name():q['actor_baselines'][a.get_name()]-attr(a.get_component_by_class(u.MyAbilitySystemComponent),'health') for a in owned}
                q['chain_damage']=damages
                hit_count=sum(d>0 for d in damages.values())
                cdo=u.get_default_object(u.load_class(None,'/Game/CompositionBatch6/GA_FissionChain.GA_FissionChain_C'))
                budget=cdo.get_editor_property('max_targets')
                check(name+'_multiple_actor_targets_hit',hit_count>=3)
                check(name+'_global_budget_respected',hit_count<=min(budget,4))
                segments=q['chain_segments_seen'];check(name+'_replicated_segments_within_budget',0<len(segments)<=min(budget,4))
                degrees={}
                for segment in segments:
                    key=tuple(round(v,1) for v in segment['start']);degrees[key]=degrees.get(key,0)+1
                check(name+'_actual_fork_seen_on_client',max(degrees.values(),default=0)>=2)
                check(name+'_per_parent_branch_limit',max(degrees.values(),default=0)<=cdo.get_editor_property('branches_per_target'))
                for actor in owned:
                    remote=remote_fixture(cw,actor)
                    check(name+'_'+actor.get_name()+'_replicated_damage',remote is not None and abs(attr(remote.get_component_by_class(u.MyAbilitySystemComponent),'health')-attr(actor.get_component_by_class(u.MyAbilitySystemComponent),'health'))<.01)
            if name=='StarFlameHalo':
                check(name+'_toggle_cleanup_server',not runtimes(sw));check(name+'_toggle_cleanup_client',not runtimes(cw))
            q['samples'].append({k:q[k] for k in ['index','client_present','actor_control','actor_slow','mana_drops','mana_frames','ability_before_input','ability_after_input','actor_before']})
            q['index']+=1
            if q['index']==len(IDS):finish()
            else:phase('prepare')
    except Exception as error:finish(repr(error));u.log_error('BATCH6_ACTOR_PIE '+repr(error))
handle=u.register_slate_post_tick_callback(tick)
write()

