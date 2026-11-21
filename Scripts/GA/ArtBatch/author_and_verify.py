"""Author only this batch through the existing Schema 2 authoring pipeline."""
import os
import runpy
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
os.environ['GA_BATCH_BASE'] = '/Game/UnifiedAbilities/ArtBatch'
os.environ['GA_BATCH_NAMESPACE'] = 'Art'
os.environ['GA_BATCH_SPECS'] = 'Scripts/GA/ArtBatch/skills.json'
runpy.run_path(str(root / 'Scripts/GA/MageBatch/author_and_verify.py'), run_name='__main__')
