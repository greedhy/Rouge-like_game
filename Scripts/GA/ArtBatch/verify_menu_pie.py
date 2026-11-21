"""Launch an isolated two-player PIE for the new batch's learning/equip flow."""
import os
import runpy
from pathlib import Path
import unreal as u

os.environ['GA_BATCH_NAMESPACE'] = 'Art'
os.environ['GA_BATCH_SPECS'] = 'Scripts/GA/ArtBatch/skills.json'
profession = os.environ.get('GA_MENU_PROFESSION', 'MAGE')
os.environ['GA_MENU_PROFESSION'] = profession
os.environ['GA_MENU_RECEIPT'] = 'Saved/GAAuthoring/ProfessionSkills/menu-' + profession.lower() + '.json'
os.environ['GA_MENU_QUIT'] = '1'
level = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert level.load_level('/Game/CompositionBatch6/QA/L_Batch6Test')
if profession == 'STANDER':
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    settings = u.GameplayStatics.get_all_actors_of_class(world, u.WorldSettings)
    assert len(settings) == 1
    settings[0].set_editor_property('default_game_mode',
        u.load_class(None, '/Game/BluePrints/Characters/Stander/BP_StanderGameMode.BP_StanderGameMode_C'))
u.EditorPythonScripting.set_keep_python_script_alive(True)
runpy.run_path(str(Path(u.Paths.project_dir()) / 'Scripts/GA/MageBatch/verify_menu_pie.py'), run_name='__main__')
level.editor_request_begin_play()
