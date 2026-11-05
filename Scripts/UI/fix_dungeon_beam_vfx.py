import sys,json,uuid
from pathlib import Path
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'skills/build-ga/scripts'))
from mcp_call import MCP
c=MCP(root,'DungeonHybridFix');out=root/'Saved/GAAuthoring/DungeonHybridFix'/uuid.uuid4().hex;out.mkdir()
def call(name,**kw):
 r=c.call(name,kw);(out/(uuid.uuid4().hex+'.json')).write_text(json.dumps(r,indent=2));return r
src='/Game/Assets/Effects/Shock/NS_ElectricBeam';dst='/Game/PureMass/Effects/NS_DungeonElectricBeam'
if not (root/'Content/PureMass/Effects/NS_DungeonElectricBeam.uasset').exists():
 call('asset_duplicate',source_asset_path=src,destination_asset_path=dst)
for emitter in ['BlueElectricBeam_5','BlackElectricBeam_6']:
 script=dst+'.NS_DungeonElectricBeam:'+emitter+'.EmitterUpdateScript'
 graph=call('niagara_graph_explain',script_path=script,include_graph=True)
 nodes=[n for n in graph['nodes'] if any(p['name']=='Absolute Beam Start' for p in n.get('pins',[]))];assert len(nodes)==1
 node=nodes[0]
 call('niagara_script_set_pin_default',script_path=script,node_id=node['id'],pin_name='Absolute Beam Start',direction='input',value=True,save=False)
 fresh=call('niagara_graph_explain',script_path=script,include_graph=True)
 assert next(p for n in fresh['nodes'] if n['id']==node['id'] for p in n['pins'] if p['name']=='Absolute Beam Start')['default_value']=='true'
call('niagara_compile_system',system_asset_path=dst,force=True,wait_for_completion=True)
call('asset_save',asset_path=dst,only_if_dirty=False)
print(out)
