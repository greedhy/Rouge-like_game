"""PIE-only crowd congestion observation; no assets/configuration are modified."""
import unreal as u
import json, time, math, uuid
from pathlib import Path

out = Path(u.Paths.project_dir())/'Saved/PureMass/Movement'/uuid.uuid4().hex
out.mkdir(parents=True)
start = time.monotonic()
began = None
samples = []
initial = {}
last = 0
perf = u.get_default_object(u.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
old = perf.get_editor_property('bThrottleCPUWhenNotForeground')
perf.set_editor_property('bThrottleCPUWhenNotForeground', False)

def finish(error=None):
    u.unregister_slate_post_tick_callback(handle)
    perf.set_editor_property('bThrottleCPUWhenNotForeground', old)
    result = {'error': error, 'samples': samples,
              'note': 'XY distances are capsule-centre distances; visual silhouettes may extend beyond capsules.'}
    (out/'runtime.json').write_text(json.dumps(result, indent=2))
    (out.parent/'latest.txt').write_text(str(out))

def tick(dt):
    global began, last, initial
    try:
        now = time.monotonic()
        assert now-start < 100, 'PIE timeout'
        sims = [s for s in u.ObjectIterator(u.PureMassSubsystem) if s.get_world()
                and 'UEDPIE_' in s.get_world().get_path_name()]
        sim = next((s for s in sims if u.GameplayStatics.get_game_mode(s.get_world())), None)
        if not sim: return
        w = sim.get_world()
        directors = u.GameplayStatics.get_all_actors_of_class(w, u.DungeonRunDirector)
        if not directors: return
        r = json.loads(directors[0].get_run_report())
        assert not r['error'], r['error']
        pcs = u.GameplayStatics.get_all_actors_of_class(w, u.DungeonRunController)
        if not pcs or any(not p.get_controlled_pawn() for p in pcs): return
        for pc in pcs:
            attrs = pc.player_state.get_component_by_class(u.MyAbilitySystemComponent).get_attribute_set(u.MyAttributeSet)
            for n in ['max_health', 'health']:
                value = attrs.get_editor_property(n)
                value.import_text('(BaseValue=1000000,CurrentValue=1000000)')
                attrs.set_editor_property(n,value)
        if began is None:
            gs = u.GameplayStatics.get_all_actors_of_class(w,u.DungeonGenerationRuntime)
            if not gs or not all(g.is_local_ready() for g in gs) or r['phase'] != 'Exploring': return
            room = next(x for x in r['rooms'] if x['role']=='Combat' and x['phase']=='Discovered')
            for i,p in enumerate(pcs):
                p.get_controlled_pawn().set_actor_location(u.Vector(room['x']+i*120,room['y'],110),False,True)
            began = now
            return
        if now-last < .1: return
        last = now
        views = sim.get_snapshots()
        if not views: return
        for v in views:
            initial.setdefault(v.id, (v.location.x,v.location.y,v.location.z))
        gap = 100000.
        for i,a in enumerate(views):
            for b in views[i+1:]:
                if abs(a.location.z-b.location.z) < a.type.half_height+b.type.half_height:
                    gap = min(gap, math.hypot(a.location.x-b.location.x,a.location.y-b.location.y)-a.type.radius-b.type.radius)
        samples.append({'age':now-began, 'count':len(views), 'minimum_gap':gap,
            'max_height_change':max(abs(v.location.z-initial[v.id][2]) for v in views),
            'moving':sum(v.state==1 for v in views), 'attacking':sum(v.state in [2,4] for v in views),
            'max_displacement':max(math.hypot(v.location.x-initial[v.id][0],v.location.y-initial[v.id][1]) for v in views)})
        if now-began>20:
            for i in range(len(sims)):
                u.SystemLibrary.execute_console_command(w, 'PureMass.QA.CaptureUI '+str(i)+' CrowdMovement_C'+str(i))
            finish()
    except Exception as e:
        finish(repr(e))

handle = u.register_slate_post_tick_callback(tick)
