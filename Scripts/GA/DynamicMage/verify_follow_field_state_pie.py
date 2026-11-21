"""Real StarFlameHalo T toggle in Standalone/Listen Server, without activation injection.

This deliberately preserves the production LocalPredicted toggle route. A failure
to activate on the remote owner is reported rather than hidden by changing net
policy or manually sending a prediction packet. Run in a fresh empty QA PIE.
"""
from pathlib import Path
import unreal as u

scope = {"__name__": "dynamic_mage_follow_field_qa",
         "DYNAMIC_MAGE_FIELD_IDENTITY": "Abilities.Batch6.StarFlameHalo"}
exec((Path(u.Paths.project_dir()) / "Scripts/GA/DynamicMage/verify_field_state_pie.py").read_text(encoding="utf-8"), scope)
