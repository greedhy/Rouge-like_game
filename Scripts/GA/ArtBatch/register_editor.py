"""Run inside the bound editor that owns the existing catalog packages."""
import os
import runpy
from pathlib import Path
import unreal as u

os.environ['MAGE_ASSET_MODE'] = 'register'
runpy.run_path(str(Path(u.Paths.project_dir()) / 'Scripts/GA/ArtBatch/author_and_verify.py'), run_name='__main__')
