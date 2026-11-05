"""Enable requested hybrid encounter in the active dungeon profile only."""
import unreal as u,json,uuid,shutil
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve();out=root/'Saved/GAAuthoring/DungeonHybrid'/uuid.uuid4().hex;out.mkdir(parents=True)
profile=u.load_asset('/Game/RoguelikeGeneration/Gameplay/DA_DungeonGameplay');assert profile
allowed={'/Game/RoguelikeGeneration/Gameplay/DA_DungeonGameplay','/Game/RoguelikeGeneration/Gameplay/DA_DungeonMassMelee'}
assert all(p.get_path_name() in allowed for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
shutil.copy2(root/'Content/RoguelikeGeneration/Gameplay/DA_DungeonGameplay.uasset',out/'DA_DungeonGameplay-before.uasset')
source=u.load_asset('/Game/RoguelikeGeneration/Gameplay/DA_DungeonGoblin');assert source
path='/Game/RoguelikeGeneration/Gameplay/DA_DungeonMassMelee'
monster=u.load_asset(path) if u.EditorAssetLibrary.does_asset_exist(path) else u.AssetToolsHelpers.get_asset_tools().duplicate_asset('DA_DungeonMassMelee','/Game/RoguelikeGeneration/Gameplay',source)
monster.set_editor_property('health',10.);monster.set_editor_property('attack_windup_seconds',.35)
assert monster.mesh and monster.mesh.get_editor_property('physics_asset')
before=[{'id':str(r.id),'class':str(r.enemy_class),'health':r.health} for r in profile.normal_roster]
roster=[r for r in profile.normal_roster if str(r.id)!='GoblinSpear']
assert len(roster)>=4 and not any('GoblinSpear' in str(r.enemy_class) for r in roster)
profile.set_editor_property('normal_roster',roster);profile.set_editor_property('monster_type',monster)
profile.set_editor_property('use_mass_melee',True);profile.set_editor_property('mass_per_combat_room',50)
for obj in [monster,profile]:assert u.EditorAssetLibrary.save_loaded_asset(obj,False)
report={'passed':profile.mass_per_combat_room==50 and monster.health==10,'profile':profile.get_path_name(),'mass_type':monster.get_path_name(),'before_roster':before,'actor_roster':[str(r.id) for r in profile.normal_roster],'windup':monster.attack_windup_seconds,'physics':monster.mesh.get_editor_property('physics_asset').get_path_name()}
(out/'authored.json').write_text(json.dumps(report,indent=2));(root/'Saved/GAAuthoring/DungeonHybrid/latest-author.txt').write_text(str(out))
