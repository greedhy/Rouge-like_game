"""Execute in an active two-player PIE editor. Every missing prerequisite fails this run."""
import unreal as u
import json, time, uuid
from pathlib import Path

IDS = ['StarFlameHalo','PetrifyingGaze','SolarFireball','GlacialStorm','FissionChain','EarthShockwave']
RUN = Path(u.Paths.project_dir()).resolve()/'Saved/GAAuthoring/CompositionBatch6'/uuid.uuid4().hex
RUN.mkdir(parents=True)
state = {'started':time.monotonic(),'checks':{},'passed':False,'report':str(RUN/'menu.json')}
menus = []
def finish(error=None):
    u.unregister_slate_post_tick_callback(handle)
    for menu in menus: menu.remove_from_parent()
    state.update(error=error,passed=error is None and bool(state['checks']))
    (RUN/'menu.json').write_text(json.dumps(state,indent=2,ensure_ascii=False),encoding='utf8')
def tick(dt):
    try:
        assert time.monotonic()-state['started']<30,'Two local PIE views/menu prerequisites did not become available'
        pcs=[p for p in u.ObjectIterator(u.MyPlayerController) if 'UEDPIE_' in p.get_path_name() and p.is_local_controller() and p.get_controlled_pawn()]
        if len(pcs)!=2:return
        if 'capture_at' in state:
            if time.monotonic()-state['capture_at']<1:return
            u.SystemLibrary.execute_console_command(pcs[0].get_world(),'PureMass.QA.CaptureUI 1 Batch6_Menu')
            finish();return
        if not menus:
            for pc in pcs:
                cls=u.load_class(None,'/Game/BluePrints/UI/SpellMenu/WBP_SpellMune.WBP_SpellMune_C'); assert cls,'Spell menu asset missing'
                menu=u.get_default_object(u.load_class(None,'/Script/UMG.WidgetBlueprintLibrary')).call_method('Create',(pc.get_world(),cls,pc))
                menu.set_widget_controller(u.MyAbilitySystemLibrary.get_spell_menu_widget_controller(pc.get_world()))
                menu.add_to_viewport(); menus.append(menu)
            state['created']=time.monotonic();return
        if time.monotonic()-state['created']<1:return
        for index,menu in enumerate(menus):
            trees=[x for x in u.ObjectIterator(u.PrototypeSpellTree) if x.get_path_name().startswith(menu.get_path_name()+'.')]
            assert len(trees)==1,'Expected live spell tree'
            tree=trees[0];tree.call_method('ShowPrototypes');menu.force_layout_prepass()
            entries=[x for x in u.ObjectIterator(u.PrototypeSpellEntry) if x.get_path_name().startswith(tree.get_path_name()+'.')]
            state.setdefault('entry_counts',[]).append(len(entries))
            for name in IDS:
                config=u.load_asset('/Game/CompositionBatch6/DA_'+name);assert config,name+' config missing'
                label=str(config.get_editor_property('display_name'))
                found=[e for e in entries if any(t.get_path_name().startswith(e.get_path_name()+'.') and str(t.get_text())==label for t in u.ObjectIterator(u.TextBlock))]
                assert len(found)==1,name+' entry absent or duplicated'
                found[0].call_method('Select');menu.force_layout_prepass()
                found[0].get_parent().get_parent().scroll_widget_into_view(found[0],False)
                assert any(t.get_path_name().startswith(menu.get_path_name()+'.') and label in str(t.get_text()) for t in u.ObjectIterator(u.RichTextBlock)),name+' selected description missing'
                state['checks'][str(index)+'_'+name+'_selectable_description']=True
        state['scope']='Live menu selection/description only; does not certify mouse clicks, saving, or visual clipping.'
        state['capture_at']=time.monotonic()
    except Exception as e:finish(repr(e))
handle=u.register_slate_post_tick_callback(tick)
